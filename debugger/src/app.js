/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

'use strict';

var module = angular.module('app', [
  'ui.bootstrap',
  'ui.router',
  'xe.datasources',
  'xe.directives',
  'xe.filters',
  'xe.log',
  'xe.router',
  'xe.session',
  'xe.ui.code',
  'xe.ui.code.functionView',
  'xe.ui.console',
  'xe.ui.navbar'
]);


module.controller('AppController', function($scope, app) {
  this.app = app;
});


module.service('app', function(
    $rootScope, $q, $state, log, Session) {
  var App = function() {
    this.loading = false;
    this.session = null;
  };

  App.prototype.setSession = function(session) {
    this.close();

    this.session = session;
  };

  App.prototype.close = function() {
    this.loading = false;
    if (this.session) {
      this.session.dispose();
      this.session = null;
    }
  };

  App.prototype.open = function(sessionId) {
    this.close();

    var d = $q.defer();
    this.loading = true;

    log.info('Opening session ' + sessionId);

    var session = new Session(sessionId);
    this.loading = false;
    this.setSession(session);
    d.resolve(session);

    return d.promise;
  };

  App.prototype.connect = function(opt_host) {
    this.close();

    var d = $q.defer();
    this.loading = true;

    Session.query(opt_host).then((function(infos) {
      var info = infos[0];
      var id = info.titleId;
      if (id == '00000000') {
        id = info.name;
      }
      var session = new Session(id);
      var p = session.connect(opt_host);
      p.then((function(session) {
        this.loading = false;
        this.setSession(session);
        d.resolve(session);
      }).bind(this), (function(e) {
        this.loading = false;
        d.reject(e);
      }).bind(this), function(update) {
        d.notify(update);
      });
    }).bind(this), (function(e) {
      this.loading = false;
      log.info('No sessions found at ' + Session.getHost(opt_host));
      d.reject(e);
    }).bind(this));

    return d.promise;
  };

  return new App();
});


module.run(function($rootScope, app, log) {
  $rootScope.app = app;
  $rootScope.log = log;
});
