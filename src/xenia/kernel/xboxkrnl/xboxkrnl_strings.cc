/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

DEFINE_bool(log_string_format_kernel_calls, false,
            "Log kernel calls with the kHighFrequency tag.", "Logging");

namespace xe {
namespace kernel {
namespace xboxkrnl {

enum FormatState {
  FS_Invalid = 0,
  FS_Unknown,
  FS_Start,
  FS_Flags,
  FS_Width,
  FS_PrecisionStart,
  FS_Precision,
  FS_Size,
  FS_Type,
  FS_End,
};

enum FormatFlags {
  FF_LeftJustify = 1 << 0,
  FF_AddLeadingZeros = 1 << 1,
  FF_AddPositive = 1 << 2,
  FF_AddPositiveAsSpace = 1 << 3,
  FF_AddNegative = 1 << 4,
  FF_AddPrefix = 1 << 5,
  FF_IsShort = 1 << 6,
  FF_IsLong = 1 << 7,
  FF_IsLongLong = 1 << 8,
  FF_IsWide = 1 << 9,
  FF_IsSigned = 1 << 10,
  FF_ForceLeadingZero = 1 << 11,
  FF_InvertWide = 1 << 12,
};

enum ArgumentSize {
  AS_Default = 0,
  AS_Short,
  AS_Long,
  AS_LongLong,
};

class FormatData {
 public:
  virtual uint16_t get() = 0;
  virtual uint16_t peek(int32_t offset) = 0;
  virtual void skip(int32_t count) = 0;
  virtual bool put(uint16_t c) = 0;
};

class ArgList {
 public:
  virtual uint32_t get32() = 0;
  virtual uint64_t get64() = 0;
};

// Making the assumption that the Xbox 360's implementation of the
// printf-functions matches what is described on MSDN's documentation for the
// Windows CRT:
//
// "Format Specification Syntax: printf and wprintf Functions"
// https://msdn.microsoft.com/en-us/library/56e442dc.aspx

std::string format_double(double value, int32_t precision, uint16_t c,
                          uint32_t flags) {
  if (precision < 0) {
    precision = 6;
  } else if (precision == 0 && c == 'g') {
    precision = 1;
  }

  std::ostringstream temp;
  temp << std::setprecision(precision);

  if (c == 'f') {
    temp << std::fixed;
  } else if (c == 'e' || c == 'E') {
    temp << std::scientific;
  } else if (c == 'a' || c == 'A') {
    temp << std::hexfloat;
  } else if (c == 'g' || c == 'G') {
    temp << std::defaultfloat;
  }

  if (c == 'E' || c == 'G' || c == 'A') {
    temp << std::uppercase;
  }

  if (flags & FF_AddPrefix) {
    temp << std::showpoint;
  }

  temp << value;
  return temp.str();
}

int32_t format_core(PPCContext* ppc_context, FormatData& data, ArgList& args,
                    const bool wide) {
  int32_t count = 0;

  char work8[512];
  char16_t work16[4];

  struct {
    const void* buffer;
    int32_t length;
    bool is_wide;
    bool swap_wide;
  } text;

  struct {
    char buffer[2];
    int32_t length;
  } prefix;

  auto state = FS_Unknown;
  uint32_t flags = 0;
  int32_t width = 0;
  int32_t precision = -1;
  ArgumentSize size = AS_Default;
  int32_t radix = 0;
  const char* digits = nullptr;

  text.buffer = nullptr;
  text.is_wide = false;
  text.swap_wide = true;
  text.length = 0;
  prefix.buffer[0] = '\0';
  prefix.length = 0;

  for (uint16_t c = data.get();; c = data.get()) {
    if (state == FS_Unknown) {
      if (!c) {  // the end
        return count;
      } else if (c != '%') {
      output:
        if (!data.put(c)) {
          return -1;
        }
        ++count;
        continue;
      }

      state = FS_Start;
      c = data.get();
      // fall through
    }

    // in any state, if c is \0, it's bad
    if (!c) {
      return -1;
    }

  restart:
    switch (state) {
      case FS_Invalid:
      case FS_Unknown:
      case FS_End:
      default: {
        assert_always();
      }

      case FS_Start: {
        if (c == '%') {
          state = FS_Unknown;
          goto output;
        }

        state = FS_Flags;

        // reset to defaults
        flags = 0;
        width = 0;
        precision = -1;
        size = AS_Default;
        radix = 0;
        digits = nullptr;

        text.buffer = nullptr;
        text.is_wide = false;
        text.swap_wide = true;
        text.length = 0;
        prefix.buffer[0] = '\0';
        prefix.length = 0;

        // fall through, don't need to goto restart
      }

      // https://msdn.microsoft.com/en-us/library/8aky45ct.aspx
      case FS_Flags: {
        if (c == '-') {
          flags |= FF_LeftJustify;
          continue;
        } else if (c == '+') {
          flags |= FF_AddPositive;
          continue;
        } else if (c == '0') {
          flags |= FF_AddLeadingZeros;
          continue;
        } else if (c == ' ') {
          flags |= FF_AddPositiveAsSpace;
          continue;
        } else if (c == '#') {
          flags |= FF_AddPrefix;
          continue;
        }
        state = FS_Width;
        // fall through, don't need to goto restart
      }

      // https://msdn.microsoft.com/en-us/library/25366k66.aspx
      case FS_Width: {
        if (c == '*') {
          width = (int32_t)args.get32();
          if (width < 0) {
            flags |= FF_LeftJustify;
            width = -width;
          }
          state = FS_PrecisionStart;
          continue;
        } else if (c >= '0' && c <= '9') {
          width *= 10;
          width += c - '0';
          continue;
        }
        state = FS_PrecisionStart;
        // fall through, don't need to goto restart
      }

      // https://msdn.microsoft.com/en-us/library/0ecbz014.aspx
      case FS_PrecisionStart: {
        if (c == '.') {
          state = FS_Precision;
          precision = 0;
          continue;
        }
        state = FS_Size;
        goto restart;
      }

      // https://msdn.microsoft.com/en-us/library/0ecbz014.aspx
      case FS_Precision: {
        if (c == '*') {
          precision = (int32_t)args.get32();
          if (precision < 0) {
            precision = -1;
          }
          state = FS_Size;
          continue;
        } else if (c >= '0' && c <= '9') {
          precision *= 10;
          precision += c - '0';
          continue;
        }
        state = FS_Size;
        // fall through
      }

      // https://msdn.microsoft.com/en-us/library/tcxf1dw6.aspx
      case FS_Size: {
        if (c == 'l') {
          if (data.peek(0) == 'l') {
            data.skip(1);
            flags |= FF_IsLongLong;
          } else {
            flags |= FF_IsLong;
          }
          state = FS_Type;
          continue;
        } else if (c == 'L') {
          // 58410826 incorrectly uses 'L' instead of 'l'.
          // TODO(gibbed): L appears to be treated as an invalid token by
          // xboxkrnl, investigate how invalid tokens are processed in xboxkrnl
          // formatting when state FF_Type is reached.
          state = FS_Type;
          continue;
        } else if (c == 'h') {
          flags |= FF_IsShort;
          state = FS_Type;
          continue;
        } else if (c == 'w') {
          flags |= FF_IsWide;
          state = FS_Type;
          continue;
        } else if (c == 'I') {
          if (data.peek(0) == '6' && data.peek(1) == '4') {
            data.skip(2);
            flags |= FF_IsLongLong;
            state = FS_Type;
            continue;
          } else if (data.peek(0) == '3' && data.peek(1) == '2') {
            data.skip(2);
            state = FS_Type;
            continue;
          } else {
            state = FS_Type;
            continue;
          }
        }
        // fall through
      }

      // https://msdn.microsoft.com/en-us/library/hf4y5e3w.aspx
      case FS_Type: {
        // wide character
        switch (c) {
          case 'C': {
            flags |= FF_InvertWide;
            // fall through
          }

          // character
          case 'c': {
            bool is_wide;
            if (flags & (FF_IsLong | FF_IsWide)) {
              // "An lc, lC, wc or wC type specifier is synonymous with C in
              // printf functions and with c in wprintf functions."
              is_wide = true;
            } else if (flags & FF_IsShort) {
              // "An hc or hC type specifier is synonymous with c in printf
              // functions and with C in wprintf functions."
              is_wide = false;
            } else {
              is_wide = ((flags & FF_InvertWide) != 0) ^ wide;
            }

            auto value = args.get32();

            if (!is_wide) {
              work8[0] = (uint8_t)value;
              text.buffer = &work8[0];
              text.length = 1;
              text.is_wide = false;
            } else {
              work16[0] = (uint16_t)value;
              text.buffer = &work16[0];
              text.length = 1;
              text.is_wide = true;
              text.swap_wide = false;
            }

            break;
          }

          // signed decimal integer
          case 'd':
          case 'i': {
            flags |= FF_IsSigned;
            digits = "0123456789";
            radix = 10;

          integer:
            assert_not_null(digits);
            assert_not_zero(radix);

            int64_t value;

            if (flags & FF_IsLongLong) {
              value = (int64_t)args.get64();
            } else if (flags & FF_IsLong) {
              value = (int32_t)args.get32();
            } else if (flags & FF_IsShort) {
              value = (int16_t)args.get32();
            } else {
              value = (int32_t)args.get32();
            }

            if (precision >= 0) {
              precision = std::min(precision, (int32_t)xe::countof(work8));
            } else {
              precision = 1;
            }

            if ((flags & FF_IsSigned) && value < 0) {
              value = -value;
              flags |= FF_AddNegative;
            }

            if (!(flags & FF_IsLongLong)) {
              value &= UINT32_MAX;
            }

            if (value == 0) {
              prefix.length = 0;
            }

            char* end = &work8[xe::countof(work8) - 1];
            char* start = end;
            start[0] = '\0';

            while (precision-- > 0 || value != 0) {
              auto digit = (int32_t)(value % radix);
              value /= radix;
              assert_true(digit < strlen(digits));
              *--start = digits[digit];
            }

            if ((flags & FF_ForceLeadingZero) &&
                (start == end || *start != '0')) {
              *--start = '0';
            }

            text.buffer = start;
            text.length = (int32_t)(end - start);
            text.is_wide = false;
            break;
          }

          // unsigned octal integer
          case 'o': {
            digits = "01234567";
            radix = 8;
            if (flags & FF_AddPrefix) {
              flags |= FF_ForceLeadingZero;
            }
            goto integer;
          }

          // unsigned decimal integer
          case 'u': {
            digits = "0123456789";
            radix = 10;
            goto integer;
          }

          // unsigned hexadecimal integer
          case 'x':
          case 'X': {
            digits = c == 'x' ? "0123456789abcdef" : "0123456789ABCDEF";
            radix = 16;

            if (flags & FF_AddPrefix) {
              prefix.buffer[0] = '0';
              prefix.buffer[1] = c == 'x' ? 'x' : 'X';
              prefix.length = 2;
            }

            goto integer;
          }

          // floating-point with exponent
          case 'e':
          case 'E': {
            // fall through
          }

          // floating-point without exponent
          case 'f': {
          floatingpoint:
            flags |= FF_IsSigned;

            int64_t dummy = args.get64();
            double value = *(double*)&dummy;

            if (value < 0) {
              value = -value;
              flags |= FF_AddNegative;
            }

            auto s = format_double(value, precision, c, flags);
            auto length = (int32_t)s.size();
            assert_true(length < xe::countof(work8));

            auto start = &work8[0];
            auto end = &start[length];

            std::memcpy(start, s.c_str(), length);
            end[0] = '\0';

            text.buffer = start;
            text.length = (int32_t)(end - start);
            text.is_wide = false;
            break;
          }

          // floating-point with or without exponent
          case 'g':
          case 'G': {
            goto floatingpoint;
          }

          // floating-point in hexadecimal
          case 'a':
          case 'A': {
            goto floatingpoint;
          }

          // pointer to integer
          case 'n': {
            auto pointer = (uint32_t)args.get32();
            if (flags & FF_IsShort) {
              SHIM_SET_MEM_16(pointer, (uint16_t)count);
            } else {
              SHIM_SET_MEM_32(pointer, (uint32_t)count);
            }
            continue;
          }

          // pointer
          case 'p': {
            digits = "0123456789ABCDEF";
            radix = 16;
            precision = 8;
            flags &= ~(FF_IsLongLong | FF_IsShort);
            flags |= FF_IsLong;
            goto integer;
          }

          // wide string
          case 'S': {
            flags |= FF_InvertWide;
            // fall through
          }

          // string
          case 's': {
            uint32_t pointer = args.get32();
            int32_t cap = precision < 0 ? INT32_MAX : precision;

            if (pointer == 0) {
              auto nullstr = "(null)";
              text.buffer = nullstr;
              text.length = std::min((int32_t)strlen(nullstr), cap);
              text.is_wide = false;
            } else {
              void* str = SHIM_MEM_ADDR(pointer);
              bool is_wide;
              if (flags & (FF_IsLong | FF_IsWide)) {
                // "An ls, lS, ws or wS type specifier is synonymous with S in
                // printf functions and with s in wprintf functions."
                is_wide = true;
              } else if (flags & FF_IsShort) {
                // "An hs or hS type specifier is synonymous with s in printf
                // functions and with S in wprintf functions."
                is_wide = false;
              } else {
                is_wide = ((flags & FF_InvertWide) != 0) ^ wide;
              }
              int32_t length;

              if (!is_wide) {
                length = 0;
                for (auto s = (const uint8_t*)str; cap > 0 && *s; ++s, cap--) {
                  length++;
                }
              } else {
                length = 0;
                for (auto s = (const uint16_t*)str; cap > 0 && *s; ++s, cap--) {
                  length++;
                }
              }

              text.buffer = str;
              text.length = length;
              text.is_wide = is_wide;
            }
            break;
          }

          // ANSI_STRING / UNICODE_STRING
          case 'Z': {
            assert_always();
            break;
          }

          default: {
            assert_always();
          }
        }
      }
    }

    if (flags & FF_IsSigned) {
      if (flags & FF_AddNegative) {
        prefix.buffer[0] = '-';
        prefix.length = 1;
      } else if (flags & FF_AddPositive) {
        prefix.buffer[0] = '+';
        prefix.length = 1;
      } else if (flags & FF_AddPositiveAsSpace) {
        prefix.buffer[0] = ' ';
        prefix.length = 1;
      }
    }

    int32_t padding = width - text.length - prefix.length;

    if (!(flags & (FF_LeftJustify | FF_AddLeadingZeros)) && padding > 0) {
      count += padding;
      while (padding-- > 0) {
        if (!data.put(' ')) {
          return -1;
        }
      }
    }

    if (prefix.length > 0) {
      int32_t remaining = prefix.length;
      count += prefix.length;
      auto b = &prefix.buffer[0];
      while (remaining-- > 0) {
        if (!data.put(*b++)) {
          return -1;
        }
      }
    }

    if ((flags & FF_AddLeadingZeros) && !(flags & (FF_LeftJustify)) &&
        padding > 0) {
      count += padding;
      while (padding-- > 0) {
        if (!data.put('0')) {
          return -1;
        }
      }
    }

    int32_t remaining = text.length;
    if (!text.is_wide) {
      // it's a const char*
      auto b = (const uint8_t*)text.buffer;
      while (remaining-- > 0) {
        if (!data.put(*b++)) {
          return -1;
        }
      }
    } else {
      // it's a const char16_t*
      auto b = (const uint16_t*)text.buffer;
      if (text.swap_wide) {
        while (remaining-- > 0) {
          if (!data.put(xe::byte_swap(*b++))) {
            return -1;
          }
        }
      } else {
        while (remaining-- > 0) {
          if (!data.put(*b++)) {
            return -1;
          }
        }
      }
    }
    count += text.length;

    // right padding
    if ((flags & FF_LeftJustify) && padding > 0) {
      count += padding;
      while (padding-- > 0) {
        if (!data.put(' ')) {
          return -1;
        }
      }
    }

    state = FS_Unknown;
  }

  return count;
}

class StackArgList : public ArgList {
 public:
  StackArgList(PPCContext* ppc_context, int32_t index)
      : ppc_context(ppc_context), index_(index) {}

