/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_DBG_INDEXED_CONTENT_SOURCE_H_
#define XENIA_KERNEL_DBG_INDEXED_CONTENT_SOURCE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/dbg/content_source.h>


namespace xe {
namespace dbg {


class Client;


class IndexedContentSource : public ContentSource {
public:
  IndexedContentSource();
  virtual ~IndexedContentSource();

  int DispatchRequest(Client* client, const uint8_t* data, size_t length);

protected:
  virtual int RequestIndex(Client* client, uint64_t req_id) = 0;
  virtual int RequestRange(Client* client, uint64_t req_id,
                           uint64_t start_index, uint64_t end_index) = 0;
  virtual int RequestEntry(Client* client, uint64_t req_id, uint64_t index) = 0;
};


}  // namespace dbg
}  // namespace xe


#endif  // XENIA_KERNEL_DBG_INDEXED_CONTENT_SOURCE_H_
