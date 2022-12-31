/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/assert.h"
#include "xenia/base/platform_win.h"
#include "xenia/base/string.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/window_win.h"

// Microsoft headers after platform_win.h.
#include <wrl/client.h>

namespace xe {
namespace ui {

class Win32FilePicker : public FilePicker {
 public:
  Win32FilePicker();
  ~Win32FilePicker() override;

  bool Show(Window* parent_window) override;

 private:
};

std::unique_ptr<FilePicker> FilePicker::Create() {
  return std::make_unique<Win32FilePicker>();
}

class CDialogEventHandler : public IFileDialogEvents,
                            public IFileDialogControlEvents {
 public:
  // IUnknown methods
  IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) {
    // dwOffset may be a DWORD or an int depending on compiler/SDK version.
    static const QITAB qit[] = {
        {&__uuidof(IFileDialogEvents),
         static_cast<decltype(qit[0].dwOffset)>(
             OFFSETOFCLASS(IFileDialogEvents, CDialogEventHandler))},
        {&__uuidof(IFileDialogControlEvents),
         static_cast<decltype(qit[1].dwOffset)>(
             OFFSETOFCLASS(IFileDialogControlEvents, CDialogEventHandler))},
        {0},
    };
    return QISearch(this, qit, riid, ppv);
  }

  IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&_cRef); }

  IFACEMETHODIMP_(ULONG) Release() {
    long cRef = InterlockedDecrement(&_cRef);
    if (!cRef) delete this;
    return cRef;
  }

  // IFileDialogEvents methods
  IFACEMETHODIMP OnFileOk(IFileDialog*) { return S_OK; }
  IFACEMETHODIMP OnFolderChange(IFileDialog*) { return S_OK; }
  IFACEMETHODIMP OnFolderChanging(IFileDialog*, IShellItem*) { return S_OK; }
  IFACEMETHODIMP OnHelp(IFileDialog*) { return S_OK; }
  IFACEMETHODIMP OnSelectionChange(IFileDialog*) { return S_OK; }
  IFACEMETHODIMP OnShareViolation(IFileDialog*, IShellItem*,
                                  FDE_SHAREVIOLATION_RESPONSE*) {
    return S_OK;
  }
  IFACEMETHODIMP OnTypeChange(IFileDialog* pfd) { return S_OK; }
  IFACEMETHODIMP OnOverwrite(IFileDialog*, IShellItem*,
                             FDE_OVERWRITE_RESPONSE*) {
    return S_OK;
  }

  // IFileDialogControlEvents methods
  IFACEMETHODIMP OnItemSelected(IFileDialogCustomize* pfdc, DWORD dwIDCtl,
                                DWORD dwIDItem) {
    return S_OK;
  }
  IFACEMETHODIMP OnButtonClicked(IFileDialogCustomize*, DWORD) { return S_OK; }
  IFACEMETHODIMP OnCheckButtonToggled(IFileDialogCustomize*, DWORD, BOOL) {
    return S_OK;
  }
  IFACEMETHODIMP OnControlActivating(IFileDialogCustomize*, DWORD) {
    return S_OK;
  }

  CDialogEventHandler() : _cRef(1) {}

 private:
  virtual ~CDialogEventHandler() = default;
  long _cRef;
};

HRESULT CDialogEventHandler_CreateInstance(REFIID riid, void** ppv) {
  *ppv = NULL;
  auto dialog_event_handler = new (std::nothrow) CDialogEventHandler();
  HRESULT hr = dialog_event_handler ? S_OK : E_OUTOFMEMORY;
  if (SUCCEEDED(hr)) {
    hr = dialog_event_handler->QueryInterface(riid, ppv);
    dialog_event_handler->Release();
  }
  return hr;
}

Win32FilePicker::Win32FilePicker() = default;

Win32FilePicker::~Win32FilePicker() = default;

