// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/quick_answers/quick_answers_state_controller.h"

#include "ash/session/session_controller_impl.h"
#include "ash/shell.h"

namespace ash {

QuickAnswersStateController::QuickAnswersStateController()
    : session_observer_(this) {}

QuickAnswersStateController::~QuickAnswersStateController() = default;

void QuickAnswersStateController::OnFirstSessionStarted() {
  state_.RegisterPrefChanges(
      Shell::Get()->session_controller()->GetPrimaryUserPrefService());
}

}  // namespace ash
