// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/test_shell_delegate.h"

#include <memory>

#include "ash/accessibility/default_accessibility_delegate.h"
#include "ash/capture_mode/test_capture_mode_delegate.h"
#include "ash/constants/app_types.h"
#include "ash/public/cpp/desk_template.h"
#include "ash/public/cpp/test/test_nearby_share_delegate.h"
#include "ash/system/tray/system_tray_notifier.h"
#include "ash/wm/gestures/back_gesture/test_back_gesture_contextual_nudge_delegate.h"
#include "components/app_restore/app_launch_info.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/gfx/image/image.h"

namespace ash {

TestShellDelegate::TestShellDelegate() = default;

TestShellDelegate::~TestShellDelegate() = default;

bool TestShellDelegate::CanShowWindowForUser(const aura::Window* window) const {
  return true;
}

std::unique_ptr<CaptureModeDelegate>
TestShellDelegate::CreateCaptureModeDelegate() const {
  return std::make_unique<TestCaptureModeDelegate>();
}

AccessibilityDelegate* TestShellDelegate::CreateAccessibilityDelegate() {
  return new DefaultAccessibilityDelegate;
}

std::unique_ptr<BackGestureContextualNudgeDelegate>
TestShellDelegate::CreateBackGestureContextualNudgeDelegate(
    BackGestureContextualNudgeController* controller) {
  return std::make_unique<TestBackGestureContextualNudgeDelegate>(controller);
}

bool TestShellDelegate::CanGoBack(gfx::NativeWindow window) const {
  return can_go_back_;
}

void TestShellDelegate::SetTabScrubberEnabled(bool enabled) {
  tab_scrubber_enabled_ = enabled;
}

bool TestShellDelegate::ShouldWaitForTouchPressAck(gfx::NativeWindow window) {
  return should_wait_for_touch_ack_;
}

int TestShellDelegate::GetBrowserWebUITabStripHeight() {
  return 0;
}

void TestShellDelegate::BindMultiDeviceSetup(
    mojo::PendingReceiver<chromeos::multidevice_setup::mojom::MultiDeviceSetup>
        receiver) {
  if (multidevice_setup_binder_)
    multidevice_setup_binder_.Run(std::move(receiver));
}

void TestShellDelegate::SetCanGoBack(bool can_go_back) {
  can_go_back_ = can_go_back;
}

void TestShellDelegate::SetShouldWaitForTouchAck(
    bool should_wait_for_touch_ack) {
  should_wait_for_touch_ack_ = should_wait_for_touch_ack;
}

std::unique_ptr<NearbyShareDelegate>
TestShellDelegate::CreateNearbyShareDelegate(
    NearbyShareController* controller) const {
  return std::make_unique<TestNearbyShareDelegate>();
}

bool TestShellDelegate::IsSessionRestoreInProgress() const {
  return session_restore_in_progress_;
}

void TestShellDelegate::SetSessionRestoreInProgress(bool in_progress) {
  session_restore_in_progress_ = in_progress;
}

bool TestShellDelegate::IsLoggingRedirectDisabled() const {
  return false;
}

base::FilePath TestShellDelegate::GetPrimaryUserDownloadsFolder() const {
  return base::FilePath();
}

std::unique_ptr<app_restore::AppLaunchInfo>
TestShellDelegate::GetAppLaunchDataForDeskTemplate(aura::Window* window) const {
  return nullptr;
}

void TestShellDelegate::GetFaviconForUrl(
    const std::string& page_url,
    int desired_icon_size,
    favicon_base::FaviconRawBitmapCallback callback,
    base::CancelableTaskTracker* tracker) const {}

void TestShellDelegate::GetIconForAppId(
    const std::string& app_id,
    int desired_icon_size,
    base::OnceCallback<void(apps::mojom::IconValuePtr icon_value)> callback)
    const {}

void TestShellDelegate::LaunchAppsFromTemplate(
    std::unique_ptr<DeskTemplate> desk_template) {}

bool TestShellDelegate::IsWindowSupportedForDeskTemplate(
    aura::Window* window) const {
  const ash::AppType app_type =
      static_cast<ash::AppType>(window->GetProperty(aura::client::kAppType));
  switch (app_type) {
    case AppType::CROSTINI_APP:
    case AppType::LACROS:
      return false;
    default:
      break;
  }
  return true;
}

}  // namespace ash
