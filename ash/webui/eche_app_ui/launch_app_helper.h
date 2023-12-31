// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WEBUI_ECHE_APP_UI_LAUNCH_APP_HELPER_H_
#define ASH_WEBUI_ECHE_APP_UI_LAUNCH_APP_HELPER_H_

// TODO(https://crbug.com/1164001): move to forward declaration.
#include "ash/components/phonehub/phone_hub_manager.h"
#include "ash/webui/eche_app_ui/mojom/eche_app.mojom.h"
#include "base/callback.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/abseil-cpp/absl/types/variant.h"

namespace ash {
namespace eche_app {

// A helper class for launching/closing the app or show a notification.
class LaunchAppHelper {
 public:
  class NotificationInfo {
   public:
    // Enum representing the notification was generated from where.
    enum Category {
      // The notification was generated from native layer,
      kNative,
      // THe notification was generated from webUI,
      kWebUI,
    };

    // Enum representing potential type for the notification.
    enum class NotificationType {
      // Remind users to enable screen lock.
      kScreenLock = 0,
    };

    NotificationInfo(
        Category category,
        absl::variant<NotificationType, mojom::WebNotificationType> type);
    ~NotificationInfo();

    Category category() const { return category_; }
    absl::variant<NotificationType, mojom::WebNotificationType> type() const {
      return type_;
    }

   private:
    Category category_;
    absl::variant<NotificationType, mojom::WebNotificationType> type_;
  };

  using LaunchNotificationFunction = base::RepeatingCallback<void(
      const absl::optional<std::u16string>& title,
      const absl::optional<std::u16string>& message,
      std::unique_ptr<NotificationInfo> info)>;

  using LaunchEcheAppFunction = base::RepeatingCallback<void(
      const absl::optional<int64_t>& notification_id,
      const std::string& package_name,
      const std::u16string& visible_name,
      const absl::optional<int64_t>& user_id)>;

  using CloseEcheAppFunction = base::RepeatingCallback<void()>;

  LaunchAppHelper(phonehub::PhoneHubManager* phone_hub_manager,
                  LaunchEcheAppFunction launch_eche_app_function,
                  CloseEcheAppFunction close_eche_app_function,
                  LaunchNotificationFunction launch_notification_function);
  virtual ~LaunchAppHelper();

  LaunchAppHelper(const LaunchAppHelper&) = delete;
  LaunchAppHelper& operator=(const LaunchAppHelper&) = delete;

  // Exposed virtual for testing.
  virtual bool IsAppLaunchAllowed() const;

  // Exposed virtual for testing.
  // The notification could be generated from webUI or native layer, for the
  // latter it doesn't carry title and message.
  virtual void ShowNotification(const absl::optional<std::u16string>& title,
                                const absl::optional<std::u16string>& message,
                                std::unique_ptr<NotificationInfo> info) const;

  void LaunchEcheApp(absl::optional<int64_t> notification_id,
                     const std::string& package_name,
                     const std::u16string& visible_name,
                     const absl::optional<int64_t>& user_id) const;

  void CloseEcheApp() const;

 private:
  phonehub::PhoneHubManager* phone_hub_manager_;
  LaunchEcheAppFunction launch_eche_app_function_;
  CloseEcheAppFunction close_eche_app_function_;
  LaunchNotificationFunction launch_notification_function_;
};

}  // namespace eche_app
}  // namespace ash

// TODO(https://crbug.com/1164001): remove when the migration is finished.
namespace chromeos {
namespace eche_app {
using ::ash::eche_app::LaunchAppHelper;
}  // namespace eche_app
}  // namespace chromeos

#endif  // ASH_WEBUI_ECHE_APP_UI_LAUNCH_APP_HELPER_H_
