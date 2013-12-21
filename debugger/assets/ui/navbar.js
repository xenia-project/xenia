/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

'use strict';

var module = angular.module('xe.ui.navbar', []);


module.controller('NavbarController', function(
    $scope, $state, app, log) {

  $scope.connectClicked = function() {
    // TODO(benvanik): show a fancy dialog or something.
    var oldSession = app.session;
    app.connect().then(function(session) {
      if (!oldSession || oldSession.id != session.id) {
        $state.go('session', {
          'sessionId': session.id
        }, {
          notify: true
        });
      }
    }, function(e) {
      $state.go('/', {
      }, {
        notify: true
      });
    });
  };

  $scope.openClicked = function() {
    var inputEl = document.createElement('input');
    inputEl.type = 'file';
    inputEl.accept = '.xe-trace,application/x-extension-xe-trace';
    inputEl.onchange = function(e) {
      $scope.$apply(function() {
        if (inputEl.files.length) {
          //app.open(inputEl.files[0]);
          log.info('Not implemented yet');
        }
      });
    };
    inputEl.click();
  };

});
