/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

'use strict';

var module = angular.module('xe.ui.code.moduleInfo', [
  'ui.bootstrap',
  'xe.log',
  'xe.session'
]);


module.controller('ModuleInfoController', function(
    $rootScope, $scope, $modal, log, moduleName, moduleInfo) {
  $scope.moduleName = moduleName;
  $scope.moduleInfo = moduleInfo;

  $scope.close = function() {
    $scope.$close(null);
  };
});
