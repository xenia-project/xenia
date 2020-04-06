#ifndef XENIA_BASE_LOGZIPPER_H_
#define XENIA_BASE_LOGZIPPER_H_

#include <cstdint>
#include <string>
#include "third_party/zlib/zlib.h"

// read this: https://zlib.net/zlib_how.html
#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#include <fcntl.h>
#include <io.h>
#define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#define SET_BINARY_MODE(file)
#endif
#define FLUSH_INTERVAL 32 * 1024

namespace xe {

class LogZipper {
 public:
  LogZipper(const std::wstring& file_path);
  ~LogZipper();
  size_t write(const char* buffer, size_t size);

 private:
  gzFile_s* output;
  std::wstring gz_file;
  size_t bytes_written_since_last_flush;
};

}  // namespace xe

#endif  // XENIA_BASE_LOGZIPPER_H_