  uint32_t get32() { return (uint32_t)get64(); }

  uint64_t get64() {
    auto value = SHIM_GET_ARG_64(index_);
    ++index_;
    return value;
  }

 private:
  PPCContext* ppc_context;
  int32_t index_;
};

class ArrayArgList : public ArgList {
 public:
  ArrayArgList(PPCContext* ppc_context, uint32_t arg_ptr)
      : ppc_context(ppc_context), arg_ptr_(arg_ptr), index_(0) {}

  uint32_t get32() { return (uint32_t)get64(); }

  uint64_t get64() {
    auto value = SHIM_MEM_64(arg_ptr_ + (8 * index_));
    ++index_;
    return value;
  }

 private:
  PPCContext* ppc_context;
  uint32_t arg_ptr_;
  int32_t index_;
};

class StringFormatData : public FormatData {
 public:
  StringFormatData(const uint8_t* input) : input_(input) {}

  uint16_t get() {
    uint16_t result = *input_;
    if (result) {
      input_++;
    }
    return result;
  }

  uint16_t peek(int32_t offset) { return input_[offset]; }

  void skip(int32_t count) {
    while (count-- > 0) {
      if (!get()) {
        break;
      }
    }
  }

  bool put(uint16_t c) {
    if (c >= 0x100) {
      return false;
    }
    output_.push_back(char(c));
    return true;
  }

