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

  $rootScope.$on('refresh', function() {
    var dataSource = app.session.dataSource;

    dataSource.getModuleList().then(function(list) {
      console.log(list);
    }, function(e) {
      log('Unable to fetch module list');
    });

    console.log('refresh');
  });
});