bool Win32FilePicker::Show(Window* parent_window) {
  // TODO(benvanik): FileSaveDialog.
  assert_true(mode() == Mode::kOpen);

  Microsoft::WRL::ComPtr<IFileOpenDialog> file_dialog;
  HRESULT hr =
      CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                       IID_PPV_ARGS(&file_dialog));
  if (!SUCCEEDED(hr)) {
    return false;
  }

  hr = file_dialog->SetTitle((LPCWSTR)xe::to_utf16(title()).c_str());
  if (!SUCCEEDED(hr)) {
    return false;
  }

  DWORD flags;
  hr = file_dialog->GetOptions(&flags);
  if (!SUCCEEDED(hr)) {
    return false;
  }
  // FOS_FILEMUSTEXIST
  // FOS_PATHMUSTEXIST
  flags |= FOS_FORCEFILESYSTEM;
  if (multi_selection()) {
    flags |= FOS_ALLOWMULTISELECT;
  }
  if (type() == Type::kDirectory) {
    flags |= FOS_PICKFOLDERS;
  }
  hr = file_dialog->SetOptions(flags);
  if (!SUCCEEDED(hr)) {
    return false;
  }

  if (type() == Type::kFile) {
    // Set the file types to display only. Notice that this is a 1-based array.
    using u16sPair = std::pair<std::u16string, std::u16string>;
    std::vector<std::unique_ptr<u16sPair>> file_pairs;
    std::vector<COMDLG_FILTERSPEC> file_types;
    for (const auto& extension : this->extensions()) {
      const auto& file_pair =
          file_pairs.emplace_back(std::make_unique<u16sPair>(
              xe::to_utf16(extension.first), xe::to_utf16(extension.second)));
      file_types.push_back(
          {reinterpret_cast<LPCWSTR>(file_pair->first.c_str()),
           reinterpret_cast<LPCWSTR>(file_pair->second.c_str())});
    }
    hr = file_dialog->SetFileTypes(static_cast<UINT>(file_types.size()),
                                   file_types.data());
    if (!SUCCEEDED(hr)) {
      return false;
    }

    hr = file_dialog->SetFileTypeIndex(1);
    if (!SUCCEEDED(hr)) {
      return false;
    }
  }

  // Create an event handling object, and hook it up to the dialog.
  Microsoft::WRL::ComPtr<IFileDialogEvents> file_dialog_events;
  hr = CDialogEventHandler_CreateInstance(IID_PPV_ARGS(&file_dialog_events));
  if (!SUCCEEDED(hr)) {
    return false;
  }
  DWORD cookie;
  hr = file_dialog->Advise(file_dialog_events.Get(), &cookie);
  if (!SUCCEEDED(hr)) {
    return false;
  }

  // Show the dialog modally.
  hr = file_dialog->Show(
      parent_window ? static_cast<const Win32Window*>(parent_window)->hwnd()
                    : nullptr);
  file_dialog->Unadvise(cookie);
  if (!SUCCEEDED(hr)) {
    return false;
  }

  // Obtain the result once the user clicks the 'Open' button.
  // The result is an IShellItem object.
  Microsoft::WRL::ComPtr<IShellItemArray> shell_items;
  hr = file_dialog->GetResults(&shell_items);
  if (!SUCCEEDED(hr)) {
    return false;
  }

  std::vector<std::filesystem::path> selected_files;

  DWORD items_count = 0;
  shell_items->GetCount(&items_count);
  // Iterate over selected files
  for (DWORD i = 0; i < items_count; i++) {
    Microsoft::WRL::ComPtr<IShellItem> shell_item;
    shell_items->GetItemAt(i, &shell_item);
    // We are just going to print out the name of the file for sample sake.
    PWSTR file_path = nullptr;
    hr = shell_item->GetDisplayName(SIGDN_FILESYSPATH, &file_path);
    if (!SUCCEEDED(hr)) {
      return false;
    }
    selected_files.push_back(std::filesystem::path(file_path));
    CoTaskMemFree(file_path);
  }
  set_selected_files(selected_files);
  return true;
}

}  // namespace ui
}  // namespace xe
