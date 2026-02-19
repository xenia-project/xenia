/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                *
 ******************************************************************************
 * Copyright 2025. All rights reserved.                                       *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/update_manager.h"

#include <chrono>
#include <functional>
#include <memory>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "build/version.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX 
#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace xe {
namespace ui {

namespace {

// GitHub API endpoint for latest release of Xenia
const wchar_t* kGitHubApiHost = L"api.github.com";
const wchar_t* kGitHubApiPath =
    L"/repos/xenia-project/release-builds-windows/releases/latest";

// Helper function for converting a wide string to UTF-8
std::string WideToUtf8(const std::wstring& wide) {
  if (wide.empty()) return {};
  int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                 static_cast<int>(wide.size()), nullptr, 0,
                                 nullptr, nullptr);
  std::string result(size, 0);
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

// Helper function for converting UTF-8 to wide string
std::wstring Utf8ToWide(const std::string& utf8) {
  if (utf8.empty()) return {};
  int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                 static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring result(size, 0);
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                      result.data(), size);
  return result;
}

// JSON parser/extraction tool for the fields being queried
std::string ExtractJsonString(const std::string& json, const std::string& key) {
  std::string search = "\"" + key + "\":\"";
  size_t pos = json.find(search);
  if (pos == std::string::npos) {
    XELOGE("Key '{}' not found in JSON", key);
    return {};
  }

  pos += search.length();
  size_t end = json.find("\"", pos);
  if (end == std::string::npos) {
    XELOGE("Closing quote not found for key '{}'", key);
    return {};
  }

  std::string value = json.substr(pos, end - pos);
  XELOGI("ExtractJsonString: {} = {}", key, value);
  return value;
}

}  

UpdateManager::UpdateManager() = default;
UpdateManager::~UpdateManager() = default;

// Use Xenia's build commit as the current version
std::string UpdateManager::GetCurrentVersion() {
  
  return XE_BUILD_COMMIT_SHORT;
}

void UpdateManager::CheckForUpdatesAsync(
    std::function<void(const UpdateInfo&)> callback) {
  std::thread([this, callback]() {
    UpdateInfo info;
    info.update_available = false;

    HINTERNET hSession = nullptr;
    HINTERNET hConnect = nullptr;
    HINTERNET hRequest = nullptr;

    try {
      // Initialize WinHTTP
      hSession =
          WinHttpOpen(L"Xenia-Emulator/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

      if (!hSession) {
        XELOGE("WinHttpOpen failed: {}", GetLastError());
        callback(info);
        return;
      }

      // Connect to GitHub API
      hConnect = WinHttpConnect(hSession, kGitHubApiHost,
                                INTERNET_DEFAULT_HTTPS_PORT, 0);
      if (!hConnect) {
        XELOGE("WinHttpConnect failed: {}", GetLastError());
        callback(info);
        return;
      }

      // Create HTTPS request
      hRequest = WinHttpOpenRequest(
          hConnect, L"GET", kGitHubApiPath, nullptr, WINHTTP_NO_REFERER,
          WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

      if (!hRequest) {
        XELOGE("WinHttpOpenRequest failed: {}", GetLastError());
        callback(info);
        return;
      }
      // Disable SSL certificate validation
      DWORD security_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                             SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
      WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &security_flags,
                       sizeof(security_flags));

      // Add User-Agent header
      std::wstring headers =
          L"User-Agent: Xenia-Emulator\r\n"
          L"Accept: application/vnd.github+json\r\n";
      WinHttpAddRequestHeaders(hRequest, headers.c_str(),
                               static_cast<DWORD>(headers.length()),
                               WINHTTP_ADDREQ_FLAG_ADD);

      // Send request
      if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        XELOGE("WinHttpSendRequest failed: {}", GetLastError());
        callback(info);
        return;
      }

      // Receive response
      if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        XELOGE("WinHttpReceiveResponse failed: {}", GetLastError());
        callback(info);
        return;
      }

      // Read response data
      std::string response_data;
      DWORD bytes_available = 0;
      DWORD bytes_read = 0;

      do {
        bytes_available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &bytes_available)) {
          XELOGE("WinHttpQueryDataAvailable failed: {}", GetLastError());
          break;
        }

        if (bytes_available > 0) {
          std::vector<char> buffer(bytes_available + 1);
          if (WinHttpReadData(hRequest, buffer.data(), bytes_available,
                              &bytes_read)) {
            response_data.append(buffer.data(), bytes_read);
          }
        }
      } while (bytes_available > 0);

      // Parse response
      info = ParseReleaseInfo(response_data);
      std::string current_version = GetCurrentVersion();

      if (!info.version.empty()) {
        if (response_data.find(current_version) != std::string::npos) {
          XELOGI("Already running latest version (commit: {})",
                 current_version);
          info.update_available = false;
        } else if (info.version == current_version) {
          XELOGI("Already running latest version: {}", current_version);
          info.update_available = false;
        } else {
          XELOGI("Update available: {} (current: {})", info.version,
                 current_version);
          info.update_available = true;
        }
      } else {
        XELOGI("No version information found in release");
        info.update_available = false;
      }
      // Catch any other exception not listed above
    } catch (...) {
      XELOGE("Exception during update check");
    }

    // Cleanup for WinHTTP
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);

    // Call callback with result
    callback(info);
  }).detach();
}

