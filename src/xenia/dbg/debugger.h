/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DBG_DEBUGGER_H_
#define XENIA_DBG_DEBUGGER_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <map>
#include <vector>


namespace xe {
namespace dbg {


class Client;
class ContentSource;
class Listener;


class Debugger {
public:
  Debugger(xe_pal_ref pal);
  virtual ~Debugger();

  xe_pal_ref pal();

  void RegisterContentSource(ContentSource* content_source);

  int Startup();

  void Broadcast(uint32_t source_id, const uint8_t* data, size_t length);

private:
  void AddClient(Client* client);
  void RemoveClient(Client* client);
  int Dispatch(Client* client, const uint8_t* data, size_t length);

  friend class Client;

private:
  xe_pal_ref pal_;
  auto_ptr<Listener> listener_;
  std::vector<Client*> clients_;
  std::map<uint32_t, ContentSource*> content_sources_;
};


}  // namespace dbg
}  // namespace xe


#endif  // XENIA_DBG_DEBUGGER_H_
