/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

'use strict';

var module = angular.module('xe.ui.console', [
  'xe.log'
]);


module.controller('ConsoleController', function($scope, log) {
  $scope.log = log;

  $scope.commandText = '';

  $scope.issueCommand = function() {
    var command = $scope.commandText;
    $scope.commandText = '';
    if (!command) {
      return;
    }

    log.appendLine('@' + command);

    // TODO(benvanik): execute.
    console.log(command);
  };
});
