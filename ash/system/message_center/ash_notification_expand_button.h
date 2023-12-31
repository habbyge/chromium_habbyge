// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_MESSAGE_CENTER_ASH_NOTIFICATION_EXPAND_BUTTON_H_
#define ASH_SYSTEM_MESSAGE_CENTER_ASH_NOTIFICATION_EXPAND_BUTTON_H_

#include "ui/views/controls/button/button.h"
#include "ui/views/metadata/view_factory.h"

namespace views {
class Label;
class ImageView;
}  // namespace views

namespace ash {

// Customized expand button for ash notification view. Used for grouped as
// well as singular notifications.
class AshNotificationExpandButton : public views::Button {
 public:
  METADATA_HEADER(AshNotificationExpandButton);
  explicit AshNotificationExpandButton(
      PressedCallback callback = PressedCallback());
  AshNotificationExpandButton(const AshNotificationExpandButton&) = delete;
  AshNotificationExpandButton& operator=(const AshNotificationExpandButton&) =
      delete;
  ~AshNotificationExpandButton() override;

  // Change the expanded state. The icon will change.
  void SetExpanded(bool expanded);

  // Whether the label displaying the number of notifications in a grouped
  // notification needs to be displayed.
  bool ShouldShowLabel() const;

  // Update the count of total grouped notifications in the parent view and
  // update the text for the label accordingly.
  void UpdateGroupedNotificationsCount(int count);

  // Generate the icons used for chevron in the expanded and collapsed state.
  void UpdateIcons();

  // views::Button:
  gfx::Size CalculatePreferredSize() const override;
  void OnThemeChanged() override;

  views::Label* label_for_test() { return label_; }

 private:
  // Owned by views hierarchy.
  views::Label* label_;
  views::ImageView* image_;

  // Cached icons used to display the chevron in the button.
  gfx::ImageSkia expanded_image_;
  gfx::ImageSkia collapsed_image_;

  // Total number of grouped child notifications in this button's parent view.
  int total_grouped_notifications_ = 0;

  // The expand state of the button.
  bool expanded_ = false;
};
BEGIN_VIEW_BUILDER(/*no export*/, AshNotificationExpandButton, views::Button)
END_VIEW_BUILDER

}  // namespace ash

DEFINE_VIEW_BUILDER(/* no export */, ash::AshNotificationExpandButton)

#endif  // ASH_SYSTEM_MESSAGE_CENTER_ASH_NOTIFICATION_EXPAND_BUTTON_H_
