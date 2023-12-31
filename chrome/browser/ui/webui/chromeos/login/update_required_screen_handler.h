// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_UPDATE_REQUIRED_SCREEN_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_UPDATE_REQUIRED_SCREEN_HANDLER_H_

#include <string>

#include "base/compiler_specific.h"
#include "chrome/browser/ui/webui/chromeos/login/base_screen_handler.h"

namespace ash {
class UpdateRequiredScreen;
}

namespace chromeos {

// Interface for dependency injection between UpdateRequiredScreen and its
// WebUI representation.

class UpdateRequiredView {
 public:
  enum UIState {
    UPDATE_REQUIRED_MESSAGE = 0,   // 'System update required' message.
    UPDATE_PROCESS,                // Update is going on.
    UPDATE_NEED_PERMISSION,        // Need user's permission to proceed.
    UPDATE_COMPLETED_NEED_REBOOT,  // Update successful, manual reboot is
                                   // needed.
    UPDATE_ERROR,                  // An error has occurred.
    EOL_REACHED,                   // End of Life reached message.
    UPDATE_NO_NETWORK              // No network available to update
  };

  constexpr static StaticOobeScreenId kScreenId{"update-required"};

  virtual ~UpdateRequiredView() {}

  // Shows the contents of the screen.
  virtual void Show() = 0;

  // Hides the contents of the screen.
  virtual void Hide() = 0;

  // Binds `screen` to the view.
  virtual void Bind(ash::UpdateRequiredScreen* screen) = 0;

  // Unbinds the screen from the view.
  virtual void Unbind() = 0;

  // Is device connected to some network?
  virtual void SetIsConnected(bool connected) = 0;
  // Is progress unavailable (e.g. we are checking for updates)?
  virtual void SetUpdateProgressUnavailable(bool unavailable) = 0;
  // Set progress percentage.
  virtual void SetUpdateProgressValue(int progress) = 0;
  // Set progress message (like "Verifying").
  virtual void SetUpdateProgressMessage(const std::u16string& message) = 0;
  // Set the visibility of the estimated time left.
  virtual void SetEstimatedTimeLeftVisible(bool visible) = 0;
  // Set the estimated time left, in seconds.
  virtual void SetEstimatedTimeLeft(int seconds_left) = 0;
  // Set the UI state of the screen.
  virtual void SetUIState(UpdateRequiredView::UIState ui_state) = 0;
  // Set enterprise and device name to be used in strings in the UI.
  virtual void SetEnterpriseAndDeviceName(const std::string& enterpriseDomain,
                                          const std::u16string& deviceName) = 0;
  virtual void SetEolMessage(const std::string& eolMessage) = 0;
  virtual void SetIsUserDataPresent(bool deleted) = 0;
};

class UpdateRequiredScreenHandler : public UpdateRequiredView,
                                    public BaseScreenHandler {
 public:
  using TView = UpdateRequiredView;

  explicit UpdateRequiredScreenHandler(JSCallsContainer* js_calls_container);

  UpdateRequiredScreenHandler(const UpdateRequiredScreenHandler&) = delete;
  UpdateRequiredScreenHandler& operator=(const UpdateRequiredScreenHandler&) =
      delete;

  ~UpdateRequiredScreenHandler() override;

 private:
  void Show() override;
  void Hide() override;
  void Bind(ash::UpdateRequiredScreen* screen) override;
  void Unbind() override;

  void SetIsConnected(bool connected) override;
  void SetUpdateProgressUnavailable(bool unavailable) override;
  void SetUpdateProgressValue(int progress) override;
  void SetUpdateProgressMessage(const std::u16string& message) override;
  void SetEstimatedTimeLeftVisible(bool visible) override;
  void SetEstimatedTimeLeft(int seconds_left) override;
  void SetUIState(UpdateRequiredView::UIState ui_state) override;
  void SetEnterpriseAndDeviceName(const std::string& enterpriseDomain,
                                  const std::u16string& deviceName) override;
  void SetEolMessage(const std::string& eolMessage) override;
  void SetIsUserDataPresent(bool data_present) override;

  // BaseScreenHandler:
  void DeclareLocalizedValues(
      ::login::LocalizedValuesBuilder* builder) override;
  void Initialize() override;

  ash::UpdateRequiredScreen* screen_ = nullptr;

  // If true, Initialize() will call Show().
  bool show_on_init_ = false;

  // The domain name for which update required screen is being shown.
  std::string domain_;
};

}  // namespace chromeos

// TODO(https://crbug.com/1164001): remove after the //chrome/browser/chromeos
// source migration is finished.
namespace ash {
using ::chromeos::UpdateRequiredScreenHandler;
using ::chromeos::UpdateRequiredView;
}

#endif  // CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_UPDATE_REQUIRED_SCREEN_HANDLER_H_
