// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_DRM_COMMON_DRM_UTIL_H_
#define UI_OZONE_PLATFORM_DRM_COMMON_DRM_UTIL_H_

#include <stddef.h>

#include <memory>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "ui/display/types/display_snapshot.h"
#include "ui/ozone/platform/drm/common/display_types.h"
#include "ui/ozone/platform/drm/common/scoped_drm_types.h"

typedef struct _drmModeModeInfo drmModeModeInfo;

namespace display {
class DisplayMode;
}  // namespace display

namespace gfx {
class Point;
}

namespace ui {

// It is safe to assume there will be no more than 256 connected DRM devices.
constexpr int kMaxDrmCount = 256u;

// It is safe to assume there will be no more than 256 connectors per DRM.
constexpr int kMaxDrmConnectors = 256u;

// Representation of the information required to initialize and configure a
// native display. |index| is the position of the connection and will be
// used to generate a unique identifier for the display.
class HardwareDisplayControllerInfo {
 public:
  HardwareDisplayControllerInfo(ScopedDrmConnectorPtr connector,
                                ScopedDrmCrtcPtr crtc,
                                uint8_t index);

  HardwareDisplayControllerInfo(const HardwareDisplayControllerInfo&) = delete;
  HardwareDisplayControllerInfo& operator=(
      const HardwareDisplayControllerInfo&) = delete;

  ~HardwareDisplayControllerInfo();

  drmModeConnector* connector() const { return connector_.get(); }
  drmModeCrtc* crtc() const { return crtc_.get(); }
  uint8_t index() const { return index_; }

 private:
  ScopedDrmConnectorPtr connector_;
  ScopedDrmCrtcPtr crtc_;
  uint8_t index_;
};

// Looks-up and parses the native display configurations returning all available
// displays.
std::vector<std::unique_ptr<HardwareDisplayControllerInfo>>
GetAvailableDisplayControllerInfos(int fd);

bool SameMode(const drmModeModeInfo& lhs, const drmModeModeInfo& rhs);

std::unique_ptr<display::DisplayMode> CreateDisplayMode(
    const drmModeModeInfo& mode);

// Extracts the display modes list from |info| as well as the current and native
// display modes given the |active_pixel_size| which is retrieved from the first
// detailed timing descriptor in the EDID.
display::DisplaySnapshot::DisplayModeList ExtractDisplayModes(
    HardwareDisplayControllerInfo* info,
    const gfx::Size& active_pixel_size,
    const display::DisplayMode** out_current_mode,
    const display::DisplayMode** out_native_mode);

// |info| provides the DRM information related to the display, |fd| is the
// connection to the DRM device.
std::unique_ptr<display::DisplaySnapshot> CreateDisplaySnapshot(
    HardwareDisplayControllerInfo* info,
    int fd,
    const base::FilePath& sys_path,
    uint8_t device_index,
    const gfx::Point& origin);

int GetFourCCFormatForOpaqueFramebuffer(gfx::BufferFormat format);

gfx::Size GetMaximumCursorSize(int fd);

ScopedDrmPropertyPtr FindDrmProperty(int fd,
                                     drmModeObjectProperties* properties,
                                     const char* name);

bool HasColorCorrectionMatrix(int fd, drmModeCrtc* crtc);

bool MatchMode(const display::DisplayMode& display_mode,
               const drmModeModeInfo& m);

const gfx::Size ModeSize(const drmModeModeInfo& mode);

float ModeRefreshRate(const drmModeModeInfo& mode);

bool ModeIsInterlaced(const drmModeModeInfo& mode);

uint64_t GetEnumValueForName(int fd, int property_id, const char* str);

std::vector<uint64_t> ParsePathBlob(const drmModePropertyBlobRes& path_blob);

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_DRM_COMMON_DRM_UTIL_H_
