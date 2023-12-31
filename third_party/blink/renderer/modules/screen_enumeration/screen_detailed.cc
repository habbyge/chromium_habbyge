// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/screen_enumeration/screen_detailed.h"

#include "base/numerics/safe_conversions.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/platform/wtf/text/string_statics.h"
#include "ui/display/screen_info.h"
#include "ui/display/screen_infos.h"

namespace blink {

namespace {

const display::ScreenInfo& GetScreenInfo(LocalFrame& frame,
                                         int64_t display_id) {
  const auto& screen_infos = frame.GetChromeClient().GetScreenInfos(frame);
  for (const auto& screen : screen_infos.screen_infos) {
    if (screen.display_id == display_id)
      return screen;
  }
  DEFINE_STATIC_LOCAL(display::ScreenInfo, kEmptyScreenInfo, ());
  return kEmptyScreenInfo;
}

}  // namespace

ScreenDetailed::ScreenDetailed(LocalDOMWindow* window,
                               int64_t display_id,
                               bool label_is_internal,
                               uint32_t label_idx)
    : Screen(window),
      label_idx_(label_idx),
      label_is_internal_(label_is_internal),
      display_id_(display_id) {}

// static
bool ScreenDetailed::AreWebExposedScreenDetailedPropertiesEqual(
    const display::ScreenInfo& prev,
    const display::ScreenInfo& current) {
  if (!Screen::AreWebExposedScreenPropertiesEqual(prev, current))
    return false;

  // left() / top()
  if (prev.rect.origin() != current.rect.origin())
    return false;

  // isPrimary()
  if (prev.is_primary != current.is_primary)
    return false;

  // isInternal()
  if (prev.is_internal != current.is_internal)
    return false;

  // Note: devicePixelRatio() covered by Screen base function
  // TODO: handle label() when it gets implemented.

  return true;
}

int ScreenDetailed::height() const {
  if (!DomWindow())
    return 0;
  LocalFrame* frame = DomWindow()->GetFrame();
  const display::ScreenInfo& screen_info = GetScreenInfo(*frame, display_id_);
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk()) {
    return base::ClampRound(screen_info.rect.height() *
                            screen_info.device_scale_factor);
  }
  return screen_info.rect.height();
}

int ScreenDetailed::width() const {
  if (!DomWindow())
    return 0;
  LocalFrame* frame = DomWindow()->GetFrame();
  const display::ScreenInfo& screen_info = GetScreenInfo(*frame, display_id_);
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk()) {
    return base::ClampRound(screen_info.rect.width() *
                            screen_info.device_scale_factor);
  }
  return screen_info.rect.width();
}

unsigned ScreenDetailed::colorDepth() const {
  if (!DomWindow())
    return 0;
  LocalFrame* frame = DomWindow()->GetFrame();
  return GetScreenInfo(*frame, display_id_).depth;
}

unsigned ScreenDetailed::pixelDepth() const {
  return colorDepth();
}

int ScreenDetailed::availLeft() const {
  if (!DomWindow())
    return 0;
  LocalFrame* frame = DomWindow()->GetFrame();
  const display::ScreenInfo& screen_info = GetScreenInfo(*frame, display_id_);
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk()) {
    return base::ClampRound(screen_info.available_rect.x() *
                            screen_info.device_scale_factor);
  }
  return screen_info.available_rect.x();
}

int ScreenDetailed::availTop() const {
  if (!DomWindow())
    return 0;
  LocalFrame* frame = DomWindow()->GetFrame();
  const display::ScreenInfo& screen_info = GetScreenInfo(*frame, display_id_);
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk()) {
    return base::ClampRound(screen_info.available_rect.y() *
                            screen_info.device_scale_factor);
  }
  return screen_info.available_rect.y();
}

int ScreenDetailed::availHeight() const {
  if (!DomWindow())
    return 0;
  LocalFrame* frame = DomWindow()->GetFrame();
  const display::ScreenInfo& screen_info = GetScreenInfo(*frame, display_id_);
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk()) {
    return base::ClampRound(screen_info.available_rect.height() *
                            screen_info.device_scale_factor);
  }
  return screen_info.available_rect.height();
}

int ScreenDetailed::availWidth() const {
  if (!DomWindow())
    return 0;
  LocalFrame* frame = DomWindow()->GetFrame();
  const display::ScreenInfo& screen_info = GetScreenInfo(*frame, display_id_);
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk()) {
    return base::ClampRound(screen_info.available_rect.width() *
                            screen_info.device_scale_factor);
  }
  return screen_info.available_rect.width();
}

bool ScreenDetailed::isExtended() const {
  if (!DomWindow())
    return false;
  LocalFrame* frame = DomWindow()->GetFrame();
  return GetScreenInfo(*frame, display_id_).is_extended;
}

int ScreenDetailed::left() const {
  if (!DomWindow())
    return 0;
  LocalFrame* frame = DomWindow()->GetFrame();
  const display::ScreenInfo& screen_info = GetScreenInfo(*frame, display_id_);
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk()) {
    return base::ClampRound(screen_info.rect.x() *
                            screen_info.device_scale_factor);
  }
  return screen_info.rect.x();
}

int ScreenDetailed::top() const {
  if (!DomWindow())
    return 0;
  LocalFrame* frame = DomWindow()->GetFrame();
  const display::ScreenInfo& screen_info = GetScreenInfo(*frame, display_id_);
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk()) {
    return base::ClampRound(screen_info.rect.y() *
                            screen_info.device_scale_factor);
  }
  return screen_info.rect.y();
}

bool ScreenDetailed::isPrimary() const {
  if (!DomWindow())
    return false;
  LocalFrame* frame = DomWindow()->GetFrame();
  return GetScreenInfo(*frame, display_id_).is_primary;
}

bool ScreenDetailed::isInternal() const {
  if (!DomWindow())
    return false;
  LocalFrame* frame = DomWindow()->GetFrame();
  return GetScreenInfo(*frame, display_id_).is_internal;
}

float ScreenDetailed::devicePixelRatio() const {
  if (!DomWindow())
    return 0.f;
  LocalFrame* frame = DomWindow()->GetFrame();
  return GetScreenInfo(*frame, display_id_).device_scale_factor;
}

String ScreenDetailed::label() const {
  // Returns a placeholder label, e.g. "Internal Display 1".
  // These don't have to be unique, but it's nice to be able to differentiate
  // if a user has two external screens, for example.
  const char* prefix =
      label_is_internal_ ? "Internal Display " : "External Display ";
  return String(prefix) + String::Number(label_idx_);
}

int64_t ScreenDetailed::DisplayId() const {
  return display_id_;
}

}  // namespace blink
