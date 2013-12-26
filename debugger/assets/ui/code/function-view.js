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
    $rootScope, $scope, $location, app, log, Breakpoint) {
  $scope.codeType = 'source';
  $scope.highlightInfo = null;

  function refresh() {
    if (!app.session) {
      $scope.fn = null;
      return;
    }
    app.session.state.fetchFunction($scope.functionAddress).then(function(fn) {
      $scope.fn = fn;
      updateCode();
    }, function(e) {
      log.error('Unable to fetch function.');
    });
  };
  $rootScope.$on('refresh', refresh);
  $scope.$watch('functionAddress', refresh);

  function updateHighlight(address) {
    if (!$scope.sourceLines || $scope.codeType != 'source') {
      return;
    }
    if ($scope.highlightInfo) {
      if ($scope.highlightInfo.address == address) {
        return;
      }
      var oldLine = $scope.highlightInfo.line;
      if ($scope.highlightInfo.widget) {
        $scope.highlightInfo.widget.clear();
      }
      $scope.highlightInfo = null;
      updateLine(oldLine);
    }
    // TODO(benvanik): a better mapping.
    var line = -1;
    for (var n = 0; n < $scope.sourceLines.length; n++) {
      var sourceLine = $scope.sourceLines[n];
      if (sourceLine[0] == 'i' &&
          sourceLine[1] == address) {
        line = n;
        break;
      }
    }
    if (line != -1) {
      $scope.highlightInfo = {
        address: address,
        line: line,
        widget: null
      };
      updateLine(line);
    }
  };
  $scope.$watch(function() {
    return $location.search();
  }, function(search) {
    updateHighlight(parseInt(search.a, 16));
  });

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

        updateLine(n);
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

  function updateLine(line) {
    var sourceLine = $scope.sourceLines[line];
    var cm = $scope.codeMirror;
    if (sourceLine[0] != 'i') {
      return;
    }
    var address = sourceLine[1];
    var breakpoint = app.session.breakpoints[address];
    var el;
    if (breakpoint && breakpoint.type == 'code') {
      el = document.createElement('span');
      el.classList.add('debugger-fnview-gutter-icon-el');
      if (breakpoint.enabled) {
        el.innerHTML = '&#9679;';
      } else {
        el.innerHTML = '&#9676;';
      }
    } else {
      el = null;
    }
    cm.setGutterMarker(line, 'debugger-fnview-gutter-icon', el);

    var highlightInfo = $scope.highlightInfo;
    if (highlightInfo && highlightInfo.line == line) {
      /*
      if (!highlightInfo.widget) {
        el = document.createElement('div');
        el.style.width = '100%';
        el.style.height = '20px';
        el.style.backgroundColor = 'red';
        el.innerHTML = 'hi!';
        highlightInfo.widget = cm.addLineWidget(line, el, {
          coverGutter: false
        });
        cm.scrollIntoView(line, 50);
      }
      */
      cm.addLineClass(line, 'background', 'debugger-fnview-line-highlight-bg');
    } else {
      cm.removeLineClass(line, 'background');
    }
  };

  function toggleBreakpoint(line, sourceLine, shiftKey) {
    var address = sourceLine[1];
    var breakpoint = app.session.breakpoints[address];
    if (breakpoint) {
      // Existing breakpoint - toggle or remove.
      if (shiftKey || !breakpoint.enabled) {
        app.session.toggleBreakpoint(breakpoint, !breakpoint.enabled);
      } else {
        app.session.removeBreakpoint(breakpoint);
      }
    } else {
      // New breakpoint needed.
      breakpoint = app.session.addCodeBreakpoint(
          $scope.functionAddress, address);
    }

    updateLine(line);
  };

  $scope.codeMirror.on('gutterClick', function(
      instance, line, gutterClass, e) {
    if (e.which == 1) {
      if (gutterClass == 'debugger-fnview-gutter-icon' ||
          gutterClass == 'debugger-fnview-gutter-addr') {
        var sourceLine = $scope.sourceLines[line];
        if (!sourceLine || sourceLine[0] != 'i') {
          return;
        }
        e.preventDefault();
        toggleBreakpoint(line, sourceLine, e.shiftKey);
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
