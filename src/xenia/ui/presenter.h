/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_PRESENTER_H_
#define XENIA_UI_PRESENTER_H_

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/cvar.h"
#include "xenia/base/math.h"
#include "xenia/base/platform.h"
#include "xenia/ui/surface.h"
#include "xenia/ui/ui_drawer.h"

#if XE_PLATFORM_WIN32
// Must be included before DXGI for things like NOMINMAX, and also needed for
// Windows handle types.
#include "xenia/base/platform_win.h"

#include <dxgi.h>
#include <wrl/client.h>
#endif  // XE_PLATFORM

// For implementation use.
DECLARE_bool(present_render_pass_clear);

namespace xe {
namespace ui {

class Presenter;
class Window;
class Win32Window;

class UIDrawContext {
 public:
  UIDrawContext(const UIDrawContext& context) = delete;
  UIDrawContext& operator=(const UIDrawContext& context) = delete;
  virtual ~UIDrawContext() = default;

  Presenter& presenter() const { return presenter_; }

  // It's assumed that the render target size will be either equal to the size
  // of the surface, or the render target will be stretched to cover the entire
  // surface (not in the corner of the surface).
  uint32_t render_target_width() const { return render_target_width_; }
  uint32_t render_target_height() const { return render_target_height_; }

 protected:
  explicit UIDrawContext(Presenter& presenter, uint32_t render_target_width,
                         uint32_t render_target_height)
      : presenter_(presenter),
        render_target_width_(render_target_width),
        render_target_height_(render_target_height) {}

 private:
  Presenter& presenter_;
  uint32_t render_target_width_;
  uint32_t render_target_height_;
};

struct RawImage {
  uint32_t width = 0;
  uint32_t height = 0;
  size_t stride = 0;
  // R8 G8 B8 X8. The last row is not required to be padded to the stride.
  std::vector<uint8_t> data;
};

// The presenter displays up to two layers of content on a host surface:
// - Guest output image, focusing on lowering latency and maintaining stable
//   frame pacing, with various scaling and sharpening methods and letterboxing;
// - Xenia's internal UI (such as the profiler and Dear ImGui).
//
// The guest output image may be refreshed from any thread generating it
// (usually the GPU emulation thread), as long as there are no multiple threads
// doing that simultaneously (since that would functionally be a race condition
// even if refreshing is performed in a critical section).
//
// The UI overlays are managed entirely by the UI thread.
//
// Painting on the host surface may occur in two places:
// - If there are no UI overlays, painting of the guest output may be performed
//   immediately from the thread refreshing it, to bypass the OS scheduling and
//   event handling. This is especially important on platforms where the native
//   surface paint event has a frame rate limit (such as the display refresh
//   rate), and the limit may differ greatly from the guest frame rate (such as
//   presenting a 30 or 60 FPS guest to a 144 Hz host surface).
// - If the UI overlays (owned by the UI thread) are present, painting of both
//   the guest output (is available) and the UI is done exclusively from the
//   platform paint event handler. The guest output without UI overlays may also
//   be painted from the platform paint callback in certain cases, such as when
//   an additional paint beyond the guest's frame rate may be needed (like when
//   resizing the window), or when painting from the thread refreshing the guest
//   output is undesirable (for instance, if it will result in waiting for host
//   vertical sync in that thread too early if host vertical sync can't be
//   disabled on the platform, blocking the next frame of GPU emulation).
//
// The composition of the guest and the UI is done by Xenia manually, as opposed
// to using platform functionality such as DirectComposition, in order to have
// more predictability of GPU queue scheduling, but primarily to be able to take
// advantage of independent host presentation where it's available, so variable
// refresh rate may be used where possible, and latency may be significantly
// reduced. Also, at least on some configurations (checked on Windows 11 21H2 on
// Nvidia GeForce GTX 1070 with driver version 472.12), when in borderless
// fullscreen, any composition causes the DXGI Present to wait for vertical sync
// on the GPU even if the sync interval 0 is specified.
//
// An intermediate image with the size requested by the guest is used for guest
// output in all cases. Even though it adds some GPU overhead, especially in the
// 1:1 size case, using it solves multiple issues:
// - Presentation may be done more often than by the guest.
// - There is clear separation between pre-scaling and mid- / post-scaling
//   operations. The gamma ramp, for instance, may be applied before scaling,
//   with one lookup per pixel rather than four with fetch4.
// - A simpler compute shader may be used instead of setting up the whole
//   graphics pipeline for copying in the GPU command processor in all cases,
//   while Direct3D 12 does not allow UAVs for swap chain buffers.
//
// The presenter limits the frame rate of the UI overlay (when possible) to a
// value that's ideally the refresh rate of the monitor containing the window if
// the platform's paint event doesn't have an internal limiter. However, where
// possible, the arrival of a new guest output image will interrupt the UI tick
// wait.
//
// Because the UI overlays preclude the possibility of presenting directly from
// the thread refreshing the guest output, and on some platforms, result in the
// frame rate limiting of paint events manifesting itself, there must be no
// persistent UI overlays that haven't been explicitly requested by the user.
// However, for temporary (primarily non-modal) UI elements such as various
// timed notifications, using the Presenter should be preferred to implementing
// them via overlaying native windows on top of the presentation surface on
// platforms where the concept of independent presentation exists, as multiple
// windows will result in native composition disabling it.
//
// The painting connection between the Presenter and the Surface can be managed
// only by the UI thread. However, the thread refreshing the guest output may
// still mark the current connection as outdated and ask the UI thread (by
// requesting painting) to try to recover - but the guest output refresh thread
// must not try to reconnect by itself, as methods of the Surface are available
// only to the UI thread.
class Presenter {
 public:
  // May be actually called on the UI thread even if statically_from_ui_thread
  // is false, such as when the guest output is refreshed by the UI thread.
  using HostGpuLossCallback =
      std::function<void(bool is_responsible, bool statically_from_ui_thread)>;
  static void FatalErrorHostGpuLossCallback(bool is_responsible,
                                            bool statically_from_ui_thread);

