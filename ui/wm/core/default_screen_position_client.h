// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_WM_CORE_DEFAULT_SCREEN_POSITION_CLIENT_H_
#define UI_WM_CORE_DEFAULT_SCREEN_POSITION_CLIENT_H_

#include "ui/aura/client/screen_position_client.h"
#include "ui/wm/core/wm_core_export.h"

namespace wm {

// Client that always offsets by the toplevel RootWindow of the passed
// in child NativeWidgetAura.
class WM_CORE_EXPORT DefaultScreenPositionClient
    : public aura::client::ScreenPositionClient {
 public:
  explicit DefaultScreenPositionClient(aura::Window* root_window);

  DefaultScreenPositionClient(const DefaultScreenPositionClient&) = delete;
  DefaultScreenPositionClient& operator=(const DefaultScreenPositionClient&) =
      delete;

  ~DefaultScreenPositionClient() override;

  // aura::client::ScreenPositionClient overrides:
  void ConvertPointToScreen(const aura::Window* window,
                            gfx::PointF* point) override;
  void ConvertPointFromScreen(const aura::Window* window,
                              gfx::PointF* point) override;
  void ConvertHostPointToScreen(aura::Window* window,
                                gfx::Point* point) override;
  void SetBounds(aura::Window* window,
                 const gfx::Rect& bounds,
                 const display::Display& display) override;

 protected:
  // aura::client::ScreenPositionClient:
  gfx::Point GetRootWindowOriginInScreen(
      const aura::Window* root_window) override;

 private:
  aura::Window* root_window_;
};

}  // namespace wm

#endif  // UI_WM_CORE_DEFAULT_SCREEN_POSITION_CLIENT_H_
