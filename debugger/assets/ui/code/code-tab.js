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
  'ui.bootstrap',
  'xe.log',
  'xe.session'
]);


module.controller('CodeTabController', function(
    $rootScope, $scope, $modal, app, log) {
  $scope.moduleList = [];
  $scope.selectedModule = null;
  $scope.functionList = [];

  function refresh() {
    if (!app.session || !app.session.dataSource) {
      $scope.moduleList = [];
      return;
    }
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
  };
  $rootScope.$on('refresh', refresh);

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

  $scope.showModuleInfo = function(module) {
    var modalInstance = $modal.open({
      templateUrl: 'assets/ui/code/module-info.html',
      controller: 'ModuleInfoController',
      windowClass: 'debugger-module-info',
      resolve: {
        moduleName: function() {
          return $scope.selectedModule.name;
        },
        moduleInfo: function() {
          return app.session.dataSource.getModule(
              $scope.selectedModule.name);
        }
      }
    });
    modalInstance.result.then(function() {
    }, function () {
    });
  };

  $scope.showLocation = function() {
    //
  };

  if (app.session.dataSource) {
    refresh();
  }
});