  class GuestOutputRefreshContext {
   public:
    GuestOutputRefreshContext(const GuestOutputRefreshContext& context) =
        delete;
    GuestOutputRefreshContext& operator=(
        const GuestOutputRefreshContext& context) = delete;
    virtual ~GuestOutputRefreshContext() = default;

    // Sets whether the source actually has no more than 8 bits of precision
    // (though the image provided by the refresher may still have a higher
    // storage precision). If never called, assuming it's false.
    void SetIs8bpc(bool is_8bpc) { is_8bpc_out_ref_ = is_8bpc; }

   protected:
    GuestOutputRefreshContext(bool& is_8bpc_out_ref)
        : is_8bpc_out_ref_(is_8bpc_out_ref) {
      is_8bpc_out_ref = false;
    }

   private:
    bool& is_8bpc_out_ref_;
  };

  class GuestOutputPaintConfig {
   public:
    enum class Effect {
      kBilinear,
      kCas,
      // AMD FidelityFX Super Resolution upsampling, Contrast Adaptive
      // Sharpening otherwise.
      kFsr,
    };

    // This value is used as a lerp factor.
    static constexpr float kCasAdditionalSharpnessMin = 0.0f;
    static constexpr float kCasAdditionalSharpnessMax = 1.0f;
    static constexpr float kCasAdditionalSharpnessDefault = 0.0f;
    static_assert(kCasAdditionalSharpnessDefault >=
                      kCasAdditionalSharpnessMin &&
                  kCasAdditionalSharpnessDefault <= kCasAdditionalSharpnessMax);

    // EASU (as well as CAS) is designed for scaling by factors of up to 2x2.
    // Some sensible limit for unusual cases, when the game for some reason
    // presents a very small back buffer.
    // This is enough for 480p > 960p > 1920p > 3840p > 7680p (bigger than 8K,
    // or 4320p).
    static constexpr uint32_t kFsrMaxUpscalingPassesMax = 4;

    static constexpr float kFsrSharpnessReductionMin = 0.0f;
    // "Values above 2.0 won't make a visible difference."
    // https://raw.githubusercontent.com/GPUOpen-Effects/FidelityFX-FSR/master/docs/FidelityFX-FSR-Overview-Integration.pdf
    static constexpr float kFsrSharpnessReductionMax = 2.0f;
    static constexpr float kFsrSharpnessReductionDefault = 0.2f;
    static_assert(kFsrSharpnessReductionDefault >= kFsrSharpnessReductionMin &&
                  kFsrSharpnessReductionDefault <= kFsrSharpnessReductionMax);

    // In the sharpness setters, min / max with a constant as the first argument
    // also drops NaNs.

    bool GetAllowOverscanCutoff() const { return allow_overscan_cutoff_; }
    void SetAllowOverscanCutoff(bool new_allow_overscan_cutoff) {
      allow_overscan_cutoff_ = new_allow_overscan_cutoff;
    }

    Effect GetEffect() const { return effect_; }
    void SetEffect(Effect new_effect) { effect_ = new_effect; }

    float GetCasAdditionalSharpness() const {
      return cas_additional_sharpness_;
    }
    void SetCasAdditionalSharpness(float new_cas_additional_sharpness) {
      cas_additional_sharpness_ = std::min(
          kCasAdditionalSharpnessMax,
          std::max(kCasAdditionalSharpnessMin, new_cas_additional_sharpness));
    }

    uint32_t GetFsrMaxUpsamplingPasses() const {
      return fsr_max_upsampling_passes_;
    }
    void SetFsrMaxUpsamplingPasses(uint32_t new_fsr_max_upsampling_passes) {
      fsr_max_upsampling_passes_ =
          std::min(kFsrMaxUpscalingPassesMax,
                   std::max(uint32_t(1), new_fsr_max_upsampling_passes));
    }

    // In stops.
    float GetFsrSharpnessReduction() const { return fsr_sharpness_reduction_; }
    void SetFsrSharpnessReduction(float new_fsr_sharpness_reduction) {
      fsr_sharpness_reduction_ = std::min(
          kFsrSharpnessReductionMax,
          std::max(kFsrSharpnessReductionMin, new_fsr_sharpness_reduction));
    }

    // Very tiny effect, but highly noticeable, for instance, on the sky in the
    // 4D5307E6 main menu (prominently in Custom Games, especially with FSR -
    // banding around the clouds can be clearly seen without dithering with 8bpc
    // final host output).
    bool GetDither() const { return dither_; }
    void SetDither(bool new_dither) { dither_ = new_dither; }

   private:
    // Tools, rather than the emulator itself, must not allow overscan cutoff
    // and must use the kBilinear effect as the image must be as close to the
    // original front buffer as possible.
    bool allow_overscan_cutoff_ = false;
    Effect effect_ = Effect::kBilinear;
    float cas_additional_sharpness_ = kCasAdditionalSharpnessDefault;
    uint32_t fsr_max_upsampling_passes_ = kFsrMaxUpscalingPassesMax;
    float fsr_sharpness_reduction_ = kFsrSharpnessReductionDefault;
    bool dither_ = false;
  };

  Presenter(const Presenter& presenter) = delete;
  Presenter& operator=(const Presenter& presenter) = delete;
  virtual ~Presenter();

  virtual Surface::TypeFlags GetSupportedSurfaceTypes() const = 0;

