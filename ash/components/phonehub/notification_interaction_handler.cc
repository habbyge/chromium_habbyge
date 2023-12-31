// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/components/phonehub/notification_interaction_handler.h"
#include "ash/components/phonehub/notification.h"

namespace chromeos {
namespace phonehub {

NotificationInteractionHandler::NotificationInteractionHandler() = default;
NotificationInteractionHandler::~NotificationInteractionHandler() = default;

void NotificationInteractionHandler::AddNotificationClickHandler(
    NotificationClickHandler* handler) {
  handler_list_.AddObserver(handler);
}

void NotificationInteractionHandler::RemoveNotificationClickHandler(
    NotificationClickHandler* handler) {
  handler_list_.RemoveObserver(handler);
}

void NotificationInteractionHandler::NotifyNotificationClicked(
    int64_t notification_id,
    const Notification::AppMetadata& app_metadata) {
  for (auto& handler : handler_list_)
    handler.HandleNotificationClick(notification_id, app_metadata);
}

}  // namespace phonehub
}  // namespace chromeos