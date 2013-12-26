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
    $rootScope, $q, $http, $state, log,
    Breakpoint, FileDataSource, RemoteDataSource) {
  var State = function(session) {
    this.session = session;
    this.clear();
  };
  State.prototype.clear = function() {
    this.cache_ = {
      moduleList: [],
      modules: {},
      moduleFunctionLists: {},
      functions: {},
      threadStates: {},
      threadList: []
    };
  };
  State.prototype.sync = function() {
    var cache = this.cache_;
    var dataSource = this.session.dataSource;
    if (!dataSource) {
      var d = $q.defer();
      d.resolve();
      return d.promise;
    }
    var ps = [];

    // Update all modules/functions.
    var modulesUpdated = $q.defer();
    ps.push(modulesUpdated.promise);
    dataSource.getModuleList().then((function(list) {
      cache.moduleList = list;

      // Update module information.
      var moduleFetches = [];
      list.forEach(function(module) {
        if (cache.modules[module.name]) {
          return;
        }
        var moduleFetch = $q.defer();
        moduleFetches.push(moduleFetch.promise);
        dataSource.getModule(module.name).
            then(function(moduleInfo) {
          cache.modules[module.name] = moduleInfo;
          moduleFetch.resolve();
        }, function(e) {
          moduleFetch.reject(e);
        });
      });

      // Update function lists for each module.
      list.forEach(function(module) {
        var cached = cache.moduleFunctionLists[module.name];
        var functionListFetch = $q.defer();
        moduleFetches.push(functionListFetch);
        dataSource.getFunctionList(module.name, cached ? cached.version : 0).
            then(function(result) {
          if (cached) {
            cached.version = result.version;
            for (var n = 0; n < result.list.length; n++) {
              cached.list.push(result.list[n]);
            }
          } else {
            cached = cache.moduleFunctionLists[module.name] = {
              version: result.version,
              list: result.list
            };
          }
          functionListFetch.resolve();
        }, function(e) {
          functionListFetch.reject(e);
        });
      });

      $q.all(moduleFetches).then(function() {
        modulesUpdated.resolve();
      }, function(e) {
        modulesUpdated.reject();
      });
    }).bind(this), function(e) {
      modulesUpdated.reject(e);
    });

    // Update threads/thread states.
    var threadsUpdated = $q.defer();
    ps.push(threadsUpdated.promise);
    dataSource.getThreadStates().then((function(states) {
      cache.threadStates = states;
      cache.threadList = [];
      for (var threadId in states) {
        cache.threadList.push(states[threadId]);
      }
      threadsUpdated.resolve();
    }).bind(this), function(e) {
      threadsUpdated.reject(e);
    });

    var d = $q.defer();
    $q.all(ps).then((function() {
      d.resolve();
    }).bind(this), (function(e) {
      d.reject(e);
    }).bind(this));
    return d.promise;
  };
  State.prototype.getModuleList = function() {
    return this.cache_.moduleList;
  };
  State.prototype.getModule = function(moduleName) {
    return this.cache_.modules[moduleName] || null;
  };
  State.prototype.getFunctionList = function(moduleName) {
    var cached = this.cache_.moduleFunctionLists[moduleName];
    return cached ? cached.list : [];
  };
  State.prototype.getFunction = function(address) {
    return this.cache_.functions[address] || null;
  };
  State.prototype.fetchFunction = function(address) {
    var cache = this.cache_;
    var d = $q.defer();
    var cached = cache.functions[address];
    if (cached) {
      d.resolve(cached);
      return d.promise;
    }
    var dataSource = this.session.dataSource;
    if (!dataSource) {
      d.reject(new Error('Not online.'));
      return d.promise;
    }
    dataSource.getFunction(address).then(function(result) {
      cache.functions[address] = result;
      d.resolve(result);
    }, function(e) {
      d.reject(e);
    });
    return d.promise;
  }
  Object.defineProperty(State.prototype, 'threadList', {
    get: function() {
      return this.cache_.threadList || [];
    }
  });
  State.prototype.getThreadStates = function() {
    return this.cache_.threadStates || {};
  };
  State.prototype.getThreadState = function(threadId) {
    return this.cache_.threadStates[threadId] || null;
  };

  var Session = function(id, opt_dataSource) {
    this.id = id;

    this.breakpoints = {};
    this.breakpointsById = {};

    this.dataSource = opt_dataSource || null;
    this.state = new State(this);

    this.activeThread = null;

    this.paused = false;

    this.loadState();
  };

  Session.prototype.dispose = function() {
    this.saveState();
    this.disconnect();
  };

  Session.prototype.loadState = function() {
    var raw = window.localStorage[this.id];
    if (!raw) {
      return;
    }
    var json = JSON.parse(raw);
    if (!json) {
      return;
    }

    var breakpointList = json.breakpoints;
    this.breakpoints = {};
    for (var n = 0; n < breakpointList.length; n++) {
      var breakpointJson = breakpointList[n];
      var breakpoint = Breakpoint.fromJSON(breakpointJson);
      this.breakpoints[breakpointJson.address] = breakpoint;
      this.breakpointsById[breakpoint.id] = breakpoint;
    }
  };

  Session.prototype.saveState = function() {
    var json = {
      id: this.id,
      breakpoints: []
    };
    for (var key in this.breakpointsById) {
      var breakpoint = this.breakpointsById[key];
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

    var dataSource = new RemoteDataSource(url, this);
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
    var self = this;

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
    this.state.clear();
    this.activeThread = null;

    this.dataSource = dataSource;
    this.dataSource.on('online', function() {
      //
    }, this);
    this.dataSource.on('offline', function() {
      this.setDataSource(null);
    }, this);

    var ps = [];

    // Add breakpoints.
    var breakpointList = [];
    for (var key in this.breakpoints) {
      var breakpoint = this.breakpoints[key];
      if (breakpoint.enabled) {
        breakpointList.push(breakpoint);
      }
    }
    ps.push(this.dataSource.addBreakpoints(breakpointList));

    // Perform a full sync.
    var syncDeferred = $q.defer();
    ps.push(syncDeferred.promise);
    this.state.sync().then((function() {
      // Put a breakpoint at the entry point.
      // TODO(benvanik): make an option?
      var moduleList = this.state.getModuleList();
      if (!moduleList.length) {
        log.error('No modules found!');
        syncDeferred.reject(new Error('No modules found.'));
        return;
      }
      var moduleInfo = this.state.getModule(moduleList[0].name);
      if (!moduleInfo) {
        log.error('Main module not found!');
        syncDeferred.reject(new Error('Main module not found.'));
        return;
      }
      var entryPoint = moduleInfo.exeEntryPoint;
      self.addTempBreakpoint(entryPoint, entryPoint);

      syncDeferred.resolve();
    }).bind(this), (function(e) {
      syncDeferred.reject(e);
    }).bind(this));

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
    this.breakpointsById[breakpoint.id] = breakpoint;
    if (this.dataSource) {
      this.dataSource.addBreakpoint(breakpoint);
    }
    this.saveState();
    return breakpoint;
  };

  Session.prototype.addTempBreakpoint = function(fnAddress, address) {
    var breakpoint = new Breakpoint();
    breakpoint.type = Breakpoint.Type.TEMP;
    breakpoint.fnAddress = fnAddress;
    breakpoint.address = address;
    breakpoint.enabled = true;
    return this.addBreakpoint(breakpoint);
  };

  Session.prototype.addCodeBreakpoint = function(fnAddress, address) {
    var breakpoint = new Breakpoint();
    breakpoint.type = Breakpoint.Type.CODE;
    breakpoint.fnAddress = fnAddress;
    breakpoint.address = address;
    breakpoint.enabled = true;
    return this.addBreakpoint(breakpoint);
  };

  Session.prototype.removeBreakpoint = function(breakpoint) {
    delete this.breakpoints[breakpoint.address];
    delete this.breakpointsById[breakpoint.id];
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

  Session.prototype.onBreakpointHit = function(breakpointId, threadId) {
    // Now paused!
    this.paused = true;

    this.state.sync().then((function() {
      // Switch active thread.
      var thread = this.state.getThreadState(threadId);
      this.activeThread = thread;

      if (!breakpointId) {
        // Just a general pause.
        log.info('Execution paused.');
        return;
      }

      var breakpoint = this.breakpointsById[breakpointId];
      if (!breakpoint) {
        log.error('Breakpoint hit but not found.');
        return;
      }

      // TODO(benvanik): stash current breakpoint/thread/etc.

      log.info('Breakpoint hit at 0x' +
               breakpoint.address.toString(16).toUpperCase() + '.');

      $state.go('session.code.function', {
        sessionId: this.id,
        function: breakpoint.fnAddress.toString(16).toUpperCase(),
        a: breakpoint.address.toString(16).toUpperCase()
      }, {
        notify: true,
        reloadOnSearch: false
      });
    }).bind(this), (function(e) {
      log.error('Unable to synchronize state,');
    }).bind(this));
  };

  Session.prototype.continueExecution = function() {
    if (!this.dataSource) {
      return;
    }
    this.paused = false;
    this.dataSource.continueExecution().then(function() {
      log.info('Execution resumed.');
    }, function(e) {
      log.error('Unable to continue: ' + e);
    });
  };

  Session.prototype.breakExecution = function() {
    if (!this.dataSource) {
      return;
    }
    this.paused = true;
    this.dataSource.breakExecution().then(function() {
      log.info('Execution paused.');
      $rootScope.$emit('refresh');
    }, function(e) {
      log.error('Unable to break: ' + e);
    });
  };

  Session.prototype.stepNext = function(threadId) {
    if (!this.dataSource) {
      return;
    }
    this.paused = false;
    this.dataSource.stepNext(threadId).then(function() {
    }, function(e) {
      log.error('Unable to step: ' + e);
    });
  };

  return Session;
});