UpdateInfo UpdateManager::ParseReleaseInfo(const std::string& json_data) {
  UpdateInfo info;

  XELOGI("Parsing JSON response ({} bytes)", json_data.size());

  // Extract tag_name as the version
  info.version = ExtractJsonString(json_data, "tag_name");
  if (info.version.empty()) {
    XELOGE("Failed to extract version from JSON");
    return info;
  }
  XELOGI("Version: {}", info.version);

  // Find the assets array
  size_t assets_start = json_data.find("\"assets\":");
  if (assets_start == std::string::npos) {
    XELOGE("No 'assets' field found in JSON");
    return info;
  }
  XELOGI("Found assets field at position {}", assets_start);

  // Find xenia_master.zip in the release assets (will be extracted for installation)
  size_t asset_name_pos =
      json_data.find("\"name\":\"xenia_master.zip\"", assets_start);

  if (asset_name_pos == std::string::npos) {
    XELOGE("xenia_master.zip not found in assets");
    return info;
  }

  XELOGI("Found xenia_master.zip at position {}", asset_name_pos);

  // Find the asset object boundaries
  // Go backwards to find the opening brace of this asset object
  size_t asset_obj_start = json_data.rfind("{", asset_name_pos);
  if (asset_obj_start == std::string::npos || asset_obj_start < assets_start) {
    XELOGE("Could not find asset object start");
    return info;
  }

  // Go forward to find the closing brace of this asset object
  // Count braces to handle nested objects
  int brace_count = 1;
  size_t asset_obj_end = asset_obj_start + 1;
  while (asset_obj_end < json_data.size() && brace_count > 0) {
    if (json_data[asset_obj_end] == '{') {
      brace_count++;
    } else if (json_data[asset_obj_end] == '}') {
      brace_count--;
    }
    asset_obj_end++;
  }

  if (brace_count != 0) {
    XELOGE("Could not find asset object end");
    return info;
  }

  XELOGI("Asset object spans from {} to {}", asset_obj_start, asset_obj_end);

  // Extract the asset object substring
  std::string asset_obj =
      json_data.substr(asset_obj_start, asset_obj_end - asset_obj_start);

  // Find browser_download_url within this object
  size_t url_field_pos = asset_obj.find("\"browser_download_url\":\"");
  if (url_field_pos == std::string::npos) {
    XELOGE("browser_download_url not found in asset object");
    XELOGI("Asset object: {}",
           asset_obj.substr(0, std::min(size_t(500), asset_obj.size())));
    return info;
  }

  // Extract the URL value
  size_t url_start = url_field_pos + 24; 
  size_t url_end = asset_obj.find("\"", url_start);

  if (url_end == std::string::npos) {
    XELOGE("Could not find end of URL");
    return info;
  }

  info.download_url = asset_obj.substr(url_start, url_end - url_start);
  XELOGI("Extracted download URL: {}", info.download_url);

  // Validate the URL
  if (info.download_url.empty() ||
      info.download_url.find("http") == std::string::npos) {
    XELOGE("Invalid download URL: {}", info.download_url);
    info.download_url.clear();
    return info;
  }

  return info;
}

