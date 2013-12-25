/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

'use strict';

var module = angular.module('xe.directives', [
  'ui.router'
]);


module.directive('uiEnter', function() {
  return function($scope, element, attrs) {
    element.bind('keydown keypress', function(e) {
      if(e.which === 13) {
        $scope.$apply(function(){
          $scope.$eval(attrs.uiEnter);
        });
        e.preventDefault();
      }
    });
  };
});

module.directive('uiEscape', function() {
  return function($scope, element, attrs) {
    element.bind('keydown keypress', function(e) {
      if(e.which === 27) {
        $scope.$apply(function(){
          $scope.$eval(attrs.uiEscape);
        });
        e.preventDefault();
      }
    });
  };
});

module.directive('uiScrollDownOn', function() {
  return {
    priority: 1,
    link: function($scope, element, attrs) {
      $scope.$watch(attrs.uiScrollDownOn, function() {
        element[0].scrollTop = element[0].scrollHeight;
      });
    }
  };
});

module.directive('xeCoderef', function($state) {
  return {
    priority: 1,
    link: function($scope, element, attrs) {
      var target = attrs.xeCoderef;
      var stateName = 'session.code.function';
      var stateParams = {
        function: target,
        a: null
      };
      element.attr('href', $state.href(stateName, stateParams));
      element.bind('click', function(e) {
        e.preventDefault();
        $state.go(stateName, stateParams);
      });
    }
  };
});

module.directive('xeMemref', function($state) {
  return {
    priority: 1,
    link: function($scope, element, attrs) {
      var target = attrs.xeMemref;
      var stateName = 'session.memory';
      var stateParams = {
        a: target
      };
      element.attr('href', $state.href(stateName, stateParams));
      element.bind('click', function(e) {
        e.preventDefault();
        $state.go(stateName, stateParams);
      });
    }
  };
});