  const std::string& str() const { return output_; }

 private:
  const uint8_t* input_;
  std::string output_;
};

class WideStringFormatData : public FormatData {
 public:
  WideStringFormatData(const uint16_t* input) : input_(input) {}

  uint16_t get() {
    uint16_t result = *input_;
    if (result) {
      input_++;
    }
    return xe::byte_swap(result);
  }

  uint16_t peek(int32_t offset) { return xe::byte_swap(input_[offset]); }

  void skip(int32_t count) {
    while (count-- > 0) {
      if (!get()) {
        break;
      }
    }
  }

  bool put(uint16_t c) {
    output_.push_back(char16_t(c));
    return true;
  }

  const std::u16string& wstr() const { return output_; }

 private:
  const uint16_t* input_;
  std::u16string output_;
};

class WideCountFormatData : public FormatData {
 public:
  WideCountFormatData(const uint16_t* input) : input_(input), count_(0) {}

  uint16_t get() {
    uint16_t result = *input_;
    if (result) {
      input_++;
    }
    return xe::byte_swap(result);
  }

  uint16_t peek(int32_t offset) { return xe::byte_swap(input_[offset]); }

  void skip(int32_t count) {
    while (count-- > 0) {
      if (!get()) {
        break;
      }
    }
  }

  bool put(uint16_t c) {
    ++count_;
    return true;
  }

