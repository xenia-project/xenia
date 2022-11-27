/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstdlib>
#include <memory>

#include "xenia/base/console.h"
#include "xenia/base/cvar.h"
#include "xenia/base/main_win.h"
#include "xenia/base/platform_win.h"
#include "xenia/ui/windowed_app.h"
#include "xenia/ui/windowed_app_context_win.h"

DEFINE_bool(enable_console, false, "Open a console window with the main window",
            "General");
#if XE_ARCH_AMD64 == 1
DEFINE_bool(enable_rdrand_ntdll_patch, true,
            "Hot-patches ntdll at the start of the process to not use rdrand "
            "as part of the RNG for heap randomization. Can reduce CPU usage "
            "significantly, but is untested on all Windows versions.",
            "Win32");
// begin ntdll hack
#include <psapi.h>
static bool g_didfailtowrite = false;
static void write_process_memory(HANDLE process, uintptr_t offset,
                                 unsigned size, const unsigned char* bvals) {
  if (!WriteProcessMemory(process, (void*)offset, bvals, size, nullptr)) {
    if (!g_didfailtowrite) {
      MessageBoxA(nullptr, "Failed to write to process!", "Failed", MB_OK);
      g_didfailtowrite = true;
    }
  }
}

static const unsigned char pattern_cmp_processorfeature_28_[] = {
    0x80, 0x3C, 0x25, 0x90,
    0x02, 0xFE, 0x7F, 0x00};  // cmp     byte ptr ds:7FFE0290h, 0
static const unsigned char pattern_replacement[] = {
    0x48, 0x39, 0xe4,             // cmp rsp, rsp = always Z
    0x0F, 0x1F, 0x44, 0x00, 0x00  // 5byte nop
};
static void patch_ntdll_instance(HANDLE process, uintptr_t ntdll_base) {
  MODULEINFO modinfo;

  GetModuleInformation(process, (HMODULE)ntdll_base, &modinfo,
                       sizeof(MODULEINFO));

  std::vector<uintptr_t> possible_places{};

  unsigned char* strt = (unsigned char*)modinfo.lpBaseOfDll;

  for (unsigned i = 0; i < modinfo.SizeOfImage; ++i) {
    for (unsigned j = 0; j < sizeof(pattern_cmp_processorfeature_28_); ++j) {
      if (strt[i + j] != pattern_cmp_processorfeature_28_[j]) {
        goto miss;
      }
    }
    possible_places.push_back((uintptr_t)(&strt[i]));
  miss:;
  }

  for (auto&& place : possible_places) {
    write_process_memory(process, place, sizeof(pattern_replacement),
                         pattern_replacement);
  }
}

static void do_ntdll_hack_this_process() {
  patch_ntdll_instance(GetCurrentProcess(),
                       (uintptr_t)GetModuleHandleA("ntdll.dll"));
}
#endif
// end ntdll hack
LONG _UnhandledExceptionFilter(_EXCEPTION_POINTERS* ExceptionInfo) {
  PVOID exception_addr = ExceptionInfo->ExceptionRecord->ExceptionAddress;

  DWORD64 last_stackpointer = ExceptionInfo->ContextRecord->Rsp;

  DWORD64 last_rip = ExceptionInfo->ContextRecord->Rip;

  DWORD except_code = ExceptionInfo->ExceptionRecord->ExceptionCode;

  DWORD last_error = GetLastError();

  NTSTATUS stat = __readgsdword(0x1250);

  int last_errno_value = errno;



  char except_message_buf[1024];

  sprintf_s(except_message_buf,
            "Exception encountered!\nException address: %p\nStackpointer: "
            "%p\nInstruction pointer: %p\nExceptionCode: 0x%X\nLast Win32 "
            "Error: 0x%X\nLast NTSTATUS: 0x%X\nLast errno value: 0x%X\n",
            exception_addr, (void*)last_stackpointer, (void*)last_rip, except_code,
            last_error, stat, last_errno_value);
  MessageBoxA(nullptr, except_message_buf, "Unhandled Exception", MB_ICONERROR);
  return EXCEPTION_CONTINUE_SEARCH;
}
int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE hinstance_prev,
                    LPWSTR command_line, int show_cmd) {
  int result;
  SetUnhandledExceptionFilter(_UnhandledExceptionFilter);
  {
    xe::ui::Win32WindowedAppContext app_context(hinstance, show_cmd);
    // TODO(Triang3l): Initialize creates a window. Set DPI awareness via the
    // manifest.
    if (!app_context.Initialize()) {
      return EXIT_FAILURE;
    }

    std::unique_ptr<xe::ui::WindowedApp> app =
        xe::ui::GetWindowedAppCreator()(app_context);

    if (!xe::ParseWin32LaunchArguments(false, app->GetPositionalOptionsUsage(),
                                       app->GetPositionalOptions(), nullptr)) {
      return EXIT_FAILURE;
    }

    // Initialize COM on the UI thread with the apartment-threaded concurrency
    // model, so dialogs can be used.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
      return EXIT_FAILURE;
    }

    xe::InitializeWin32App(app->GetName());

    if (app->OnInitialize()) {
#if XE_ARCH_AMD64 == 1
      if (cvars::enable_rdrand_ntdll_patch) {
        do_ntdll_hack_this_process();
      }
#endif
      // TODO(Triang3l): Rework this, need to initialize the console properly,
      // disable has_console_attached_ by default in windowed apps, and attach
      // only if needed.
      if (cvars::enable_console) {
        xe::AttachConsole();
      }
      result = app_context.RunMainMessageLoop();
    } else {
      result = EXIT_FAILURE;
    }

    app->InvokeOnDestroy();
  }

  // Logging may still be needed in the destructors.
  xe::ShutdownWin32App();

  CoUninitialize();

  return result;
}
