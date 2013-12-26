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
    if (!app.session) {
      $scope.moduleList = [];
      return;
    }

    $scope.moduleList = app.session.state.getModuleList();
    if (!$scope.selectedModule) {
      if ($scope.moduleList.length) {
        $scope.selectModule($scope.moduleList[0]);
      }
    } else {
      $scope.selectModule($scope.selectedModule);
    }

    console.log('refresh');
  };
  $rootScope.$on('refresh', refresh);

  $scope.selectModule = function(module) {
    var moduleChange = module != $scope.selectedModule;
    $scope.selectedModule = module;

    if (moduleChange) {
      $scope.functionList = [];
    }

    $scope.functionList = app.session.state.getFunctionList(module.name);
  };

  $scope.showModuleInfo = function() {
    var modalInstance = $modal.open({
      templateUrl: 'assets/ui/code/module-info.html',
      controller: 'ModuleInfoController',
      windowClass: 'debugger-module-info',
      resolve: {
        moduleName: function() {
          return $scope.selectedModule.name;
        },
        moduleInfo: function() {
          return app.session.state.getModule(
              $scope.selectedModule.name);
        }
      }
    });
  };

  $scope.showThreadInfo = function() {
    var modalInstance = $modal.open({
      templateUrl: 'assets/ui/code/thread-info.html',
      controller: 'ThreadInfoController',
      windowClass: 'debugger-module-info',
      resolve: {
        thread: function() {
          return app.session.activeThread;
        }
      }
    });
  };

  $scope.showLocation = function() {
    //
  };

  if (app.session.dataSource) {
    refresh();
  }
});
