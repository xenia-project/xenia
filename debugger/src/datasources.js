/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

'use strict';

var module = angular.module('xe.datasources', []);


module.service('DataSource', function($q) {
  var DataSource = function(source) {
    this.source = source;
    this.online = false;
    this.status = 'disconnected';
  };
  DataSource.prototype.open = function() {};
  DataSource.prototype.dispose = function() {};
  DataSource.prototype.issue = function(command) {};

  DataSource.prototype.addBreakpoint = function(breakpoint) {
    return this.addBreakpoints([breakpoint]);
  };

  DataSource.prototype.addBreakpoints = function(breakpoints) {
    if (!breakpoints.length) {
      var d = $q.defer();
      d.resolve();
      return d.promise;
    }
    return this.issue({
      command: 'cpu.add_breakpoints',
      breakpoints: breakpoints.map(function(breakpoint) {
        return breakpoint.toJSON();
      })
    });
  };

  DataSource.prototype.removeBreakpoint = function(breakpointId) {
    return this.removeBreakpoints([breakpointId]);
  };

  DataSource.prototype.removeBreakpoints = function(breakpointIds) {
    return this.issue({
      command: 'cpu.remove_breakpoints',
      breakpointIds: breakpointIds
    });
  };

  DataSource.prototype.removeAllBreakpoints = function() {
    return this.issue({
      command: 'cpu.remove_all_breakpoints'
    });
  };

  DataSource.prototype.getModuleList = function() {
    return this.issue({
      command: 'cpu.get_module_list'
    });
  };

  DataSource.prototype.getModule = function(moduleName) {
    return this.issue({
      command: 'cpu.get_module',
      module: moduleName
    });
  };

  DataSource.prototype.getFunctionList = function(moduleName) {
    return this.issue({
      command: 'cpu.get_function_list',
      module: moduleName
    });
  };

  DataSource.prototype.getFunction = function(address) {
    return this.issue({
      command: 'cpu.get_function',
      address: address
    });
  };

  return DataSource;
});

module.service('RemoteDataSource', function($q, DataSource) {
  var RemoteDataSource = function(url) {
    DataSource.call(this, url);
    this.url = url;
    this.socket = null;
    this.nextRequestId_ = 1;
    this.pendingRequests_ = {};
  };
  inherits(RemoteDataSource, DataSource);

  RemoteDataSource.prototype.open = function() {
    var url = this.url;

    this.online = false;
    this.status = 'connecting';

    var d = $q.defer();

    this.socket = new WebSocket(url, []);
    this.socket.onopen = (function() {
      // TODO(benvanik): handshake

      this.online = true;
      this.status = 'connected';
      d.resolve();
    }).bind(this);

    this.socket.onclose = (function(e) {
      this.online = false;
      if (this.status == 'connecting') {
        this.status = 'disconnected';
        d.reject(e.code + ' ' + e.reason);
      } else {
        this.status = 'disconnected';
      }
    }).bind(this);

    this.socket.onerror = (function(e) {
      // ?
    }).bind(this);

    this.socket.onmessage = (function(e) {
      console.log('message', e.data);
      var json = JSON.parse(e.data);
      if (json.requestId) {
        // Response to a previous request.
        var request = this.pendingRequests_[json.requestId];
        if (request) {
          delete this.pendingRequests_[json.requestId];
          if (json.status) {
            request.deferred.resolve(json.result);
          } else {
            request.deferred.reject(json.result);
          }
        }
      } else {
        // Notification.
      }
    }).bind(this);

    return d.promise;
  };

  RemoteDataSource.prototype.dispose = function() {
    this.pendingRequests_ = {};
    this.online = false;
    this.status = 'disconnected';
    if (this.socket) {
      this.socket.close();
      this.socket = null;
    }
    DataSource.prototype.dispose.call(this);
  };

  RemoteDataSource.prototype.issue = function(command) {
    var d = $q.defer();
    command.requestId = this.nextRequestId_++;
    this.socket.send(JSON.stringify(command));
    command.deferred = d;
    this.pendingRequests_[command.requestId] = command;
    return d.promise;
  };

  return RemoteDataSource;
});


module.service('FileDataSource', function($q, DataSource) {
  var FileDataSource = function(file) {
    DataSource.call(this, file.name);
    this.file = file;
  };
  inherits(FileDataSource, DataSource);

  FileDataSource.prototype.open = function() {
    this.status = 'connecting';

    var d = $q.defer();

    var self = this;
    window.setTimeout(function() {
      $scope.$apply((function() {
        // TODO(benvanik): scan/load trace

        this.online = true;
        this.status = 'connected';
        d.resolve();
      }).bind(self));
    });

    return d.promise;
  };

  FileDataSource.prototype.dispose = function() {
    this.online = false;
    if (this.file) {
      if (this.file.close) {
        this.file.close();
      }
      this.file = null;
    }
    DataSource.prototype.dispose.call(this);
  };

  return FileDataSource;
});
