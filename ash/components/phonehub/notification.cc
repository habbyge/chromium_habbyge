// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/components/phonehub/notification.h"

#include <tuple>

#include "base/containers/flat_map.h"
#include "base/logging.h"

namespace chromeos {
namespace phonehub {

Notification::AppMetadata::AppMetadata(const std::u16string& visible_app_name,
                                       const std::string& package_name,
                                       const gfx::Image& icon,
                                       int64_t user_id)
    : visible_app_name(visible_app_name),
      package_name(package_name),
      icon(icon),
      user_id(user_id) {}

Notification::AppMetadata::AppMetadata(const AppMetadata& other) = default;

Notification::AppMetadata& Notification::AppMetadata::operator=(
    const AppMetadata& other) = default;

bool Notification::AppMetadata::operator==(const AppMetadata& other) const {
  return visible_app_name == other.visible_app_name &&
         package_name == other.package_name && icon == other.icon &&
         user_id == other.user_id;
}

bool Notification::AppMetadata::operator!=(const AppMetadata& other) const {
  return !(*this == other);
}

Notification::Notification(
    int64_t id,
    const AppMetadata& app_metadata,
    const base::Time& timestamp,
    Importance importance,
    Notification::Category category,
    const base::flat_map<Notification::ActionType, int64_t>& action_id_map,
    InteractionBehavior interaction_behavior,
    const absl::optional<std::u16string>& title,
    const absl::optional<std::u16string>& text_content,
    const absl::optional<gfx::Image>& shared_image,
    const absl::optional<gfx::Image>& contact_image)
    : id_(id),
      app_metadata_(app_metadata),
      timestamp_(timestamp),
      importance_(importance),
      category_(category),
      action_id_map_(action_id_map),
      interaction_behavior_(interaction_behavior),
      title_(title),
      text_content_(text_content),
      shared_image_(shared_image),
      contact_image_(contact_image) {}

Notification::Notification(const Notification& other) = default;

Notification::~Notification() = default;

bool Notification::operator<(const Notification& other) const {
  return std::tie(timestamp_, id_) < std::tie(other.timestamp_, other.id_);
}

bool Notification::operator==(const Notification& other) const {
  return id_ == other.id_ && app_metadata_ == other.app_metadata_ &&
         timestamp_ == other.timestamp_ && importance_ == other.importance_ &&
         category_ == other.category_ &&
         action_id_map_ == other.action_id_map_ &&
         interaction_behavior_ == other.interaction_behavior_ &&
         title_ == other.title_ && text_content_ == other.text_content_ &&
         shared_image_ == other.shared_image_ &&
         contact_image_ == other.contact_image_;
}

bool Notification::operator!=(const Notification& other) const {
  return !(*this == other);
}

std::ostream& operator<<(std::ostream& stream,
                         const Notification::AppMetadata& app_metadata) {
  stream << "{VisibleAppName: \"" << app_metadata.visible_app_name << "\", "
         << "PackageName: \"" << app_metadata.package_name << "\"}";
  return stream;
}

std::ostream& operator<<(std::ostream& stream,
                         Notification::Importance importance) {
  switch (importance) {
    case Notification::Importance::kUnspecified:
      stream << "[Unspecified]";
      break;
    case Notification::Importance::kNone:
      stream << "[None]";
      break;
    case Notification::Importance::kMin:
      stream << "[Min]";
      break;
    case Notification::Importance::kLow:
      stream << "[Low]";
      break;
    case Notification::Importance::kDefault:
      stream << "[Default]";
      break;
    case Notification::Importance::kHigh:
      stream << "[High]";
      break;
  }
  return stream;
}

std::ostream& operator<<(std::ostream& stream,
                         Notification::InteractionBehavior behavior) {
  switch (behavior) {
    case Notification::InteractionBehavior::kNone:
      stream << "[None]";
      break;
    case Notification::InteractionBehavior::kOpenable:
      stream << "[Openable]";
      break;
  }
  return stream;
}

std::ostream& operator<<(std::ostream& stream,
                         Notification::Category catetory) {
  switch (catetory) {
    case Notification::Category::kNone:
      stream << "[None]";
      break;
    case Notification::Category::kConversation:
      stream << "[Conversation]";
      break;
    case Notification::Category::kIncomingCall:
      stream << "[IncomingCall]";
      break;
    case Notification::Category::kOngoingCall:
      stream << "[OngoingCall]";
      break;
    case Notification::Category::kScreenCall:
      stream << "[ScreenCall]";
      break;
  }
  return stream;
}

std::ostream& operator<<(std::ostream& stream,
                         const Notification& notification) {
  stream << "{Id: " << notification.id() << ", "
         << "App: " << notification.app_metadata() << ", "
         << "Timestamp: " << notification.timestamp() << ", "
         << "Importance: " << notification.importance() << ", "
         << "Category: " << notification.category() << ", "
         << "InteractionBehavior: " << notification.interaction_behavior()
         << "}";
  return stream;
}

}  // namespace phonehub
}  // namespace chromeos
