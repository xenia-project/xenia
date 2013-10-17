/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_XFILE_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_XFILE_H_

#include <xenia/kernel/modules/xboxkrnl/xobject.h>

#include <xenia/kernel/xbox.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {

class XAsyncRequest;
class XEvent;

class XFileInfo {
public:
  // FILE_NETWORK_OPEN_INFORMATION
  uint64_t          creation_time;
  uint64_t          last_access_time;
  uint64_t          last_write_time;
  uint64_t          change_time;
  uint64_t          allocation_size;
  uint64_t          file_length;
  X_FILE_ATTRIBUTES attributes;

  void Write(uint8_t* base, uint32_t p) {
    XESETUINT64BE(base + p, creation_time);
    XESETUINT64BE(base + p + 8, last_access_time);
    XESETUINT64BE(base + p + 16, last_write_time);
    XESETUINT64BE(base + p + 24, change_time);
    XESETUINT64BE(base + p + 32, allocation_size);
    XESETUINT64BE(base + p + 40, file_length);
    XESETUINT32BE(base + p + 48, attributes);
    XESETUINT32BE(base + p + 52, 0); // pad
  }
};


class XFile : public XObject {
public:
  virtual ~XFile();

  size_t position() const { return position_; }
  void set_position(size_t value) { position_ = value; }

  virtual X_STATUS Wait(uint32_t wait_reason, uint32_t processor_mode,
                        uint32_t alertable, uint64_t* opt_timeout);

  virtual X_STATUS QueryInfo(XFileInfo* out_info) = 0;

  X_STATUS Read(void* buffer, size_t buffer_length, size_t byte_offset,
                size_t* out_bytes_read);
  X_STATUS Read(void* buffer, size_t buffer_length, size_t byte_offset,
                XAsyncRequest* request);

protected:
  XFile(KernelState* kernel_state, uint32_t desired_access);
  virtual X_STATUS ReadSync(
      void* buffer, size_t buffer_length, size_t byte_offset,
      size_t* out_bytes_read) = 0;

private:
  uint32_t        desired_access_;
  XEvent*         async_event_;

  // TODO(benvanik): create flags, open state, etc.

  size_t          position_;
};


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_XFILE_H_
