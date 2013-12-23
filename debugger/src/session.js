/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

'use strict';

var module = angular.module('xe.session', []);


module.service('Session', function(
    $rootScope, $q, $http, log,
    Breakpoint, FileDataSource, RemoteDataSource) {
  var Session = function(id, opt_dataSource) {
    this.id = id;

    this.breakpoints = {};

    this.dataSource = opt_dataSource || null;

    this.loadState();
  };

  Session.prototype.dispose = function() {
    this.saveState();
    this.disconnect();
  };

  Session.prototype.loadState = function() {
    var json = JSON.parse(window.localStorage[this.id]);
    if (!json) {
      return;
    }

    var breakpointList = json.breakpoints;
    this.breakpoints = {};
    for (var n = 0; n < breakpointList.length; n++) {
      var breakpointJson = breakpointList[n];
      this.breakpoints[breakpointJson.address] =
          Breakpoint.fromJSON(breakpointJson);
    }
  };

  Session.prototype.saveState = function() {
    var json = {
      id: this.id,
      breakpoints: []
    };
    for (var key in this.breakpoints) {
      var breakpoint = this.breakpoints[key];
      if (breakpoint.type != Breakpoint.TEMP) {
        json.breakpoints.push(breakpoint.toJSON());
      }
    }
    window.localStorage[this.id] = JSON.stringify(json);
  };

  Session.DEFAULT_HOST = '127.0.0.1:6200';

  Session.getHost = function(opt_host) {
    return opt_host || Session.DEFAULT_HOST;
  };

  Session.query = function(opt_host) {
    var url = 'http://' +  Session.getHost(opt_host);
    var p = $http({
      method: 'GET',
      url: url + '/sessions',
      cache: false,
      timeout: 500,
      responseType: 'json'
    });
    var d = $q.defer();
    p.then(function(response) {
      if (!response.data || !response.data.length) {
        d.reject(new Error('No session data'));
        return;
      }
      d.resolve(response.data);
    }, function(e) {
      d.reject(e);
    });
    return d.promise;
  };

  Session.prototype.connect = function(opt_host) {
    this.disconnect();

    var url = 'ws://' + Session.getHost(opt_host);

    log.info('Connecting to ' + url + '...');
    log.setProgress(0);

    var d = $q.defer();

    var dataSource = new RemoteDataSource(url);
    var p = dataSource.open();
    p.then((function() {
      log.info('Connected!');
      log.clearProgress();
      this.setDataSource(dataSource).then((function() {
        d.resolve(this);
      }).bind(this), (function(e) {
        d.reject(e);
      }).bind(this));
    }).bind(this), (function(e) {
      log.error('Unable to connect: ' + e);
      log.clearProgress();
      d.reject(e);
    }).bind(this), function(update) {
      log.setProgress(update.progress);
      d.notify(update);
    });

    return d.promise;
  };

  Session.prototype.disconnect = function() {
    this.setDataSource(null);
  };

  Session.prototype.setDataSource = function(dataSource) {
    var d = $q.defer();
    if (this.dataSource) {
      this.dataSource.dispose();
      this.dataSource = null;
    }
    $rootScope.$emit('refresh');
    if (!dataSource) {
      d.resolve();
      return d.promise;
    }

    this.dataSource = dataSource;

    var ps = [];

    // Add breakpoints.
    var breakpointList = [];
    for (var key in this.breakpoints) {
      breakpointList.push(this.breakpoints[key]);
    }
    ps.push(this.dataSource.addBreakpoints(breakpointList));

    $q.all(ps).then((function() {
      this.dataSource.makeReady().then(function() {
        d.resolve();
      }, function(e) {
        log.error('Error making target ready: ' + e);
        d.reject(e);
      });
    }).bind(this), (function(e) {
      log.error('Errors preparing target: ' + e);
      this.disconnect();
      d.reject(e);
    }).bind(this));

    return d.promise;
  };

  Session.prototype.addBreakpoint = function(breakpoint) {
    this.breakpoints[breakpoint.address] = breakpoint;
    if (this.dataSource) {
      this.dataSource.addBreakpoint(breakpoint);
    }
    this.saveState();
    return breakpoint;
  };

  Session.prototype.addTempBreakpoint = function(address) {
    var breakpoint = new Breakpoint();
    breakpoint.type = Breakpoint.Type.TEMP;
    breakpoint.address = address;
    breakpoint.enabled = true;
    return this.addBreakpoint(breakpoint);
  };

  Session.prototype.addCodeBreakpoint = function(address) {
    var breakpoint = new Breakpoint();
    breakpoint.type = Breakpoint.Type.CODE;
    breakpoint.address = address;
    breakpoint.enabled = true;
    return this.addBreakpoint(breakpoint);
  };

  Session.prototype.removeBreakpoint = function(breakpoint) {
    delete this.breakpoints[breakpoint.address];
    if (this.dataSource) {
      this.dataSource.removeBreakpoint(breakpoint.id);
    }
    this.saveState();
  };

  Session.prototype.toggleBreakpoint = function(breakpoint, enabled) {
    var oldEnabled = enabled;
    breakpoint.enabled = enabled;
    if (this.dataSource) {
      if (breakpoint.enabled) {
        this.dataSource.addBreakpoint(breakpoint);
      } else {
        this.dataSource.removeBreakpoint(breakpoint.id);
      }
    }
    this.saveState();
  };

  return Session;
});
