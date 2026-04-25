/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/i18n.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string>

#include "xenia/base/cvar.h"

DEFINE_string(ui_language, "en",
              "Language for the emulator user interface. Use: [en, sv].",
              "General");

namespace xe {
namespace ui {

namespace {

struct LocalizedText {
  const char* en;
  const char* sv;
};

constexpr std::array<LocalizedText, size_t(TextId::kCount)> kLocalizedText = {{
    {"Post-processing", "Efterbehandling"},
    {"All effects can be used on GPUs of any brand.",
     "Alla effekter kan användas på grafikkort från alla tillverkare."},
    {"Anti-aliasing", "Kantutjämning"},
    {"None", "Ingen"},
    {"NVIDIA Fast Approximate Anti-Aliasing 3.11 (FXAA), normal quality",
     "NVIDIA Fast Approximate Anti-Aliasing 3.11 (FXAA), normal kvalitet"},
    {"NVIDIA Fast Approximate Anti-Aliasing 3.11 (FXAA), extreme quality",
     "NVIDIA Fast Approximate Anti-Aliasing 3.11 (FXAA), extrem kvalitet"},
    {"Resampling and sharpening", "Omsampling och skärpa"},
    {"None / bilinear", "Ingen/bilinjär"},
    {"AMD FidelityFX Contrast Adaptive Sharpening (CAS)",
     "AMD FidelityFX Contrast Adaptive Sharpening (CAS)"},
    {"AMD FidelityFX Super Resolution 1.0 (FSR)",
     "AMD FidelityFX Super Resolution 1.0 (FSR)"},
    {"Simple bilinear filtering is done if resampling is needed.\n"
     "Otherwise, only anti-aliasing is done if enabled, or displaying as is.",
     "Enkel bilinjär filtrering görs om omsampling behövs.\n"
     "Annars görs bara kantutjämning om det är aktiverat, eller så visas "
     "bilden som den är."},
    {"Sharpening and resampling to up to 2x2 to improve the fidelity of "
     "details.\n"
     "For scaling by more than 2x2, bilinear stretching is done afterwards.",
     "Skärpa och omsampling upp till 2x2 används för att förbättra "
     "detaljåtergivningen.\n"
     "Vid skalning över 2x2 görs bilinjär utsträckning efteråt."},
    {"High-quality edge-preserving upscaling to arbitrary target resolutions.\n"
     "For scaling by more than 2x2, multiple upsampling passes are done.\n"
     "If not upscaling, Contrast Adaptive Sharpening (CAS) is used instead.",
     "Högkvalitativ kantbevarande uppskalning till valfri målupplösning.\n"
     "Vid skalning över 2x2 görs flera uppsamplingspass.\n"
     "Om bilden inte skalas upp används Contrast Adaptive Sharpening (CAS) "
     "i stället."},
    {"FXAA is highly recommended when using CAS or FSR.",
     "FXAA rekommenderas starkt när CAS eller FSR används."},
    {"FSR sharpness reduction when upscaling (lower is sharper):",
     "FSR-skärpereduktion vid uppskalning (lägre är skarpare):"},
    {"{:.3f} stops", "{:.3f} steg"},
    {"Reset##ResetFSRSharpnessReduction",
     "Återställ##ResetFSRSharpnessReduction"},
    {"CAS additional sharpness when not upscaling (higher is sharper):",
     "Ytterligare CAS-skärpa när bilden inte skalas upp (högre är skarpare):"},
    {"CAS additional sharpness (higher is sharper):",
     "Ytterligare CAS-skärpa (högre är skarpare):"},
    {"Reset##ResetCASAdditionalSharpness",
     "Återställ##ResetCASAdditionalSharpness"},
    {"Dithering", "Gitter"},
    {"Dither the final output to 8bpc to make gradients smoother",
     "Gitterbehandla slutbilden till 8 bpc för jämnare övertoningar"},
    {"&File", "&Arkiv"},
    {"&Open...", "&Öppna..."},
    {"Close", "Stäng"},
    {"Show content directory...", "Visa innehållskatalog..."},
    {"E&xit", "A&vsluta"},
    {"&CPU", "&CPU"},
    {"&Reset Time Scalar", "&Återställ tidsskalning"},
    {"Time Scalar /= 2", "Tidsskalning /= 2"},
    {"Time Scalar *= 2", "Tidsskalning *= 2"},
    {"Toggle Profiler &Display", "Växla profilerings&visning"},
    {"&Pause/Resume Profiler", "&Pausa/återuppta profileraren"},
    {"&Break and Show Guest Debugger", "&Bryt och visa gästfelsökaren"},
    {"&Break into Host Debugger", "B&ryt in i värdfelsökaren"},
    {"&GPU", "&GPU"},
    {"&Trace Frame", "&Spåra bildruta"},
    {"&Clear Runtime Caches", "Rensa &körningscache"},
    {"&Display", "&Visning"},
    {"&Post-processing settings", "Inställningar för &efterbehandling"},
    {"&Fullscreen", "&Helskärm"},
    {"&Help", "&Hjälp"},
    {"FA&Q...", "FA&Q..."},
    {"Game &compatibility...", "Spel&kompatibilitet..."},
    {"Build commit on GitHub...", "Byggversionens commit på GitHub..."},
    {"Recent changes on GitHub...", "Senaste ändringarna på GitHub..."},
    {"&About...", "&Om..."},
    {"Select Content Package", "Välj innehållspaket"},
    {"Supported Files", "Filer som stöds"},
    {"Disc Image (*.iso)", "Skivavbild (*.iso)"},
    {"Xbox Executable (*.xex)", "Xbox-körbar fil (*.xex)"},
    {"All Files (*.*)", "Alla filer (*.*)"},
    {"Xenia Debugger", "Xenias felsökare"},
    {"Xenia must be launched with the --debug flag in order to enable "
     "debugging.",
     "Xenia måste startas med flaggan --debug för att aktivera felsökning."},
}};

std::string NormalizeLanguage(std::string language) {
  std::transform(language.begin(), language.end(), language.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  std::replace(language.begin(), language.end(), '-', '_');
  return language;
}

bool IsSwedish() {
  std::string language = NormalizeLanguage(cvars::ui_language);
  return language == "sv" || language == "sv_se" || language == "sv_fi" ||
         language == "swe" || language == "swedish";
}

}  // namespace

const char* T(TextId id) {
  size_t index = size_t(id);
  if (index >= kLocalizedText.size()) {
    return "";
  }
  const LocalizedText& text = kLocalizedText[index];
  return IsSwedish() ? text.sv : text.en;
}

}  // namespace ui
}  // namespace xe