void UpdateManager::DownloadAndInstallUpdate(
    const std::string& download_url,
    std::function<void(uint64_t, uint64_t)> progress_callback,
    std::function<void(bool)> completion_callback) {
  std::thread([this, download_url, progress_callback, completion_callback]() {
    try {
      XELOGI("Starting download and install for URL: {}", download_url);

      // Download the file to zip_data
      auto zip_data = DownloadFile(download_url, progress_callback);

      XELOGI("Download completed. Received {} bytes", zip_data.size());

      if (zip_data.empty()) {
        XELOGE("Failed to download update - no data received");
        completion_callback(false);
        return;
      }

      // Extract and replace files
      XELOGI("Starting extraction...");
      bool success = ExtractAndReplace(zip_data);

      if (success) {
        XELOGI("Update installed successfully");
        completion_callback(true);

        // Restart Xenia after 2 seconds elapsed
        XELOGI("Restarting in 2 seconds...");
        std::this_thread::sleep_for(std::chrono::seconds(2));
        RestartApplication();
      } else {
        XELOGE("Failed to install update - extraction failed");
        completion_callback(false);
      }

    } catch (const std::exception& e) {
      XELOGE("Exception during update installation: {}", e.what());
      completion_callback(false);
    } catch (...) {
      XELOGE("Unknown exception during update installation");
      completion_callback(false);
    }
  }).detach();
}

std::vector<uint8_t> UpdateManager::DownloadFile(
    const std::string& url,
    std::function<void(uint64_t, uint64_t)> progress_callback) {
  std::vector<uint8_t> file_data;

  XELOGI("Attempting to download from URL: {}", url);

  // Parse URL to extract host and path
  std::regex url_regex(R"(https?://([^/]+)(/.*)?)");
  std::smatch match;

  if (!std::regex_match(url, match, url_regex) || match.size() < 2) {
    XELOGE("Invalid URL format: {}", url);

    // Try alternative parsing for GitHub URLs
    size_t protocol_end = url.find("://");
    if (protocol_end == std::string::npos) {
      XELOGE("No protocol found in URL");
      return file_data;
    }

    size_t host_start = protocol_end + 3;
    size_t path_start = url.find('/', host_start);

    if (path_start == std::string::npos) {
      XELOGE("No path found in URL");
      return file_data;
    }

    std::string host = url.substr(host_start, path_start - host_start);
    std::string path = url.substr(path_start);

    XELOGI("Manually parsed - Host: {}, Path: {}", host, path);

    return DownloadFileWithParsedUrl(Utf8ToWide(host), Utf8ToWide(path),
                                     progress_callback);
  }

  std::wstring host = Utf8ToWide(match[1].str());
  std::wstring path = match.size() > 2 ? Utf8ToWide(match[2].str()) : L"/";

  if (path.empty()) {
    path = L"/";
  }

  XELOGI("Parsed URL - Host: {}, Path: {}", WideToUtf8(host), WideToUtf8(path));

  return DownloadFileWithParsedUrl(host, path, progress_callback);
}

