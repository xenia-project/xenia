/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

'use strict';

var module = angular.module('xe.ui.code.functionView', [
  'xe.log',
  'xe.session'
]);


module.controller('FunctionViewController', function(
    $rootScope, $scope, app, log) {
  $scope.codeType = 'ppc';

  function refresh() {
    var dataSource = app.session.dataSource;

    dataSource.getFunction($scope.functionAddress).then(function(fn) {
      $scope.fn = fn;
    }, function(e) {
      log.error('Unable to fetch function');
    });
  };
  $rootScope.$on('refresh', refresh);
  $scope.$watch('functionAddress', refresh);
});
