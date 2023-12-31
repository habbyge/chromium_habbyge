// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_EXO_SERVER_WAYLAND_SERVER_CONTROLLER_H_
#define COMPONENTS_EXO_SERVER_WAYLAND_SERVER_CONTROLLER_H_

#include <memory>

#include "components/exo/display.h"

namespace exo {

namespace wayland {
class Server;
}  // namespace wayland

class DataExchangeDelegate;
class WMHelper;
class NotificationSurfaceManager;
class InputMethodSurfaceManager;
class ToastSurfaceManager;

class WaylandServerController {
 public:
  static std::unique_ptr<WaylandServerController> CreateForArcIfNecessary(
      std::unique_ptr<DataExchangeDelegate> data_exchange_delegate);

  // Creates WaylandServerController. Returns null if controller should not be
  // created.
  static std::unique_ptr<WaylandServerController> CreateIfNecessary(
      std::unique_ptr<DataExchangeDelegate> data_exchange_delegate,
      std::unique_ptr<NotificationSurfaceManager> notification_surface_manager,
      std::unique_ptr<InputMethodSurfaceManager> input_method_surface_manager,
      std::unique_ptr<ToastSurfaceManager> toast_surface_manager);

  WaylandServerController(const WaylandServerController&) = delete;
  WaylandServerController& operator=(const WaylandServerController&) = delete;

  ~WaylandServerController();

  InputMethodSurfaceManager* input_method_surface_manager() {
    return display_->input_method_surface_manager();
  }

  WaylandServerController(
      std::unique_ptr<DataExchangeDelegate> data_exchange_delegate,
      std::unique_ptr<NotificationSurfaceManager> notification_surface_manager,
      std::unique_ptr<InputMethodSurfaceManager> input_method_surface_manager,
      std::unique_ptr<ToastSurfaceManager> toast_surface_manager);

 private:
  std::unique_ptr<WMHelper> wm_helper_;
  std::unique_ptr<Display> display_;
  std::unique_ptr<wayland::Server> wayland_server_;
};

}  // namespace exo

#endif  // COMPONENTS_EXO_SERVER_WAYLAND_SERVER_CONTROLLER_H_
