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
    lineNumbers: false,
    gutters: [
      'debugger-fnview-gutter-icon',
      'debugger-fnview-gutter-addr',
      'debugger-fnview-gutter-code'
    ],
    readOnly: true
  });

  function hex32(number) {
    var str = "" + number.toString(16).toUpperCase();
    while (str.length < 8) str = "0" + str;
    return str;
  };

  function updateSourceCode(fn) {
    var cm = $scope.codeMirror;
    if (!fn) {
      $scope.sourceLines = [];
      cm.setValue('');
      return;
    }

    var doc = cm.getDoc();

    var lines = fn.disasm.source.lines;
    $scope.sourceLines = lines;

    var textContent = [];
    for (var n = 0; n < lines.length; n++) {
      var line = lines[n];
      textContent.push(line[3]);
    }
    cm.setValue(textContent.join('\n'));

    for (var n = 0; n < lines.length; n++) {
      var line = lines[n];

      var el = document.createElement('div');
      el.classList.add('debugger-fnview-gutter-addr-el');
      el.innerText = hex32(line[1]);
      cm.setGutterMarker(n, 'debugger-fnview-gutter-addr', el);
      if (line[0] != 'i') {
        el.classList.add('debugger-fnview-gutter-addr-el-inactive');
      }

      if (line[0] == 'i') {
        el = document.createElement('div');
        el.classList.add('debugger-fnview-gutter-code-el');
        el.innerText = hex32(line[2]);
        cm.setGutterMarker(n, 'debugger-fnview-gutter-code', el);
      }
    }
  };
  function updateCode() {
    var cm = $scope.codeMirror;
    var fn = $scope.fn || null;
    var codeType = $scope.codeType;

    var gutters;
    switch (codeType) {
    case 'source':
      gutters = [
        'debugger-fnview-gutter-icon',
        'debugger-fnview-gutter-addr',
        'debugger-fnview-gutter-code'
      ];
      break;
    default:
      gutters = [
        'debugger-fnview-gutter-icon',
        'debugger-fnview-gutter-addr'
      ];
      break;
    }
    cm.setOption('gutters', gutters);

    cm.clearGutter('debugger-fnview-gutter-icon');
    cm.clearGutter('debugger-fnview-gutter-addr');
    cm.clearGutter('debugger-fnview-gutter-code');

    // Set last to make all option changes stick.
    switch (codeType) {
    case 'source':
      cm.operation(function() {
        updateSourceCode(fn);
      });
      break;
    default:
      var value = fn ? fn.disasm[codeType] : null;
      cm.setValue(value || '');
      break;
    }
  };
  $scope.$watch('codeType', updateCode);

  $scope.codeMirror.on('gutterClick', function(
      instance, line, gutterClass, e) {
    if (e.which == 1) {
      if (gutterClass == 'debugger-fnview-gutter-icon') {
        console.log('click', e);
      }
    }
  });
//  $scope.codeMirror.on('gutterContextMenu', function(
//      instance, line, gutterClass, e) {
//    console.log('context menu');
//    e.preventDefault();
//  });
//  $scope.codeMirror.on('contextmenu', function(
//      instance, e) {
//    console.log('context menu main');
//    e.preventDefault();
//  });
});
