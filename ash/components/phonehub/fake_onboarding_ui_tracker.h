// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_COMPONENTS_PHONEHUB_FAKE_ONBOARDING_UI_TRACKER_H_
#define ASH_COMPONENTS_PHONEHUB_FAKE_ONBOARDING_UI_TRACKER_H_

#include "ash/components/phonehub/onboarding_ui_tracker.h"

namespace chromeos {
namespace phonehub {

class FakeOnboardingUiTracker : public OnboardingUiTracker {
 public:
  FakeOnboardingUiTracker();
  ~FakeOnboardingUiTracker() override;

  void SetShouldShowOnboardingUi(bool should_show_onboarding_ui);

  // OnboardingUiTracker:
  bool ShouldShowOnboardingUi() const override;

  size_t handle_get_started_call_count() {
    return handle_get_started_call_count_;
  }

 private:
  // OnboardingUiTracker:
  void DismissSetupUi() override;
  void HandleGetStarted() override;

  bool should_show_onboarding_ui_ = false;
  size_t handle_get_started_call_count_ = 0;
};

}  // namespace phonehub
}  // namespace chromeos

#endif  // ASH_COMPONENTS_PHONEHUB_FAKE_ONBOARDING_UI_TRACKER_H_