std::vector<uint8_t> UpdateManager::DownloadFileWithParsedUrl(
    const std::wstring& host, const std::wstring& path,
    std::function<void(uint64_t, uint64_t)> progress_callback) {
  std::vector<uint8_t> file_data;

  XELOGI("Downloading from host: {}, path: {}", WideToUtf8(host),
         WideToUtf8(path));

  HINTERNET hSession = nullptr;
  HINTERNET hConnect = nullptr;
  HINTERNET hRequest = nullptr;

  try {
    hSession =
        WinHttpOpen(L"Xenia-Emulator/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

    if (!hSession) {
      XELOGE("WinHttpOpen failed: {}", GetLastError());
      return file_data;
    }

    hConnect =
        WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
      XELOGE("WinHttpConnect failed: {}", GetLastError());
      return file_data;
    }

    hRequest = WinHttpOpenRequest(
        hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

    if (!hRequest) {
      XELOGE("WinHttpOpenRequest failed: {}", GetLastError());
      return file_data;
    }

    DWORD security_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                           SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                           SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                           SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &security_flags,
                     sizeof(security_flags));

    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
                     sizeof(redirect_policy));

    XELOGI("Sending HTTP request...");
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
      XELOGE("WinHttpSendRequest failed: {}", GetLastError());
      return file_data;
    }

    XELOGI("Waiting for response...");
    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
      XELOGE("WinHttpReceiveResponse failed: {}", GetLastError());
      return file_data;
    }

    DWORD status_code = 0;
    DWORD size = sizeof(status_code);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        nullptr, &status_code, &size, nullptr);

    XELOGI("HTTP Status: {}", status_code);

    if (status_code != 200) {
      XELOGE("Unexpected HTTP status code: {}", status_code);
      return file_data;
    }

    // Get content length (to be used for calculating download progress in UI and in logs)
    DWORD content_length = 0;
    size = sizeof(content_length);
    if (WinHttpQueryHeaders(
            hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &content_length, &size, nullptr)) {
      XELOGI("Content length: {} bytes ({:.2f} MB)", content_length,
             content_length / 1024.0 / 1024.0);
    } else {
      XELOGI("Content length not provided by server");
    }

    // Download with progress tracking
    DWORD bytes_available = 0;
    DWORD bytes_read = 0;
    DWORD total_downloaded = 0;

    XELOGI("Starting download...");

    do {
      bytes_available = 0;
      if (!WinHttpQueryDataAvailable(hRequest, &bytes_available)) {
        XELOGE("WinHttpQueryDataAvailable failed: {}", GetLastError());
        break;
      }

      if (bytes_available > 0) {
        std::vector<uint8_t> buffer(bytes_available);
        if (WinHttpReadData(hRequest, buffer.data(), bytes_available,
                            &bytes_read)) {
          file_data.insert(file_data.end(), buffer.begin(),
                           buffer.begin() + bytes_read);
          total_downloaded += bytes_read;

          // Report progress with bytes downloaded
          if (progress_callback) {
            progress_callback(total_downloaded, content_length);  
          }

          if (total_downloaded % (5 * 1024 * 1024) < bytes_read) {
            XELOGI("Downloaded {:.2f} MB...",
                   total_downloaded / 1024.0 / 1024.0);
          }
        } else {
          XELOGE("WinHttpReadData failed: {}", GetLastError());
          break;
        }
      }
    } while (bytes_available > 0);

    XELOGI("Download complete: {} bytes ({:.2f} MB)", total_downloaded,
           total_downloaded / 1024.0 / 1024.0);

  } catch (const std::exception& e) {
    XELOGE("Exception during file download: {}", e.what());
  } catch (...) {
    XELOGE("Unknown exception during file download");
  }

  if (hRequest) WinHttpCloseHandle(hRequest);
  if (hConnect) WinHttpCloseHandle(hConnect);
  if (hSession) WinHttpCloseHandle(hSession);

  return file_data;
}