  const int32_t count() const { return count_; }

 private:
  const uint16_t* input_;
  int32_t count_;
};

SHIM_CALL DbgPrint_entry(PPCContext* ppc_context) {
  uint32_t format_ptr = SHIM_GET_ARG_32(0);
  if (!format_ptr) {
    SHIM_SET_RETURN_32(X_STATUS_INVALID_PARAMETER);
    return;
  }
  auto format = (const uint8_t*)SHIM_MEM_ADDR(format_ptr);

  StackArgList args(ppc_context, 1);
  StringFormatData data(format);

  int32_t count = format_core(ppc_context, data, args, false);
  if (count <= 0) {
    SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
    return;
  }

  // trim whitespace from end of message
  auto str = data.str();
  str.erase(std::find_if(str.rbegin(), str.rend(),
                         [](uint8_t c) { return !std::isspace(c); })
                .base(),
            str.end());

  XELOGI("(DbgPrint) {}", str);

  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

// https://msdn.microsoft.com/en-us/library/2ts7cx93.aspx
SHIM_CALL _snprintf_entry(PPCContext* ppc_context) {
  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  int32_t buffer_count = SHIM_GET_ARG_32(1);
  uint32_t format_ptr = SHIM_GET_ARG_32(2);

  if (cvars::log_string_format_kernel_calls) {
    XELOGD("_snprintf({:08X}, {}, {:08X}({}), ...)", buffer_ptr, buffer_count,
           format_ptr,
           xe::load_and_swap<std::string>(SHIM_MEM_ADDR(format_ptr)));
  }

  if (buffer_ptr == 0 || buffer_count <= 0 || format_ptr == 0) {
    SHIM_SET_RETURN_32(-1);
    return;
  }

  auto buffer = (uint8_t*)SHIM_MEM_ADDR(buffer_ptr);
  auto format = (const uint8_t*)SHIM_MEM_ADDR(format_ptr);

  StackArgList args(ppc_context, 3);
  StringFormatData data(format);

  int32_t count = format_core(ppc_context, data, args, false);
  if (count < 0) {
    if (buffer_count > 0) {
      buffer[0] = '\0';  // write a null, just to be safe
    }
  } else if (count <= buffer_count) {
    std::memcpy(buffer, data.str().c_str(), count);
    if (count < buffer_count) {
      buffer[count] = '\0';
    }
  } else {
    std::memcpy(buffer, data.str().c_str(), buffer_count);
    count = -1;  // for return value
  }
  SHIM_SET_RETURN_32(count);
}

// https://msdn.microsoft.com/en-us/library/ybk95axf.aspx
SHIM_CALL sprintf_entry(PPCContext* ppc_context) {
  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  uint32_t format_ptr = SHIM_GET_ARG_32(1);

  if (cvars::log_string_format_kernel_calls) {
    XELOGD("sprintf({:08X}, {:08X}({}), ...)", buffer_ptr, format_ptr,
           xe::load_and_swap<std::string>(SHIM_MEM_ADDR(format_ptr)));
  }

  if (buffer_ptr == 0 || format_ptr == 0) {
    SHIM_SET_RETURN_32(-1);
    return;
  }

  auto buffer = (uint8_t*)SHIM_MEM_ADDR(buffer_ptr);
  auto format = (const uint8_t*)SHIM_MEM_ADDR(format_ptr);

  StackArgList args(ppc_context, 2);
  StringFormatData data(format);

  int32_t count = format_core(ppc_context, data, args, false);
  if (count <= 0) {
    buffer[0] = '\0';
  } else {
    std::memcpy(buffer, data.str().c_str(), count);
    buffer[count] = '\0';
  }
  SHIM_SET_RETURN_32(count);
}

// https://msdn.microsoft.com/en-us/library/2ts7cx93.aspx
SHIM_CALL _snwprintf_entry(PPCContext* ppc_context) {
  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  int32_t buffer_count = SHIM_GET_ARG_32(1);
  uint32_t format_ptr = SHIM_GET_ARG_32(2);

  if (cvars::log_string_format_kernel_calls) {
    XELOGD("_snwprintf({:08X}, {}, {:08X}({}), ...)", buffer_ptr, buffer_count,
           format_ptr,
           xe::to_utf8(
               xe::load_and_swap<std::u16string>(SHIM_MEM_ADDR(format_ptr))));
  }

  if (buffer_ptr == 0 || buffer_count <= 0 || format_ptr == 0) {
    SHIM_SET_RETURN_32(-1);
    return;
  }

  auto buffer = (uint16_t*)SHIM_MEM_ADDR(buffer_ptr);
  auto format = (const uint16_t*)SHIM_MEM_ADDR(format_ptr);

  StackArgList args(ppc_context, 3);
  WideStringFormatData data(format);

  int32_t count = format_core(ppc_context, data, args, true);
  if (count < 0) {
    if (buffer_count > 0) {
      buffer[0] = '\0';  // write a null, just to be safe
    }
  } else if (count <= buffer_count) {
    xe::copy_and_swap(buffer, (uint16_t*)data.wstr().c_str(), count);
    if (count < buffer_count) {
      buffer[count] = '\0';
    }
  } else {
    xe::copy_and_swap(buffer, (uint16_t*)data.wstr().c_str(), buffer_count);
    count = -1;  // for return value
  }
  SHIM_SET_RETURN_32(count);
}

// https://msdn.microsoft.com/en-us/library/ybk95axf.aspx
SHIM_CALL swprintf_entry(PPCContext* ppc_context) {
  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  uint32_t format_ptr = SHIM_GET_ARG_32(1);

  if (cvars::log_string_format_kernel_calls) {
    XELOGD("swprintf({:08X}, {:08X}({}), ...)", buffer_ptr, format_ptr,
           xe::to_utf8(
               xe::load_and_swap<std::u16string>(SHIM_MEM_ADDR(format_ptr))));
  }

  if (buffer_ptr == 0 || format_ptr == 0) {
    SHIM_SET_RETURN_32(-1);
    return;
  }

  auto buffer = (uint16_t*)SHIM_MEM_ADDR(buffer_ptr);
  auto format = (const uint16_t*)SHIM_MEM_ADDR(format_ptr);

  StackArgList args(ppc_context, 2);
  WideStringFormatData data(format);

  int32_t count = format_core(ppc_context, data, args, false);
  if (count <= 0) {
    buffer[0] = '\0';
  } else {
    xe::copy_and_swap(buffer, (uint16_t*)data.wstr().c_str(), count);
    buffer[count] = '\0';
  }
  SHIM_SET_RETURN_32(count);
}

// https://msdn.microsoft.com/en-us/library/1kt27hek.aspx
SHIM_CALL _vsnprintf_entry(PPCContext* ppc_context) {
  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  int32_t buffer_count = SHIM_GET_ARG_32(1);
  uint32_t format_ptr = SHIM_GET_ARG_32(2);
  uint32_t arg_ptr = SHIM_GET_ARG_32(3);

  if (cvars::log_string_format_kernel_calls) {
    XELOGD("_vsnprintf({:08X}, {}, {:08X}({}), {:08X})", buffer_ptr,
           buffer_count, format_ptr,
           xe::load_and_swap<std::string>(SHIM_MEM_ADDR(format_ptr)), arg_ptr);
  }

  if (buffer_ptr == 0 || buffer_count <= 0 || format_ptr == 0) {
    SHIM_SET_RETURN_32(-1);
    return;
  }

  auto buffer = (uint8_t*)SHIM_MEM_ADDR(buffer_ptr);
  auto format = (const uint8_t*)SHIM_MEM_ADDR(format_ptr);

  ArrayArgList args(ppc_context, arg_ptr);
  StringFormatData data(format);

  int32_t count = format_core(ppc_context, data, args, false);
  if (count < 0) {
    // Error.
    if (buffer_count > 0) {
      buffer[0] = '\0';  // write a null, just to be safe
    }
  } else if (count <= buffer_count) {
    // Fit within the buffer.
    std::memcpy(buffer, data.str().c_str(), count);
    if (count < buffer_count) {
      buffer[count] = '\0';
    }
  } else {
    // Overflowed buffer. We still return the count we would have written.
    std::memcpy(buffer, data.str().c_str(), buffer_count);
  }
  SHIM_SET_RETURN_32(count);
}

// https://msdn.microsoft.com/en-us/library/1kt27hek.aspx
SHIM_CALL _vsnwprintf_entry(PPCContext* ppc_context,
                            KernelState* kernel_state) {
  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  int32_t buffer_count = SHIM_GET_ARG_32(1);
  uint32_t format_ptr = SHIM_GET_ARG_32(2);
  uint32_t arg_ptr = SHIM_GET_ARG_32(3);

  if (cvars::log_string_format_kernel_calls) {
    XELOGD("_vsnwprintf({:08X}, {}, {:08X}({}), {:08X})", buffer_ptr,
           buffer_count, format_ptr,
           xe::to_utf8(
               xe::load_and_swap<std::u16string>(SHIM_MEM_ADDR(format_ptr))),
           arg_ptr);
  }

  if (buffer_ptr == 0 || buffer_count <= 0 || format_ptr == 0) {
    SHIM_SET_RETURN_32(-1);
    return;
  }

  auto buffer = (uint16_t*)SHIM_MEM_ADDR(buffer_ptr);
  auto format = (const uint16_t*)SHIM_MEM_ADDR(format_ptr);

  ArrayArgList args(ppc_context, arg_ptr);
  WideStringFormatData data(format);

  int32_t count = format_core(ppc_context, data, args, true);
  if (count < 0) {
    // Error.
    if (buffer_count > 0) {
      buffer[0] = '\0';  // write a null, just to be safe
    }
  } else if (count <= buffer_count) {
    // Fit within the buffer.
    xe::copy_and_swap(buffer, (uint16_t*)data.wstr().c_str(), count);
    if (count < buffer_count) {
      buffer[count] = '\0';
    }
  } else {
    // Overflowed buffer. We still return the count we would have written.
    xe::copy_and_swap(buffer, (uint16_t*)data.wstr().c_str(), buffer_count);
  }
  SHIM_SET_RETURN_32(count);
}

// https://msdn.microsoft.com/en-us/library/28d5ce15.aspx
SHIM_CALL vsprintf_entry(PPCContext* ppc_context) {
  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  uint32_t format_ptr = SHIM_GET_ARG_32(1);
  uint32_t arg_ptr = SHIM_GET_ARG_32(2);

  if (cvars::log_string_format_kernel_calls) {
    XELOGD("vsprintf({:08X}, {:08X}({}), {:08X})", buffer_ptr, format_ptr,
           xe::load_and_swap<std::string>(SHIM_MEM_ADDR(format_ptr)), arg_ptr);
  }

  if (buffer_ptr == 0 || format_ptr == 0) {
    SHIM_SET_RETURN_32(-1);
    return;
  }

  auto buffer = (uint8_t*)SHIM_MEM_ADDR(buffer_ptr);
  auto format = (const uint8_t*)SHIM_MEM_ADDR(format_ptr);

  ArrayArgList args(ppc_context, arg_ptr);
  StringFormatData data(format);

  int32_t count = format_core(ppc_context, data, args, false);
  if (count <= 0) {
    buffer[0] = '\0';
  } else {
    std::memcpy(buffer, data.str().c_str(), count);
    buffer[count] = '\0';
  }
  SHIM_SET_RETURN_32(count);
}

// https://msdn.microsoft.com/en-us/library/w05tbk72.aspx
SHIM_CALL _vscwprintf_entry(PPCContext* ppc_context,
                            KernelState* kernel_state) {
  uint32_t format_ptr = SHIM_GET_ARG_32(0);
  uint32_t arg_ptr = SHIM_GET_ARG_32(1);

  if (cvars::log_string_format_kernel_calls) {
    XELOGD("_vscwprintf({:08X}({}), {:08X})", format_ptr,
           xe::to_utf8(
               xe::load_and_swap<std::u16string>(SHIM_MEM_ADDR(format_ptr))),
           arg_ptr);
  }

  if (format_ptr == 0) {
    SHIM_SET_RETURN_32(-1);
    return;
  }

  auto format = (const uint16_t*)SHIM_MEM_ADDR(format_ptr);

  ArrayArgList args(ppc_context, arg_ptr);
  WideCountFormatData data(format);

  int32_t count = format_core(ppc_context, data, args, true);
  assert_true(count < 0 || data.count() == count);
  SHIM_SET_RETURN_32(count);
}

// https://msdn.microsoft.com/en-us/library/28d5ce15.aspx
SHIM_CALL vswprintf_entry(PPCContext* ppc_context) {
  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  uint32_t format_ptr = SHIM_GET_ARG_32(1);
  uint32_t arg_ptr = SHIM_GET_ARG_32(2);

  if (cvars::log_string_format_kernel_calls) {
    XELOGD("vswprintf({:08X}, {:08X}({}), {:08X})", buffer_ptr, format_ptr,
           xe::to_utf8(
               xe::load_and_swap<std::u16string>(SHIM_MEM_ADDR(format_ptr))),
           arg_ptr);
  }

  if (buffer_ptr == 0 || format_ptr == 0) {
    SHIM_SET_RETURN_32(-1);
    return;
  }

  auto buffer = (uint16_t*)SHIM_MEM_ADDR(buffer_ptr);
  auto format = (const uint16_t*)SHIM_MEM_ADDR(format_ptr);

  ArrayArgList args(ppc_context, arg_ptr);
  WideStringFormatData data(format);

  int32_t count = format_core(ppc_context, data, args, true);
  if (count <= 0) {
    buffer[0] = '\0';
  } else {
    xe::copy_and_swap(buffer, (uint16_t*)data.wstr().c_str(), count);
    buffer[count] = '\0';
  }
  SHIM_SET_RETURN_32(count);
}
#if 1
void RegisterStringExports(xe::cpu::ExportResolver* export_resolver,
                           KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", DbgPrint, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", _snprintf, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", sprintf, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", _snwprintf, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", swprintf, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", _vsnprintf, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", vsprintf, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", _vscwprintf, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", vswprintf, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", _vsnwprintf, state);
}
#endif
}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
