/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// TODO(benvanik): closure or something fancy


var DebugClient = function(endpoint) {
  /**
   * Target websocket endpoint.
   * @type {string}
   * @private
   */
  this.endpoint_ = endpoint;

  /**
   * Connected socket.
   * @type {!WebSocket}
   * @private
   */
  this.socket_ = new WebSocket(endpoint, []);

  this.socket_.onopen = (function() {
    console.log('opened');
  }).bind(this);

  this.socket_.onerror = (function(e) {
    console.log('error', e);
  }).bind(this);

  this.socket_.onmessage = (function(e) {
    console.log('message', e.data);
  }).bind(this);
}


var client = new DebugClient('ws://127.0.0.1:6200');


var myTextArea = document.querySelector('.debugger-fnview-textarea');
var myCodeMirror = CodeMirror.fromTextArea(myTextArea, {
  mode: 'javascript',
  theme: 'default',
  indentUnit: 2,
  tabSize: 2,

  lineNumbers: true,
  firstLineNumber: 0,
  lineNumberFormatter: function(line) {
    return String('0x00000000' + line);
  },
  gutters: [],

  //readOnly: true,
});
