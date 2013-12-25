/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

'use strict';


function inherits(childCtor, parentCtor) {
  function tempCtor() {};
  tempCtor.prototype = parentCtor.prototype;
  childCtor.superClass_ = parentCtor.prototype;
  childCtor.prototype = new tempCtor();
  childCtor.prototype.constructor = childCtor;
};

var EventEmitter = function() {
  this.events_ = {};
};
EventEmitter.prototype.dispose = function() {
  this.events_ = {};
};
EventEmitter.prototype.on = function(name, listener, opt_scope) {
  var listeners = this.events_[name];
  if (!listeners) {
    listeners = this.events_[name] = [];
  }
  listeners.push({
    callback: listener,
    scope: opt_scope || null
  });
};
EventEmitter.prototype.off = function(name, listener, opt_scope) {
  var listeners = this.events_[name];
  if (!listeners) {
    return;
  }
  for (var n = 0; n < listeners.length; n++) {
    if (listeners[n].callback == listener) {
      listeners.splice(n, 1);
      if (!listeners.length) {
        delete this.events_[name];
      }
      return;
    }
  }
}
EventEmitter.prototype.emit = function(name, args) {
  var listeners = this.events_[name];
  if (!listeners) {
    return;
  }
  for (var n = 0; n < listeners.length; n++) {
    var listener = listeners[n];
    listener.callback.apply(listener.scope, args);
  }
};
