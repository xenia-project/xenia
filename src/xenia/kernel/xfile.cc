/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xfile.h"
#include "xenia/vfs/virtual_file_system.h"

#include "xenia/base/byte_stream.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xevent.h"

namespace xe {
namespace kernel {

XFile::XFile(KernelState* kernel_state, vfs::File* file, bool synchronous)
    : XObject(kernel_state, kType), file_(file), is_synchronous_(synchronous) {
  async_event_ = threading::Event::CreateAutoResetEvent(false);
}

XFile::XFile() : XObject(kType) {
  async_event_ = threading::Event::CreateAutoResetEvent(false);
}

XFile::~XFile() {
  // TODO(benvanik): signal that the file is closing?
  async_event_->Set();
  file_->Destroy();
}

X_STATUS XFile::QueryDirectory(X_FILE_DIRECTORY_INFORMATION* out_info,
                               size_t length, const char* file_name,
                               bool restart) {
  assert_not_null(out_info);

  vfs::Entry* entry = nullptr;

  if (file_name != nullptr) {
    // Only queries in the current directory are supported for now.
    assert_true(std::strchr(file_name, '\\') == nullptr);

    find_engine_.SetRule(file_name);

    // Always restart the search?
    find_index_ = 0;
    entry = file_->entry()->IterateChildren(find_engine_, &find_index_);
    if (!entry) {
      return X_STATUS_NO_SUCH_FILE;
    }
  } else {
    if (restart) {
      find_index_ = 0;
    }

    entry = file_->entry()->IterateChildren(find_engine_, &find_index_);
    if (!entry) {
      return X_STATUS_NO_MORE_FILES;
    }
  }

  auto end = reinterpret_cast<uint8_t*>(out_info) + length;
  const auto& entry_name = entry->name();
  if (reinterpret_cast<uint8_t*>(&out_info->file_name[0]) + entry_name.size() >
      end) {
    assert_always("Buffer overflow?");
    return X_STATUS_NO_SUCH_FILE;
  }

  out_info->next_entry_offset = 0;
  out_info->file_index = static_cast<uint32_t>(find_index_);
  out_info->creation_time = entry->create_timestamp();
  out_info->last_access_time = entry->access_timestamp();
  out_info->last_write_time = entry->write_timestamp();
  out_info->change_time = entry->write_timestamp();
  out_info->end_of_file = entry->size();
  out_info->allocation_size = entry->allocation_size();
  out_info->attributes = entry->attributes();
  out_info->file_name_length = static_cast<uint32_t>(entry_name.size());
  std::memcpy(out_info->file_name, entry_name.data(), entry_name.size());

  return X_STATUS_SUCCESS;
}

X_STATUS XFile::Read(void* buffer, size_t buffer_length, size_t byte_offset,
                     size_t* out_bytes_read, uint32_t apc_context) {
  if (byte_offset == -1) {
    // Read from current position.
    byte_offset = position_;
  }

  size_t bytes_read = 0;
  X_STATUS result =
      file_->ReadSync(buffer, buffer_length, byte_offset, &bytes_read);
  if (XSUCCEEDED(result)) {
    position_ += bytes_read;
  }

  XIOCompletion::IONotification notify;
  notify.apc_context = apc_context;
  notify.num_bytes = uint32_t(bytes_read);
  notify.status = result;

  NotifyIOCompletionPorts(notify);

  if (out_bytes_read) {
    *out_bytes_read = bytes_read;
  }

  async_event_->Set();
  return result;
}

X_STATUS XFile::Write(const void* buffer, size_t buffer_length,
                      size_t byte_offset, size_t* out_bytes_written,
                      uint32_t apc_context) {
  if (byte_offset == -1) {
    // Write from current position.
    byte_offset = position_;
  }

  size_t bytes_written = 0;
  X_STATUS result =
      file_->WriteSync(buffer, buffer_length, byte_offset, &bytes_written);
  if (XSUCCEEDED(result)) {
    position_ += bytes_written;
  }

  XIOCompletion::IONotification notify;
  notify.apc_context = apc_context;
  notify.num_bytes = uint32_t(bytes_written);
  notify.status = result;

  NotifyIOCompletionPorts(notify);

  if (out_bytes_written) {
    *out_bytes_written = bytes_written;
  }

  async_event_->Set();
  return result;
}

X_STATUS XFile::SetLength(size_t length) { return file_->SetLength(length); }

void XFile::RegisterIOCompletionPort(uint32_t key,
                                     object_ref<XIOCompletion> port) {
  std::lock_guard<std::mutex> lock(completion_port_lock_);

  completion_ports_.push_back({key, port});
}

void XFile::RemoveIOCompletionPort(uint32_t key) {
  std::lock_guard<std::mutex> lock(completion_port_lock_);

  for (auto it = completion_ports_.begin(); it != completion_ports_.end();
       it++) {
    if (it->first == key) {
      completion_ports_.erase(it);
      break;
    }
  }
}

bool XFile::Save(ByteStream* stream) {
  XELOGD("XFile %.8X (%s)", handle(), file_->entry()->absolute_path().c_str());

  if (!SaveObject(stream)) {
    return false;
  }

  stream->Write(file_->entry()->absolute_path());
  stream->Write<uint64_t>(position_);
  stream->Write(file_access());
  stream->Write<bool>(
      (file_->entry()->attributes() & vfs::kFileAttributeDirectory) != 0);
  stream->Write<bool>(is_synchronous_);

  return true;
}

object_ref<XFile> XFile::Restore(KernelState* kernel_state,
                                 ByteStream* stream) {
  auto file = new XFile();
  file->kernel_state_ = kernel_state;
  if (!file->RestoreObject(stream)) {
    delete file;
    return nullptr;
  }

  auto abs_path = stream->Read<std::string>();
  uint64_t position = stream->Read<uint64_t>();
  auto access = stream->Read<uint32_t>();
  auto is_directory = stream->Read<bool>();
  auto is_synchronous = stream->Read<bool>();

  XELOGD("XFile %.8X (%s)", file->handle(), abs_path.c_str());

  vfs::File* vfs_file = nullptr;
  vfs::FileAction action;
  auto res = kernel_state->file_system()->OpenFile(
      abs_path, vfs::FileDisposition::kOpen, access, is_directory, &vfs_file,
      &action);
  if (XFAILED(res)) {
    XELOGE("Failed to open XFile: error %.8X", res);
    return object_ref<XFile>(file);
  }

  file->file_ = vfs_file;
  file->position_ = position;
  file->is_synchronous_ = is_synchronous;

  return object_ref<XFile>(file);
}

void XFile::NotifyIOCompletionPorts(
    XIOCompletion::IONotification& notification) {
  std::lock_guard<std::mutex> lock(completion_port_lock_);

  for (auto port : completion_ports_) {
    notification.key_context = port.first;
    port.second->QueueNotification(notification);
  }
}

}  // namespace kernel
}  // namespace xe
