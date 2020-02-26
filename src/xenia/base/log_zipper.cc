
#include "log_zipper.h"

namespace xe {

LogZipper::LogZipper(const std::wstring& file_path) {
  // create file name
  gz_file = file_path + L".gz";

  // converst wstring name to char*
  const wchar_t* gz_file_c = gz_file.c_str();
  size_t size = (wcslen(gz_file_c) + 1) * sizeof(wchar_t);
  char* buffer = new char[size];
  std::wcstombs(buffer, gz_file_c, size);

  // create file
  output = gzopen(buffer, "wb");
};

LogZipper::~LogZipper() {
  gzflush(output, Z_FINISH);
  gzclose(output);
};

size_t LogZipper::write(const char* buffer, size_t size) {
  size_t bytes_written = gzwrite(output, buffer, (unsigned int)size);
  bytes_written_since_last_flush += bytes_written;
  if (bytes_written_since_last_flush >= FLUSH_INTERVAL) {
    gzflush(output, Z_SYNC_FLUSH);
    bytes_written_since_last_flush = 0;
  }
  return bytes_written;
};

}  // namespace xe