  // For calling from the Window for the Presenter attached to it.
  // May be called from the destructor of the presenter through the window.
  void SetWindowSurfaceFromUIThread(Window* new_window, Surface* new_surface);
  void OnSurfaceMonitorUpdateFromUIThread(
      bool old_monitor_potentially_disconnected);
  void OnSurfaceResizeFromUIThread();

  // For calling from the platform paint event handler. Refreshes the surface
  // connection if needed, and also paints if possible and if needed (if there
  // are no UI overlays, and the guest output is presented directly from the
  // thread refreshing it, the paint may be skipped unless there has been an
  // explicit request previously or force_paint is true). If painting happens,
  // both the guest output and the UI overlays (if any are active) are drawn.
  // The background / letterbox of the painted context will be black - windows
  // should preferably have a black background before a Presenter is attached to
  // them too.
  void PaintFromUIThread(bool force_paint = false);

  // Pass 0 as width or height to disable guest output until the next refresh
  // with an actual size. The display aspect ratio may be specified like 16:9 or
  // like 1280:720, both are accepted, for simplicity, the guest display size
  // may just be passed. The callback will receive a backend-specific context,
  // and will not be called in case of an error such as the wrong size, or if
  // guest output is disabled. Returns whether the callback was called and it
  // returned true. The callback must submit all updating work to the host GPU
  // before successfully returning, and also signal all the GPU synchronization
  // primitives required by the GuestOutputRefreshContext implementation.
  bool RefreshGuestOutput(
      uint32_t frontbuffer_width, uint32_t frontbuffer_height,
      uint32_t display_aspect_ratio_x, uint32_t display_aspect_ratio_y,
      std::function<bool(GuestOutputRefreshContext& context)> refresher);
  // The implementation must be callable from any thread, including from
  // multiple at the same time, and it should acquire the latest guest output
  // image via ConsumeGuestOutput.
  virtual bool CaptureGuestOutput(RawImage& image_out) = 0;
  const GuestOutputPaintConfig& GetGuestOutputPaintConfigFromUIThread() const {
    return guest_output_paint_config_;
  }
  // For simplicity, may be called repeatedly even if no changes have been made.
  void SetGuestOutputPaintConfigFromUIThread(
      const GuestOutputPaintConfig& new_config);

  void AddUIDrawerFromUIThread(UIDrawer* drawer, size_t z_order);
  void RemoveUIDrawerFromUIThread(UIDrawer* drawer);

  // Requests (re)painting with the UI if there's UI to draw.
  void RequestUIPaintFromUIThread();

 protected:
  enum class PaintResult {
    kPresented,
    kPresentedSuboptimal,
    // Refused for internal reasons or a host API side failure, but still may
    // try to present without resetting the graphics provider in the future.
    kNotPresented,
    kNotPresentedConnectionOutdated,
    kGpuLostExternally,
    kGpuLostResponsible,
  };

  enum class SurfacePaintConnectResult {
    // Redrawing not necessary, nothing changed. Must not be returned for a new
    // connection (when was previously disconnected from the surface).
    kSuccessUnchanged,
    kSuccess,
    kFailure,
    kFailureSurfaceUnusable,
  };

  static constexpr uint32_t kGuestOutputMailboxSize = 3;

  struct GuestOutputProperties {
    // At least any value being 0 here means the guest output is disabled for
    // this frame.
    uint32_t frontbuffer_width;
    uint32_t frontbuffer_height;
    // Guest display aspect ratio numerator and denominator (both 16:9 and
    // 1280:720 kinds of values are accepted).
    uint32_t display_aspect_ratio_x;
    uint32_t display_aspect_ratio_y;
    bool is_8bpc;

    GuestOutputProperties() { SetToInactive(); }

    bool IsActive() const {
      return frontbuffer_width && frontbuffer_height &&
             display_aspect_ratio_x && display_aspect_ratio_y;
    }

    void SetToInactive() {
      frontbuffer_width = 0;
      frontbuffer_height = 0;
      display_aspect_ratio_x = 0;
      display_aspect_ratio_y = 0;
      is_8bpc = false;
    }
  };

  enum class GuestOutputPaintEffect {
    kBilinear,
    kBilinearDither,
    kCasSharpen,
    kCasSharpenDither,
    kCasResample,
    kCasResampleDither,
    kFsrEasu,
    kFsrRcas,
    kFsrRcasDither,

    kCount,
  };

  static constexpr bool CanGuestOutputPaintEffectBeIntermediate(
      GuestOutputPaintEffect effect) {
    switch (effect) {
      case GuestOutputPaintEffect::kBilinear:
      // Dithering is never performed in intermediate passes because it may be
      // interpreted as features by the subsequent passes.
      case GuestOutputPaintEffect::kBilinearDither:
      case GuestOutputPaintEffect::kCasSharpenDither:
      case GuestOutputPaintEffect::kCasResampleDither:
      case GuestOutputPaintEffect::kFsrRcasDither:
        return false;
      default:
        // The result of any other effect can be stretched with bilinear
        // filtering to the final resolution.
        return true;
    };
  }

  static constexpr bool CanGuestOutputPaintEffectBeFinal(
      GuestOutputPaintEffect effect) {
    switch (effect) {
      case GuestOutputPaintEffect::kFsrEasu:
        return false;
      default:
        return true;
    };
  }

  // The longest path is kFsrMaxUpscalingPassesMax + optionally RCAS +
  // optionally bilinear, when upscaling by more than
  // 2^kFsrMaxUpscalingPassesMax along any direction.
  // Non-FSR paths are either only bilinear, only CAS, or (when upscaling by
  // more than 2 along any direction) CAS followed by bilinear.
  static constexpr size_t kMaxGuestOutputPaintEffects =
      GuestOutputPaintConfig::kFsrMaxUpscalingPassesMax + 2;

