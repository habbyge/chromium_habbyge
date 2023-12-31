// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_COMPONENTS_PHONEHUB_NOTIFICATION_INTERACTION_HANDLER_IMPL_H_
#define ASH_COMPONENTS_PHONEHUB_NOTIFICATION_INTERACTION_HANDLER_IMPL_H_

#include <stdint.h>
#include "ash/components/phonehub/notification.h"
#include "ash/components/phonehub/notification_interaction_handler.h"

namespace chromeos {
namespace phonehub {

class NotificationInteractionHandlerImpl
    : public NotificationInteractionHandler {
 public:
  NotificationInteractionHandlerImpl();
  ~NotificationInteractionHandlerImpl() override;

 private:
  void HandleNotificationClicked(
      int64_t notification_id,
      const Notification::AppMetadata& app_metadata) override;
};

}  // namespace phonehub
}  // namespace chromeos

#endif  // ASH_COMPONENTS_PHONEHUB_NOTIFICATION_INTERACTION_HANDLER_IMPL_H_
