/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/run_loop.h>

#include <xenia/core/mutex.h>
#include <xenia/core/thread.h>


typedef struct xe_run_loop {
  xe_ref_t ref;
} xe_run_loop_t;


#define WM_XE_RUN_LOOP_QUIT   (WM_APP + 0x100)
#define WM_XE_RUN_LOOP_CALL   (WM_APP + 0x101)

typedef struct xe_run_loop_call {
  xe_run_loop_callback  callback;
  void*                 data;
} xe_run_loop_call_t;


xe_run_loop_ref xe_run_loop_create() {
  xe_run_loop_ref run_loop = (xe_run_loop_ref)xe_calloc(sizeof(xe_run_loop_t));
  xe_ref_init((xe_ref)run_loop);
  return run_loop;
}

void xe_run_loop_dealloc(xe_run_loop_ref run_loop) {
}

xe_run_loop_ref xe_run_loop_retain(xe_run_loop_ref run_loop) {
  xe_ref_retain((xe_ref)run_loop);
  return run_loop;
}

void xe_run_loop_release(xe_run_loop_ref run_loop) {
  xe_ref_release((xe_ref)run_loop, (xe_ref_dealloc_t)xe_run_loop_dealloc);
}

int xe_run_loop_pump(xe_run_loop_ref run_loop) {
  MSG msg;
  if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
    switch (msg.message) {
    case WM_XE_RUN_LOOP_CALL:
      if (msg.wParam == (WPARAM)run_loop) {
        xe_run_loop_call_t* call = (xe_run_loop_call_t*)msg.lParam;
        call->callback(call->data);
        xe_free(call);
      }
      break;
    case WM_XE_RUN_LOOP_QUIT:
      if (msg.wParam == (WPARAM)run_loop) {
        // Done!
        return 1;
      }
      break;
    }
  }
  return 0;
}

void xe_run_loop_quit(xe_run_loop_ref run_loop) {
  PostMessage(NULL, WM_XE_RUN_LOOP_QUIT, (WPARAM)run_loop, 0);
}

void xe_run_loop_call(xe_run_loop_ref run_loop,
                      xe_run_loop_callback callback, void* data) {
  xe_run_loop_call_t* call =
      (xe_run_loop_call_t*)xe_calloc(sizeof(xe_run_loop_call_t));
  call->callback  = callback;
  call->data      = data;
  PostMessage(NULL, WM_XE_RUN_LOOP_CALL, (WPARAM)run_loop, (LPARAM)call);
}
