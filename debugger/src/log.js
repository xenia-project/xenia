/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

'use strict';

var module = angular.module('xe.log', []);


module.service('log', function($rootScope) {
  var Log = function() {
    this.lines = [];

    this.progressActive = false;
    this.progress = 0;
  };

  Log.prototype.appendLine = function(line) {
    this.lines.push(line);
  };

  Log.prototype.info = function(line) {
    this.appendLine('I ' + line);
  };

  Log.prototype.error = function(line) {
    this.appendLine('E ' + line);
  };

  Log.prototype.setProgress = function(value) {
    this.progressActive = true;
    this.progress = value;
  };

  Log.prototype.clearProgress = function() {
    this.progressActive = false;
  };

  return new Log();
});