  struct GuestOutputPaintFlow {
    // Letterbox on up to 4 sides.
    static constexpr size_t kMaxClearRectangles = 4;

    struct ClearRectangle {
      uint32_t x;
      uint32_t y;
      uint32_t width;
      uint32_t height;
    };

    GuestOutputProperties properties;

    // If 0, don't display the guest output.
    size_t effect_count;
    std::array<GuestOutputPaintEffect, kMaxGuestOutputPaintEffects> effects;
    std::array<std::pair<uint32_t, uint32_t>, kMaxGuestOutputPaintEffects>
        effect_output_sizes;

    // Offset of the rectangle for final drawing to the host window with
    // letterboxing.
    int32_t output_x;
    int32_t output_y;

    // If there is guest output (effect_count is not 0), contains the letterbox
    // rectangles around the guest output.
    size_t letterbox_clear_rectangle_count;
    std::array<ClearRectangle, kMaxClearRectangles> letterbox_clear_rectangles;

    void GetEffectInputSize(size_t effect_index, uint32_t& width_out,
                            uint32_t& height_out) const {
      assert_true(effect_index < effect_count);
      if (!effect_index) {
        width_out = properties.frontbuffer_width;
        height_out = properties.frontbuffer_height;
        return;
      }
      const std::pair<uint32_t, uint32_t>& intermediate_size =
          effect_output_sizes[effect_index - 1];
      width_out = intermediate_size.first;
      height_out = intermediate_size.second;
    }

    void GetEffectOutputOffset(size_t effect_index, int32_t& x_out,
                               int32_t& y_out) const {
      assert_true(effect_index < effect_count);
      if (effect_index + 1 < effect_count) {
        x_out = 0;
        y_out = 0;
        return;
      }
      x_out = output_x;
      y_out = output_y;
    }
  };

  struct BilinearConstants {
    int32_t output_offset[2];
    float output_size_inv[2];

    void Initialize(const GuestOutputPaintFlow& flow, size_t effect_index) {
      flow.GetEffectOutputOffset(effect_index, output_offset[0],
                                 output_offset[1]);
      const std::pair<uint32_t, uint32_t>& output_size =
          flow.effect_output_sizes[effect_index];
      output_size_inv[0] = 1.0f / float(output_size.first);
      output_size_inv[1] = 1.0f / float(output_size.second);
    }
  };

  static constexpr float CalculateCasPostSetupSharpness(float sharpness) {
    // CasSetup const1.x.
    return -1.0f / (8.0f - 3.0f * sharpness);
  }

  struct CasSharpenConstants {
    int32_t output_offset[2];
    float sharpness_post_setup;

    void Initialize(const GuestOutputPaintFlow& flow, size_t effect_index,
                    const GuestOutputPaintConfig& config) {
      flow.GetEffectOutputOffset(effect_index, output_offset[0],
                                 output_offset[1]);
      sharpness_post_setup =
          CalculateCasPostSetupSharpness(config.GetCasAdditionalSharpness());
    }
  };

  struct CasResampleConstants {
    int32_t output_offset[2];
    // Input size / output size.
    float input_output_size_ratio[2];
    float sharpness_post_setup;

    void Initialize(const GuestOutputPaintFlow& flow, size_t effect_index,
                    const GuestOutputPaintConfig& config) {
      flow.GetEffectOutputOffset(effect_index, output_offset[0],
                                 output_offset[1]);
      uint32_t input_width, input_height;
      flow.GetEffectInputSize(effect_index, input_width, input_height);
      const std::pair<uint32_t, uint32_t>& output_size =
          flow.effect_output_sizes[effect_index];
      input_output_size_ratio[0] =
          float(input_width) / float(output_size.first);
      input_output_size_ratio[1] =
          float(input_height) / float(output_size.second);
      sharpness_post_setup =
          CalculateCasPostSetupSharpness(config.GetCasAdditionalSharpness());
    }
  };

  struct FsrEasuConstants {
    // No output offset because the EASU pass is always done to an intermediate
    // framebuffer.
    float input_output_size_ratio[2];
    float input_size_inv[2];

    void Initialize(const GuestOutputPaintFlow& flow, size_t effect_index) {
      uint32_t input_width, input_height;
      flow.GetEffectInputSize(effect_index, input_width, input_height);
      const std::pair<uint32_t, uint32_t>& output_size =
          flow.effect_output_sizes[effect_index];
      input_output_size_ratio[0] =
          float(input_width) / float(output_size.first);
      input_output_size_ratio[1] =
          float(input_height) / float(output_size.second);
      input_size_inv[0] = 1.0f / float(input_width);
      input_size_inv[1] = 1.0f / float(input_height);
    }
  };

  struct FsrRcasConstants {
    int32_t output_offset[2];
    float sharpness_post_setup;

    static float CalculatePostSetupSharpness(float sharpness_reduction_stops) {
      // FsrRcasCon const0.x.
      return std::exp2f(-sharpness_reduction_stops);
    }

    void Initialize(const GuestOutputPaintFlow& flow, size_t effect_index,
                    const GuestOutputPaintConfig& config) {
      flow.GetEffectOutputOffset(effect_index, output_offset[0],
                                 output_offset[1]);
      sharpness_post_setup =
          CalculatePostSetupSharpness(config.GetFsrSharpnessReduction());
    }
  };

  explicit Presenter(HostGpuLossCallback host_gpu_loss_callback)
      : host_gpu_loss_callback_(host_gpu_loss_callback) {}

  // Must be called by the implementation's initialization, before the presenter
  // is used for anything.
  bool InitializeCommonSurfaceIndependent();

