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
  $scope.codeType = 'source';

  function refresh() {
    if (!app.session || !app.session.dataSource) {
      $scope.fn = null;
      return;
    }
    var dataSource = app.session.dataSource;

    dataSource.getFunction($scope.functionAddress).then(function(fn) {
      $scope.fn = fn;
      updateCode();
    }, function(e) {
      log.error('Unable to fetch function');
    });
  };
  $rootScope.$on('refresh', refresh);
  $scope.$watch('functionAddress', refresh);

  var textArea = document.querySelector('.debugger-fnview-textarea');
  $scope.codeMirror = CodeMirror.fromTextArea(textArea, {
    mode: 'javascript',
    theme: 'default',
    indentUnit: 2,
    tabSize: 2,
    lineNumbers: true,
    firstLineNumber: 0,
    lineNumberFormatter: function(line) {
      return String(line);
    },
    gutters: [],
    readOnly: true
  });

  function updateCode() {
    var codeType = $scope.codeType;
    var value = '';
    if ($scope.fn) {
      value = $scope.fn.disasm[codeType];
    }
    $scope.codeMirror.setValue(value || '');
  };
  $scope.$watch('codeType', updateCode);
});
