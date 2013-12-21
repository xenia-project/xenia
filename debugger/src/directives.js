/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

'use strict';

var module = angular.module('xe.directives', []);


module.directive('uiEnter', function() {
  return function($scope, element, attrs) {
    element.bind("keydown keypress", function(e) {
      if(e.which === 13) {
        $scope.$apply(function(){
          $scope.$eval(attrs.uiEnter);
        });
        e.preventDefault();
      }
    });
  };
});