  // ConnectOrReconnect and Disconnect are callable only by the UI thread and
  // only when it has access to painting (PaintMode is not
  // kGuestOutputThreadImmediately).
  // Called only for a non-zero-area surface potentially supporting painting via
  // the presenter. In case of a failure, internally no resources referencing
  // the surface must be held by the implementation anymore - the implementation
  // must be left in the same state as after
  // DisconnectPaintingFromSurfaceFromUIThreadImpl. If the call is successful,
  // the implementation must write to is_vsync_implicit_out whether the
  // connection will now have vertical sync forced by the host window system,
  // which may cause undesirable waits on the CPU when beginning or ending
  // frames.
  virtual SurfacePaintConnectResult
  ConnectOrReconnectPaintingToSurfaceFromUIThread(
      Surface& new_surface, uint32_t new_surface_width,
      uint32_t new_surface_height, bool was_paintable,
      bool& is_vsync_implicit_out) = 0;
  // Releases resources referencing the surface in the implementation if they
  // are held by it. Call through DisconnectPaintingFromSurfaceFromUIThread to
  // ensure the implementation is only called while the connection is active.
  virtual void DisconnectPaintingFromSurfaceFromUIThreadImpl() = 0;

  // The returned lock interlocks multiple consumers (but not the producer and
  // the consumer) and must be held while accessing implementation-specific
  // objects that depend on the image or its index in the mailbox (unless there
  // are other locking mechanisms involved for the resources, such as reference
  // counting for the guest output images, which doesn't have to be atomic
  // though for the reason described later in this paragraph, or assumptions
  // like of main target painting being possible only in at most one thread at
  // once). While this lock is held, the currently acquired image index can't be
  // changed (by other consumers advancing the acquired image index to the new
  // ready image index), so the image with the index given by this function
  // can't be released and be made writable or given to a different consumer
  // (thus it's owned exclusively by the consumer who has called this function).
  // The properties are returned by copy rather than returning a pointer to them
  // or asking the consumer to pull them for the current mailbox index, so there
  // are less things to take into consideration while leaving the guest output
  // consumer critical section earlier (as if a pointer was returned, the data
  // behind it could be overwritten at any time after leaving the consumer
  // critical section) if the implementation has its own synchronization
  // mechanisms that allow for doing so as described earlier. Returns UINT32_MAX
  // as the mailbox index if the image is inactive (if it's active, it has
  // proper properties though).
  [[nodiscard]] std::unique_lock<std::mutex> ConsumeGuestOutput(
      uint32_t& mailbox_index_or_max_if_inactive_out,
      GuestOutputProperties* properties_out,
      GuestOutputPaintConfig* paint_config_out);
  // The properties are passed explicitly, not taken from the current acquired
  // image, so it can be called for a copy of the acquired image's properties
  // outside the consumer lock if the implementation has its own synchronization
  // (like reference counting for the guest output images) that makes it
  // possible to leave the consumer critical section earlier. Also, the guest
  // output paint configuration is passed explicitly too so calling this
  // function multiple times is safer.
  GuestOutputPaintFlow GetGuestOutputPaintFlow(
      const GuestOutputProperties& properties, uint32_t host_rt_width,
      uint32_t host_rt_height, uint32_t max_rt_width, uint32_t max_rt_height,
      const GuestOutputPaintConfig& config) const;
  // is_8bpc_out_ref is where to write whether the source actually has no more
  // than 8 bits of precision per channel (though the image provided by the
  // refresher may still have a higher storage precision) - if not written, it
  // will be assumed to be false.
  virtual bool RefreshGuestOutputImpl(
      uint32_t mailbox_index, uint32_t frontbuffer_width,
      uint32_t frontbuffer_height,
      std::function<bool(GuestOutputRefreshContext& context)> refresher,
      bool& is_8bpc_out_ref) = 0;

  // For guest output capturing (for debugging use thus - shouldn't be adding
  // any noise like dithering that's not present in the original image),
  // converting a 10bpc RGB pixel to 8bpc that can be stored in common image
  // formats.
  static uint32_t Packed10bpcRGBTo8bpcBytes(uint32_t rgb10) {
    // Conversion almost according to the Direct3D 10+ rules (unorm > float >
    // unorm), but with one multiplication rather than separate division and
    // multiplication - the results are the same for unorm10 to unorm8.
    if constexpr (std::endian::native == std::endian::big) {
      return (uint32_t(float(rgb10 & 0x3FF) * (255.0f / 1023.0f) + 0.5f)
              << 24) |
             (uint32_t(float((rgb10 >> 10) & 0x3FF) * (255.0f / 1023.0f) + 0.5f)
              << 16) |
             (uint32_t(float((rgb10 >> 20) & 0x3FF) * (255.0f / 1023.0f) + 0.5f)
              << 8) |
             uint32_t(0xFF);
    }
    return uint32_t(float(rgb10 & 0x3FF) * (255.0f / 1023.0f) + 0.5f) |
           (uint32_t(float((rgb10 >> 10) & 0x3FF) * (255.0f / 1023.0f) + 0.5f)
            << 8) |
           (uint32_t(float((rgb10 >> 20) & 0x3FF) * (255.0f / 1023.0f) + 0.5f)
            << 16) |
           (uint32_t(0xFF) << 24);
  }

  // Paints and presents the guest output if available (or just solid black
  // color), and if requested, the UI on top of it.
  //
  // May be called from the non-UI thread, but only to paint the guest output
  // (no UI drawing, with execute_ui_drawers disabled).
  //
  // Call via PaintAndPresent.
  virtual PaintResult PaintAndPresentImpl(bool execute_ui_drawers) = 0;

  // For calling from the painting implementations if requested.
  void ExecuteUIDrawersFromUIThread(UIDrawContext& ui_draw_context);

