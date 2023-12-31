// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_PHONEHUB_ENABLE_HOTSPOT_QUICK_ACTION_CONTROLLER_H_
#define ASH_SYSTEM_PHONEHUB_ENABLE_HOTSPOT_QUICK_ACTION_CONTROLLER_H_

#include "ash/components/phonehub/tether_controller.h"
#include "ash/system/phonehub/quick_action_controller_base.h"

namespace ash {

// Controller of a quick action item that toggles Locate phone mode.
class EnableHotspotQuickActionController
    : public QuickActionControllerBase,
      public chromeos::phonehub::TetherController::Observer {
 public:
  explicit EnableHotspotQuickActionController(
      chromeos::phonehub::TetherController* tether_controller);
  ~EnableHotspotQuickActionController() override;
  EnableHotspotQuickActionController(EnableHotspotQuickActionController&) =
      delete;
  EnableHotspotQuickActionController operator=(
      EnableHotspotQuickActionController&) = delete;

  // QuickActionControllerBase:
  QuickActionItem* CreateItem() override;
  void OnButtonPressed(bool is_now_enabled) override;

  // chromeos::phonehub::TetherController::Observer:
  void OnTetherStatusChanged() override;

 private:
  // All the possible states that the enable hotspot button can be viewed. Each
  // state has a corresponding icon, labels and tooltip view.
  enum class ActionState { kOff, kConnecting, kConnected, kNoReception };

  // Set the item (including icon, label and tooltips) to a certain state.
  void SetState(ActionState state);

  chromeos::phonehub::TetherController* tether_controller_ = nullptr;
  QuickActionItem* item_ = nullptr;
};

}  // namespace ash

#endif  // ASH_SYSTEM_PHONEHUB_ENABLE_HOTSPOT_QUICK_ACTION_CONTROLLER_H_
