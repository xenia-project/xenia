/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

'use strict';

var module = angular.module('xe.filters', []);


module.filter('hex16', function() {
  return function(number) {
    if (number !== null && number !== undefined) {
      var str = '' + number.toString(16).toUpperCase();
      while (str.length < 4) str = '0' + str;
      return str;
    }
  };
});

module.filter('hex32', function() {
  return function(number) {
    if (number !== null && number !== undefined) {
      var str = '' + number.toString(16).toUpperCase();
      while (str.length < 8) str = '0' + str;
      return str;
    }
  };
});

module.filter('exp8', function() {
  return function(number) {
    if (number !== null && number !== undefined) {
      var str = number.toExponential(8);
      if (number >= 0) {
        str = ' ' + str;
      }
      return str;
    }
  }
});
