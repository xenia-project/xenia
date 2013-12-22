/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

'use strict';

var module = angular.module('xe.router', [
  'ui.router',
]);


module.config(function($stateProvider, $urlRouterProvider) {
  $urlRouterProvider.otherwise('/');

  $stateProvider.state('/', {
    template: 'empty'
  });

  $stateProvider.state('session', {
    url: '/:sessionId',
    templateUrl: 'assets/ui/session.html',
    resolve: {
      app: 'app',
      session: function($stateParams, $state, $q,
                        Session, app) {
        // If we are given a session we assume the user is trying to connect to
        // it. Attempt that now. If we fail we redirect to home, otherwise we
        // check whether it's the same game down below.
        var d = $q.defer();
        if ($stateParams.sessionId) {
          if (!app.session ||
              app.session.id != $stateParams.sessionId) {
            Session.query().then(function(infos) {
              var id = (infos[0].titleId == '00000000') ?
                  infos[0].name : infos[0].titleId;
              if (!app.session || app.session.id == id) {
                // Same session, continue.
                var p = app.connect();
                p.then(function(session) {
                  d.resolve(session);
                }, function(e) {
                  $state.go('session', {
                    'sessionId': session.id
                  }, {
                    notify: true
                  });
                  d.reject(e);
                })
              } else {
                // Different session. Create without connection.
                var p = app.open(id);
                p.then(function(session) {
                  d.resolve(session);
                }, function(e) {
                  d.reject(e);
                });
              }
            }, function(e) {
              var p = app.open($stateParams.sessionId);
              p.then(function(session) {
                d.resolve(session);
              }, function(e) {
                d.reject(e);
              });
            });
          } else {
            var p = app.open($stateParams.sessionId);
            p.then(function(session) {
              d.resolve(session);
            }, function(e) {
              d.reject(e);
            });
          }
        } else {
          d.resolve(null);
        }
        return d.promise;
      }
    },
    controller: function($scope, $stateParams, $q, app, session) {
    },
    onEnter: function() {
    },
    onExit: function() {}
  });

  $stateProvider.state('session.code', {
    url: '/code',
    templateUrl: 'assets/ui/code/code-tab.html',
    controller: function($stateParams) {
    },
    onEnter: function() {},
    onExit: function() {}
  });
  $stateProvider.state('session.code.function', {
    url: '/:module/:function',
    templateUrl: 'assets/ui/code/function-view.html',
    controller: function($scope, $stateParams) {
      $scope.moduleName = $stateParams.module;
      $scope.functionAddress = parseInt($stateParams.function, 16);
      $scope.$emit('xxx');
      $scope.$broadcast('yyy');
    },
    onEnter: function() {},
    onExit: function() {}
  });

  $stateProvider.state('session.memory', {
    url: '/memory',
    templateUrl: 'assets/ui/memory/memory-tab.html',
    controller: function($stateParams) {
    },
    onEnter: function() {},
    onExit: function() {}
  });

  $stateProvider.state('session.kernel', {
    url: '/kernel',
    templateUrl: 'assets/ui/kernel/kernel-tab.html',
    controller: function($stateParams) {
    },
    onEnter: function() {},
    onExit: function() {}
  });

  $stateProvider.state('session.gpu', {
    url: '/gpu',
    templateUrl: 'assets/ui/gpu/gpu-tab.html',
    controller: function($stateParams) {
    },
    onEnter: function() {},
    onExit: function() {}
  });

  $stateProvider.state('session.apu', {
    url: '/apu',
    templateUrl: 'assets/ui/apu/apu-tab.html',
    controller: function($stateParams) {
    },
    onEnter: function() {},
    onExit: function() {}
  });
});
