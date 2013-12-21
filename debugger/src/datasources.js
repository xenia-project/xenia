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


var DataSource = function(source) {
  this.source = source;
  this.online = false;
  this.status = 'disconnected';
};
DataSource.prototype.open = function() {};
DataSource.prototype.dispose = function() {};


module.service('RemoteDataSource', function($q) {
  var RemoteDataSource = function(url) {
    DataSource.call(this, url);
    this.url = url;
    this.socket = null;
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
    }).bind(this);

    return d.promise;
  };

  RemoteDataSource.prototype.dispose = function() {
    this.online = false;
    this.status = 'disconnected';
    if (this.socket) {
      this.socket.close();
      this.socket = null;
    }
    DataSource.prototype.dispose.call(this);
  };

  return RemoteDataSource;
});


module.service('FileDataSource', function($q) {
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
