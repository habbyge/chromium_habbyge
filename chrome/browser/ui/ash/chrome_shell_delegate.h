// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_CHROME_SHELL_DELEGATE_H_
#define CHROME_BROWSER_UI_ASH_CHROME_SHELL_DELEGATE_H_

#include <memory>

#include "ash/shell_delegate.h"
#include "base/callback_forward.h"
#include "components/favicon_base/favicon_callback.h"
#include "components/services/app_service/public/mojom/app_service.mojom.h"

namespace base {
class CancelableTaskTracker;
}  // namespace base

class ChromeShellDelegate : public ash::ShellDelegate {
 public:
  ChromeShellDelegate();

  ChromeShellDelegate(const ChromeShellDelegate&) = delete;
  ChromeShellDelegate& operator=(const ChromeShellDelegate&) = delete;

  ~ChromeShellDelegate() override;

  // ash::ShellDelegate:
  bool CanShowWindowForUser(const aura::Window* window) const override;
  std::unique_ptr<ash::CaptureModeDelegate> CreateCaptureModeDelegate()
      const override;
  ash::AccessibilityDelegate* CreateAccessibilityDelegate() override;
  std::unique_ptr<ash::BackGestureContextualNudgeDelegate>
  CreateBackGestureContextualNudgeDelegate(
      ash::BackGestureContextualNudgeController* controller) override;
  void OpenKeyboardShortcutHelpPage() const override;
  bool CanGoBack(gfx::NativeWindow window) const override;
  void SetTabScrubberEnabled(bool enabled) override;
  bool AllowDefaultTouchActions(gfx::NativeWindow window) override;
  bool ShouldWaitForTouchPressAck(gfx::NativeWindow window) override;
  bool IsTabDrag(const ui::OSExchangeData& drop_data) override;
  int GetBrowserWebUITabStripHeight() override;
  void BindBluetoothSystemFactory(
      mojo::PendingReceiver<device::mojom::BluetoothSystemFactory> receiver)
      override;
  void BindFingerprint(
      mojo::PendingReceiver<device::mojom::Fingerprint> receiver) override;
  void BindMultiDeviceSetup(
      mojo::PendingReceiver<
          chromeos::multidevice_setup::mojom::MultiDeviceSetup> receiver)
      override;
  media_session::MediaSessionService* GetMediaSessionService() override;
  std::unique_ptr<ash::NearbyShareDelegate> CreateNearbyShareDelegate(
      ash::NearbyShareController* controller) const override;
  bool IsSessionRestoreInProgress() const override;
  void SetUpEnvironmentForLockedFullscreen(bool locked) override;
  bool IsUiDevToolsStarted() const override;
  void StartUiDevTools() override;
  void StopUiDevTools() override;
  int GetUiDevToolsPort() const override;
  bool IsLoggingRedirectDisabled() const override;
  base::FilePath GetPrimaryUserDownloadsFolder() const override;
  void OpenFeedbackPageForPersistentDesksBar() override;
  std::unique_ptr<app_restore::AppLaunchInfo> GetAppLaunchDataForDeskTemplate(
      aura::Window* window) const override;
  desks_storage::DeskModel* GetDeskModel() override;
  void GetFaviconForUrl(const std::string& page_url,
                        int desired_icon_size,
                        favicon_base::FaviconRawBitmapCallback callback,
                        base::CancelableTaskTracker* tracker) const override;
  void GetIconForAppId(
      const std::string& app_id,
      int desired_icon_size,
      base::OnceCallback<void(apps::mojom::IconValuePtr icon_value)> callback)
      const override;
  void LaunchAppsFromTemplate(
      std::unique_ptr<ash::DeskTemplate> desk_template) override;
  bool IsWindowSupportedForDeskTemplate(aura::Window* window) const override;
  static void SetDisableLoggingRedirectForTesting(bool value);
  static void ResetDisableLoggingRedirectForTesting();
};

#endif  // CHROME_BROWSER_UI_ASH_CHROME_SHELL_DELEGATE_H_
