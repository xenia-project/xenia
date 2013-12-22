/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

'use strict';

var module = angular.module('xe.ui.code', [
  'xe.log',
  'xe.session'
]);


module.controller('CodeTabController', function(
    $rootScope, $scope, app, log) {
  $scope.moduleList = [];
  $scope.selectedModule = null;
  $scope.functionList = [];

  $rootScope.$on('refresh', function() {
    var dataSource = app.session.dataSource;

    dataSource.getModuleList().then(function(list) {
      $scope.moduleList = list;
      if (!$scope.selectedModule) {
        if (list.length) {
          $scope.selectModule(list[0]);
        }
      } else {
        $scope.selectModule($scope.selectedModule);
      }
    }, function(e) {
      log.error('Unable to fetch module list');
    });

    console.log('refresh');
  });

  $scope.selectModule = function(module) {
    var moduleChange = module != $scope.selectedModule;
    $scope.selectedModule = module;

    if (moduleChange) {
      $scope.functionList = [];
    }

    var dataSource = app.session.dataSource;
    dataSource.getFunctionList(module.name).then(function(list) {
      $scope.functionList = list;
    }, function(e) {
      log.error('Unable to fetch function list');
    });
  };
});
