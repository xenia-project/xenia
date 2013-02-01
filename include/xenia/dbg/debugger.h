/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_DBG_DEBUGGER_H_
#define XENIA_KERNEL_DBG_DEBUGGER_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <map>


namespace xe {
namespace dbg {


class Client;
class ContentSource;
class Listener;


class Debugger {
public:
  Debugger(xe_pal_ref pal);
  virtual ~Debugger();

  void RegisterContentSource(std::string& name, ContentSource* content_source);

  int Startup();

private:
  int DispatchRequest(Client* client, const char* source_name,
                      const uint8_t* data, size_t length);

  friend class Client;

private:
  xe_pal_ref pal_;
  auto_ptr<Listener> listener_;
  std::map<char*, ContentSource*> content_sources_;
};


}  // namespace dbg
}  // namespace xe


#endif  // XENIA_KERNEL_DBG_DEBUGGER_H_
