// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_DRM_GPU_DRM_GPU_DISPLAY_MANAGER_H_
#define UI_OZONE_PLATFORM_DRM_GPU_DRM_GPU_DISPLAY_MANAGER_H_

#include <stdint.h>
#include <memory>
#include <vector>

#include "base/callback.h"
#include "ui/display/types/display_configuration_params.h"
#include "ui/display/types/display_constants.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/ozone/platform/drm/common/display_types.h"

using drmModeModeInfo = struct _drmModeModeInfo;

namespace display {
struct GammaRampRGBEntry;
}  // namespace display

namespace gfx {
class ColorSpace;
}

namespace ui {

class DrmDeviceManager;
class DrmDisplay;
class ScreenManager;

class DrmGpuDisplayManager {
 public:
  DrmGpuDisplayManager(ScreenManager* screen_manager,
                       DrmDeviceManager* drm_device_manager);

  DrmGpuDisplayManager(const DrmGpuDisplayManager&) = delete;
  DrmGpuDisplayManager& operator=(const DrmGpuDisplayManager&) = delete;

  ~DrmGpuDisplayManager();

  // Sets a callback that will be notified when display configuration may have
  // changed to clear the overlay configuration cache.
  void SetClearOverlayCacheCallback(base::RepeatingClosure callback);

  // Returns a list of the connected displays. When this is called the list of
  // displays is refreshed.
  MovableDisplaySnapshots GetDisplays();

  // Takes/releases the control of the DRM devices.
  bool TakeDisplayControl();
  void RelinquishDisplayControl();

  bool ConfigureDisplays(
      const std::vector<display::DisplayConfigurationParams>& config_requests);
  bool GetHDCPState(int64_t display_id,
                    display::HDCPState* state,
                    display::ContentProtectionMethod* protection_method);
  bool SetHDCPState(int64_t display_id,
                    display::HDCPState state,
                    display::ContentProtectionMethod protection_method);
  void SetColorMatrix(int64_t display_id,
                      const std::vector<float>& color_matrix);
  void SetBackgroundColor(int64_t display_id, const uint64_t background_color);
  void SetGammaCorrection(
      int64_t display_id,
      const std::vector<display::GammaRampRGBEntry>& degamma_lut,
      const std::vector<display::GammaRampRGBEntry>& gamma_lut);
  void SetPrivacyScreen(int64_t display_id, bool enabled);

  void SetColorSpace(int64_t crtc_id, const gfx::ColorSpace& color_space);

 private:
  DrmDisplay* FindDisplay(int64_t display_id);

  // Notify ScreenManager of all the displays that were present before the
  // update but are gone after the update.
  void NotifyScreenManager(
      const std::vector<std::unique_ptr<DrmDisplay>>& new_displays,
      const std::vector<std::unique_ptr<DrmDisplay>>& old_displays) const;

  ScreenManager* const screen_manager_;         // Not owned.
  DrmDeviceManager* const drm_device_manager_;  // Not owned.

  std::vector<std::unique_ptr<DrmDisplay>> displays_;

  base::RepeatingClosure clear_overlay_cache_callback_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_DRM_GPU_DRM_GPU_DISPLAY_MANAGER_H_
