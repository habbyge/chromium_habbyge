// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_WEBAPPS_BROWSER_ANDROID_INSTALLABLE_INSTALLABLE_AMBIENT_BADGE_MESSAGE_CONTROLLER_H_
#define COMPONENTS_WEBAPPS_BROWSER_ANDROID_INSTALLABLE_INSTALLABLE_AMBIENT_BADGE_MESSAGE_CONTROLLER_H_

#include <memory>
#include <string>

#include "components/messages/android/message_enums.h"
#include "components/messages/android/message_wrapper.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "url/gurl.h"

namespace content {
class WebContents;
}  // namespace content

namespace webapps {

class InstallableAmbientBadgeClient;

// Message controller for a message shown to users when they visit a
// progressive web app. Tapping primary button triggers the add to home screen
// flow.
class InstallableAmbientBadgeMessageController {
 public:
  explicit InstallableAmbientBadgeMessageController(
      InstallableAmbientBadgeClient* client);
  ~InstallableAmbientBadgeMessageController();

  // Returns true if the message was enqueued with EnqueueMessage() method, but
  // wasn't dismissed yet.
  bool IsMessageEnqueued();

  // Enqueues a message to be displayed on the screen. Typically there are no
  // other messages on the screen and enqueued message will get displayed
  // immediately.
  void EnqueueMessage(content::WebContents* web_contents,
                      const std::u16string& app_name,
                      const SkBitmap& icon,
                      const GURL& start_url);

  // Dismisses displayed message. This method is safe to call  when there is no
  // displayed message.
  void DismissMessage();

 private:
  void HandleInstallButtonClicked();
  void HandleMessageDismissed(messages::DismissReason dismiss_reason);

  InstallableAmbientBadgeClient* client_;
  std::unique_ptr<messages::MessageWrapper> message_;
};

}  // namespace webapps

#endif  // COMPONENTS_WEBAPPS_BROWSER_ANDROID_INSTALLABLE_INSTALLABLE_AMBIENT_BADGE_MESSAGE_CONTROLLER_H_
