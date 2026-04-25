/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_I18N_H_
#define XENIA_UI_I18N_H_

namespace xe {
namespace ui {

enum class TextId {
  kPostProcessing,
  kAllEffectsAnyGpu,
  kAntiAliasing,
  kNone,
  kFxaaNormal,
  kFxaaExtreme,
  kResamplingAndSharpening,
  kNoneBilinear,
  kCas,
  kFsr,
  kBilinearDescription,
  kCasDescription,
  kFsrDescription,
  kFxaaRecommended,
  kFsrSharpnessReduction,
  kStops,
  kResetFsrSharpnessReduction,
  kCasAdditionalSharpnessFsr,
  kCasAdditionalSharpness,
  kResetCasAdditionalSharpness,
  kDithering,
  kDitherFinalOutput,
  kMenuFile,
  kMenuOpen,
  kMenuClose,
  kMenuShowContentDirectory,
  kMenuExit,
  kMenuCpu,
  kMenuResetTimeScalar,
  kMenuTimeScalarHalf,
  kMenuTimeScalarDouble,
  kMenuToggleProfilerDisplay,
  kMenuPauseResumeProfiler,
  kMenuBreakAndShowGuestDebugger,
  kMenuBreakIntoHostDebugger,
  kMenuGpu,
  kMenuTraceFrame,
  kMenuClearRuntimeCaches,
  kMenuDisplay,
  kMenuPostProcessingSettings,
  kMenuFullscreen,
  kMenuHelp,
  kMenuFaq,
  kMenuGameCompatibility,
  kMenuBuildCommit,
  kMenuRecentChanges,
  kMenuAbout,
  kSelectContentPackage,
  kSupportedFiles,
  kDiscImage,
  kXboxExecutable,
  kAllFiles,
  kXeniaDebugger,
  kDebugFlagRequired,
  kCount,
};

const char* T(TextId id);

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_I18N_H_