 private:
  enum class PaintMode {
    // Don't paint at all.
    // Painting lifecycle is accessible only by the UI thread.
    // window_->RequestPaint() must not be called in this mode at all regardless
    // of whether the Window object exists because the Window object in this
    // case may correspond to a window without a paintable Surface (in a closed
    // state, or in the middle of a surface change), and non-UI threads (such as
    // the guest output thread) may result in a race condition internally inside
    // Window::RequestPaint during the access to the Window's state, such as the
    // availability of the Surface that can handle the paint (therefore, if
    // there's no Surface, this is the only valid mode).
    kNone,
    // Guest output refreshing notifies the `window_`, which must be valid and
    // safe to call RequestPaint for, that painting should be done in the UI
    // thread (including the UI if needed). Painting is possible, and painting
    // lifecycle is accessible, only by the UI thread.
    kUIThreadOnRequest,
    // Paint immediately in the guest output thread for lower latency. The
    // `window_`, however, may be notified that the surface painting connection
    // has become outdated (via RequestPaint, as in this case the UI thread will
    // need to repaint as sooner as possible after reconnecting anyway), and
    // change the surface connection state accordingly (only to
    // kConnectedOutdated).
    // Painting is possible only by the guest output thread, lifecycle
    // management cannot be done from the UI thread until it takes over.
    kGuestOutputThreadImmediately,
  };

  enum class SurfacePaintConnectionState {
    // No surface at all, or couldn't connect with the current state of the
    // surface (such as because the surface was zero-sized because the window
    // was minimized, for example). Or, the connection has become outdated, and
    // the attempt to reconnect at kRetryConnectingSoon has failed. Try to
    // reconnect if anything changes in the state of the surface, such as its
    // size.
    kUnconnectedRetryAtStateChange,
    // Can't connect to the current existing surface (the surface has been lost
    // or it's completely incompatible). No point in retrying connecting until
    // the surface is replaced.
    kUnconnectedSurfaceReportedUnusable,
    // Everything is fine, can paint. The connection might have become
    // suboptimal though, and haven't tried refreshing yet, but still usable for
    // painting nonetheless.
    kConnectedPaintable,
    // The implementation still holds resources associated with the connection,
    // but presentation has reported that it has become outdated, try
    // reconnecting as soon as possible (at the next paint attempt, requesting
    // it if needed). This is the only state that the guest output thread may
    // transition the connection to (from kConnectedPaintable only) if it has
    // access to painting (the paint mode is kGuestOutputThreadImmediately).
    kConnectedOutdated,
  };

  static constexpr bool IsConnectedSurfacePaintConnectionState(
      SurfacePaintConnectionState connection_state) {
    return connection_state ==
               SurfacePaintConnectionState::kConnectedPaintable ||
           connection_state == SurfacePaintConnectionState::kConnectedOutdated;
  }

  struct UIDrawerReference {
    UIDrawer* drawer;
    uint64_t last_draw;

    explicit UIDrawerReference(UIDrawer* drawer,
                               uint64_t last_draw = UINT64_MAX)
        : drawer(drawer), last_draw(last_draw) {}
  };

  void SetPaintModeFromUIThread(PaintMode new_mode);
  // Based on conditions like whether UI needs to be drawn and whether vertical
  // sync is implicit - see the implementation for the requirements.
  // is_paintable is an explicit parameter because this function may be called
  // in two scenarios:
  // - After connection updates - painting connection is owned by the UI thread,
  //   so the actual state can be obtained and passed here so kNone can be
  //   returned.
  // - When merely toggling something local to the UI thread - only to toggle
  //   between the two threads, but not to switch from or to kNone (make sure
  //   it's not kNone before calling), pass `true` in this case.
  PaintMode GetDesiredPaintModeFromUIThread(bool is_paintable) const;

  // Callable only by the UI thread and only when it has access to painting
  // (PaintMode is not kGuestOutputThreadImmediately).
  // This can be called to a surface after having not been connected to any (in
  // this case, surface_paint_connection_state_ must be
  // kUnconnectedRetryAtStateChange, not kUnconnectedNoUsableSurface, otherwise
  // the call will be dropped), or to handle surface state changes such as
  // resizing. However, this must not be called to change directly from one
  // surface to another - need to disconnect prior to that, because the
  // implementation may assume that the surface is still the same, and may try
  // to, for instance, resize the buffers for the existing surface.
  void UpdateSurfacePaintConnectionFromUIThread(
      bool* repaint_needed_out, bool update_paint_mode_to_desired);
  // Callable only by the UI thread and only when it has access to painting
  // (PaintMode is not kGuestOutputThreadImmediately).
  // See DisconnectPaintingFromSurfaceFromUIThreadImpl for more information.
  void DisconnectPaintingFromSurfaceFromUIThread(
      SurfacePaintConnectionState new_state);

  // Can be called from any thread if an existing window_ safe to RequestPaint
  // (not closed) is available in it, so doesn't check the surface painting
  // connection state. Returns whether the window_->RequestPaint() call has been
  // made.
  bool RequestPaintOrConnectionRecoveryViaWindow(
      bool force_ui_thread_paint_tick);

  // Platform-specific function refreshing the monitor the current window
  // surface is on, through the Surface or its Window. A reference to the
  // monitor is held only when a Surface is available, so it's automatically
  // dropped when the Window loses its Surface when it's being closed (but the
  // Window object keeps being attached to the Presenter), for instance.
  void UpdateSurfaceMonitorFromUIThread(
      bool old_monitor_potentially_disconnected);
  // Platform-specific function returning whether the surface the presenter is
  // currently attached it is actually visible on any monitor. UI thread
  // painting may be dropped if this returns false - need to request painting if
  // the surface appears on a monitor again. May be using the state cached at
  // window / surface state changes, not the actual state from the platform.
  bool InSurfaceOnMonitorFromUIThread() const;