bool UpdateManager::ExtractAndReplace(const std::vector<uint8_t>& zip_data) {
  XELOGI("ExtractAndReplace called with {} bytes", zip_data.size());

  // Get temp path
  wchar_t temp_path[MAX_PATH];
  GetTempPathW(MAX_PATH, temp_path);

  std::wstring zip_file = std::wstring(temp_path) + L"xenia_update.zip";

  // Write .zip to temp file
  HANDLE hFile = CreateFileW(zip_file.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    XELOGE("Failed to create temp file: {}", GetLastError());
    return false;
  }

  DWORD bytes_written = 0;
  if (!WriteFile(hFile, zip_data.data(), static_cast<DWORD>(zip_data.size()),
                 &bytes_written, nullptr)) {
    XELOGE("Failed to write ZIP file: {}", GetLastError());
    CloseHandle(hFile);
    return false;
  }
  CloseHandle(hFile);
  XELOGI("Wrote {} bytes to ZIP file", bytes_written);

  // Get paths
  std::wstring app_dir = xe::filesystem::GetExecutableFolder();
  std::wstring exe_path = xe::filesystem::GetExecutablePath();
  std::wstring temp_extract_dir =
      std::wstring(temp_path) + L"xenia_update_temp\\";

  // Extract .zip via PowerShell
  std::wstring extract_cmd =
      L"PowerShell -WindowStyle Hidden -Command \"Expand-Archive -Path '" +
      zip_file + L"' -DestinationPath '" + temp_extract_dir + L"' -Force\"";

  XELOGI("Extracting ZIP...");
  STARTUPINFOW si = {sizeof(si)};
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi = {};

  if (!CreateProcessW(nullptr, const_cast<wchar_t*>(extract_cmd.c_str()),
                      nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    XELOGE("Failed to start extraction: {}", GetLastError());
    return false;
  }

  WaitForSingleObject(pi.hProcess, 30000);
  DWORD exit_code;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  if (exit_code != 0) {
    XELOGE("Extraction failed with exit code: {}", exit_code);
    return false;
  }

  XELOGI("Extraction complete");

  // Create batch script updater for overwriting the contents of the Xenia folder
  std::wstring batch_file = std::wstring(temp_path) + L"xenia_update.bat";
  std::wstring source_dir = temp_extract_dir;  

  // Get process ID for waiting
  DWORD pid = GetCurrentProcessId();

  // Batch script content
  std::string batch_content =
      "@echo off\r\n"
      "REM Wait for process to exit\r\n"
      ":wait_loop\r\n"
      "tasklist /FI \"PID eq " +
      std::to_string(pid) + "\" 2>NUL | find \"" + std::to_string(pid) +
      "\" >NUL\r\n"
      "if \"%ERRORLEVEL%\"==\"0\" (\r\n"
      "    timeout /t 1 /nobreak >nul\r\n"
      "    goto wait_loop\r\n"
      ")\r\n"
      "\r\n"
      "timeout /t 1 /nobreak >nul\r\n"
      "\r\n"
      "set SOURCE_DIR=" +
      WideToUtf8(source_dir) +
      "\r\n"
      "set DEST_DIR=" +
      WideToUtf8(app_dir) +
      "\r\n"
      "\r\n"
      "REM Backup old xenia.exe\r\n"
      "cd /d \"%DEST_DIR%\"\r\n"
      "if exist xenia.exe move /y xenia.exe xenia.exe.old >nul 2>&1\r\n"
      "\r\n"
      "REM Copy all new files\r\n"
      "xcopy /E /I /Y /R /Q \"%SOURCE_DIR%\\*\" \"%DEST_DIR%\\\" >nul 2>&1\r\n"
      "\r\n"
      "if errorlevel 1 (\r\n"
      "    if exist xenia.exe.old move /y xenia.exe.old xenia.exe >nul 2>&1\r\n"
      "    exit /b 1\r\n"
      ")\r\n"
      "\r\n"
      "REM Delete backup if successful\r\n"
      "if exist xenia.exe.old del /q xenia.exe.old >nul 2>&1\r\n"
      "\r\n"
      "timeout /t 1 /nobreak >nul\r\n"
      "\r\n"
      "REM Restart Xenia\r\n"
      "start \"\" \"%DEST_DIR%\\xenia.exe\"\r\n"
      "\r\n"
      "REM Cleanup\r\n"
      "timeout /t 1 /nobreak >nul\r\n"
      "del /q \"" +
      WideToUtf8(zip_file) +
      "\" >nul 2>&1\r\n"
      "rd /s /q \"" +
      WideToUtf8(temp_extract_dir) +
      "\" >nul 2>&1\r\n"
      "\r\n"
      "REM Self-delete and close\r\n"
      "(goto) 2>nul & del \"%~f0\"\r\n";

  // Write batch file
  HANDLE hBatch = CreateFileW(batch_file.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hBatch == INVALID_HANDLE_VALUE) {
    XELOGE("Failed to create batch file: {}", GetLastError());
    return false;
  }

  DWORD batch_written = 0;
  WriteFile(hBatch, batch_content.c_str(),
            static_cast<DWORD>(batch_content.size()), &batch_written, nullptr);
  CloseHandle(hBatch);

  XELOGI("Batch updater created: {} bytes", batch_written);
  XELOGI("Update staged successfully");

  return true;
}

void UpdateManager::RestartApplication() {
  wchar_t temp_path[MAX_PATH];
  GetTempPathW(MAX_PATH, temp_path);
  std::wstring batch_file = std::wstring(temp_path) + L"xenia_update.bat";

  XELOGI("Launching batch updater: {}", WideToUtf8(batch_file));

  // Launch batch script
  STARTUPINFOW si = {sizeof(si)};
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_SHOW;  
  PROCESS_INFORMATION pi = {};

  std::wstring cmd = L"cmd.exe /c \"" + batch_file + L"\"";

  if (CreateProcessW(nullptr, const_cast<wchar_t*>(cmd.c_str()), nullptr,
                     nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &si,
                     &pi)) {
    XELOGI("Updater launched successfully");
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Give batch script time to start
    Sleep(500);

    // Exit Xenia immediately
    XELOGI("Exiting Xenia for update...");
    ExitProcess(0);
  } else {
    XELOGE("Failed to launch updater: {}", GetLastError());
  }
}

}  // namespace ui
}  // namespace xe