  // Calls PaintAndPresentImpl and does post-paint checks that are safe to do on
  // both the UI thread and the guest output thread. See the information about
  // PaintAndPresentImpl for details.
  // A kPresentedSuboptimal result is returned as is, but the connection may or
  // may not be made outdated if that happens - though if it's
  // kPresentedSuboptimal rather than kNotPresentedConnectionOutdated, the image
  // has been successfully sent to the OS presentation at least.
  PaintResult PaintAndPresent(bool execute_ui_drawers);

  void HandleUIDrawersChangeFromUIThread(bool drawers_were_empty);

  bool AreUITicksNeededFromUIThread() const {
    // UI drawing should be done, and painting needs to be possible (coarsely
    // checking because the actual connection state, including outdated, may be
    // currently unavailable from the UI thread).
    // There's no need to limit the frame rate manually if there is vertical
    // sync in the presentation already as that might result in inconsistent
    // frame pacing and potentially skipped vertical sync intervals.
    return !ui_drawers_.empty() && paint_mode_ != PaintMode::kNone &&
           !surface_paint_connection_has_implicit_vsync_;
  }
  void UpdateUITicksNeededFromUIThread();
  void WaitForUITickFromUIThread();
  // May be called from any thread.
  void ForceUIThreadPaintTick();

  // Must be called only in the end of entry points - reinitialization of the
  // presenter may be done by the handler if it was called from the UI thread
  // (even if the UI thread argument is false - such as when the guest output is
  // refreshed on the UI thread).
  HostGpuLossCallback host_gpu_loss_callback_;

  // May be accessed by the guest output thread if the paint mode is not kNone,
  // to request painting (for kUIThreadOnRequest) or reconnection (for
  // kGuestOutputThreadImmediately) in the UI thread. Set the paint mode to
  // kNone before modifying (that naturally has to be done anyway by
  // disconnecting painting).
  Window* window_ = nullptr;

  // The surface of the `window_` the presenter is currently attached to.
  Surface* surface_ = nullptr;

  // Mutex protecting paint_mode_ (and, in the guest output thread, objects
  // related to painting themselves).
  //
  // The UI thread (as the mode is modifiable only by it) can use it as
  // "barriers", like:
  // 1) If needed, lock and disable guest output thread access to painting.
  // 2) Interact with the painting connection.
  // 3) If needed, lock and re-enable guest output thread access to the
  //    painting.
  //
  // On the other hand, the guest output thread _must_ hold it all the time it's
  // painting, to ensure the mode stays the same while it's painting.
  std::mutex paint_mode_mutex_;
  // UI thread: writable, guest output thread: read-only.
  PaintMode paint_mode_ = PaintMode::kNone;

  // These fields can be accessed _exclusively_ by either the UI thread or the
  // guest output thread, depending on paint_mode_.
  // If it's kGuestOutputThreadImmediately, they can be accessed _only_ by the
  // guest output thread (though the UI thread can still read, but not modify,
  // fields that are writable by the UI thread and readadable by both).
  // Otherwise, they can be accessed _only_ by the UI thread.
  // The connection state may be changed from the guest output thread, but only
  // from kConnectedPaintable to kConnectedOutdated.
  SurfacePaintConnectionState surface_paint_connection_state_ =
      SurfacePaintConnectionState::kUnconnectedRetryAtStateChange;
  // If the surface connection was optimal at the last paint attempt, but now
  // has become suboptimal, need to try to reconnect. But only in this case - if
  // the connection has been suboptimal from the very beginning don't try to
  // reconnect every frame.
  bool surface_paint_connection_was_optimal_at_successful_paint_ = false;

  // Modifiable only by the UI thread (therefore can be accessed by the UI
  // thread regardless of the paint mode) while (re)connecting painting to the
  // surface.
  bool surface_paint_connection_has_implicit_vsync_ = false;
  // Modifiable only by the UI thread, can be read by the thread that's
  // painting.
  uint32_t surface_width_in_paint_connection_ = 0;
  uint32_t surface_height_in_paint_connection_ = 0;

  // Can be set by both the UI thread and the guest output thread before doing
  // window_->RequestPaint() - whether an extra painting (preceded by
  // reconnection if needed, and painting) was requested, primarily after some
  // state change that may effect the surface painting connection, resulting in
  // the need to refresh it as soon as possible.
  //
  // Relaxed memory order is enough, everything that may influence painting is
  // either local to the UI thread or protected with barriers elsewhere.
  //
  // There's no need to bother about resetting this variable when losing
  // connection as the next successful reconnection should be followed by a
  // repaint request anyway.
  std::atomic<bool> ui_thread_paint_requested_{false};

  std::mutex guest_output_paint_config_mutex_;
  // UI thread: writable, guest output thread: read-only.
  GuestOutputPaintConfig guest_output_paint_config_;

  // Single-producer-multiple-consumers (lock-free SPSC + consumer lock) mailbox
  // for presenting of the most up-to-date guest output image without long
  // interlocking between guest output refreshing and painting.
  static_assert(kGuestOutputMailboxSize == 3);
  // The "acquired" image (in bits 0:1) is the one that is currently being read,
  // or was last read, by a consumer of the guest output. The index of it can be
  // modified only by the consumer and stays the same while it's processing the
  // image.
  // The "ready" image (in bits 2:3) is the most up-to-date image that the
  // refresher has completely written, and a consumer may acquire it. It may be
  // == acquired if there has been no refresh since the last acquisition.
  // These two images can be accessed by painting in parallel, in an unordered
  // way, with guest output refreshing.
  std::atomic<uint32_t> guest_output_mailbox_acquired_and_ready_{0};
  // The "writable" image is different than both "acquired" and "ready" and is
  // accessible only by the guest output refreshing - it's the image that the
  // refresher may write to.
  uint32_t guest_output_mailbox_writable_ = 1;
  // The guest output images may be consumed by two operations - painting, and
  // capturing to a CPU-side buffer. These two usually never happen in parallel
  // in reality though, as they're usually not even needed both at once in the
  // same app within Xenia, so there's no need to create any
  // complex lock-free synchronization between the two, but still, the situation
  // when multiple consumers want the guest output image at the same is
  // perfectly valid (unlike for producers, because even with a producer lock
  // that would still be a race condition since the two refreshes themselves
  // will be done in an undefined order) - so, a sufficient synchronization
  // mechanism is used to make sure multiple consumers can acquire images
  // without interfering with each other.
  // While this is held, paint_mode_mutex_ must not be locked (the lock order is
  // the reverse when painting in the guest output thread - painting is done
  // with paint_mode_mutex_ held in this case, and guest output consumption
  // happens as part of painting.
  std::mutex guest_output_mailbox_consumer_mutex_;

  std::array<GuestOutputProperties, kGuestOutputMailboxSize>
      guest_output_properties_;
  // Accessible only by refreshing, whether the last refresh contained an image
  // rather than being blank.
  bool guest_output_active_last_refresh_ = false;

  // Ordered by the Z order, and then by the time of addition.
  // Note: All the iteration logic involving this Z ordering must be the same as
  // in input handling (in the input listeners in the Window), but in reverse.
  std::multimap<size_t, UIDrawerReference> ui_drawers_;

  size_t ui_draw_current_ = 0;
  size_t ui_draw_current_z_order_;
  std::multimap<size_t, UIDrawerReference>::iterator ui_draw_next_iterator_;
  bool is_executing_ui_drawers_ = false;

  // Whether currently running the logic of PaintFromUIThread, so certain
  // actions (such as changing the paint mode, requesting a redraw) must be
  // deferred and be handled by the tail of PaintFromUIThread for consistency
  // with what PaintFromUIThread does internally.
  bool is_in_ui_thread_paint_ = false;
  bool request_guest_output_paint_after_current_ui_thread_paint_;
  bool request_ui_paint_after_current_ui_thread_paint_;

  // Platform-specific, but implementation-agnostic parts, primarily for
  // limiting of the frame rate of the UI to avoid drawing the UI at extreme
  // frame rates wasting the CPU and the GPU resources and starving everything
  // else. The waits performed here must be interruptible by guest output
  // presentation requests to prevent adding arbitrary amounts of latency to it.
  // On Android and GTK, this is not needed, the frame rate of draw events is
  // limited to the display refresh rate internally.
#if XE_PLATFORM_WIN32
  static Microsoft::WRL::ComPtr<IDXGIOutput> GetDXGIOutputForMonitor(
      IDXGIFactory1* factory, HMONITOR monitor);
  bool AreDXGIUITicksWaitable(
      [[maybe_unused]] const std::unique_lock<std::mutex>& dxgi_ui_tick_lock) {
    return dxgi_ui_ticks_needed_ && !dxgi_ui_tick_thread_shutdown_ &&
           dxgi_ui_tick_output_;
  }
  void DXGIUITickThread();

  // Accessible only from the UI thread, to avoid updating monitor-dependent
  // information such as the DXGI output if the monitor hasn't actually been
  // changed in the current state change (such as window positioning changes).
  HMONITOR surface_win32_monitor_ = nullptr;

  // Requiring the lowest version of DXGI for IDXGIOutput::WaitForVBlank, which
  // is available even on Windows Vista, but for IDXGIFactory1::IsCurrent,
  // DXGI 1.1 is needed (available starting from Windows 7; also mixing DXGI 1.0
  // and 1.1+ in the Direct3D 12 code is not supported, see CreateDXGIFactory on
  // MSDN). The factory is created when it's needed, and may be released and
  // recreated when it's not current anymore and that becomes relevant.
  Microsoft::WRL::ComPtr<IDXGIFactory1> dxgi_ui_tick_factory_;

  // Accessible only from the UI thread, though the value is taken from the
  // tick-mutex-protected variable.
  uint64_t dxgi_ui_tick_last_draw_ = 0;

  std::mutex dxgi_ui_tick_mutex_;
  uint64_t dxgi_ui_tick_last_vblank_ = 1;
  // If output is null or shutdown is true, the signal may not be sent, either
  // don't limit the frame rate in this case (an exceptional situation, such as
  // a failure to find the output in DXGI), or don't draw at all if the window
  // was removed from a connected monitor.
  Microsoft::WRL::ComPtr<IDXGIOutput> dxgi_ui_tick_output_;
  // To avoid allocating processing resources to the thread when nothing needs
  // the ticks (not drawing the UI), the thread waits for vertical blanking
  // intervals only when the UI drawing ticks are needed, and sleeping waiting
  // for the control condition variable signals otherwise. Modifiable only from
  // the UI thread, so readable by it without locking the mutex.
  bool dxgi_ui_ticks_needed_ = false;
  // The shutdown flag is modifiable only from the UI thread.
  bool dxgi_ui_tick_thread_shutdown_ = false;
  bool dxgi_ui_tick_force_requested_ = false;

  std::condition_variable dxgi_ui_tick_control_condition_;
  // May be signaled by guest output refreshing.
  std::condition_variable dxgi_ui_tick_signal_condition_;

  std::thread dxgi_ui_tick_thread_;
#endif  // XE_PLATFORM
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_PRESENTER_H_
