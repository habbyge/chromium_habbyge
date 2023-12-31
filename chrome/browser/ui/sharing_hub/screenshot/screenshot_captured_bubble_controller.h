// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_SHARING_HUB_SCREENSHOT_SCREENSHOT_CAPTURED_BUBBLE_CONTROLLER_H_
#define CHROME_BROWSER_UI_SHARING_HUB_SCREENSHOT_SCREENSHOT_CAPTURED_BUBBLE_CONTROLLER_H_

#include "content/public/browser/web_contents_user_data.h"

class Browser;

namespace content {
class WebContents;
}

namespace image_editor {
struct ScreenshotCaptureResult;
class ScreenshotFlow;
}  // namespace image_editor

namespace sharing_hub {

// Controller component of the Screenshot capture dialog bubble.
// Responsible for showing and hiding an owned bubble.
class ScreenshotCapturedBubbleController
    : public content::WebContentsUserData<ScreenshotCapturedBubbleController> {
 public:
  ~ScreenshotCapturedBubbleController() override;

  static ScreenshotCapturedBubbleController* Get(
      content::WebContents* web_contents);

  // Displays the Screenshot capture bubble.
  void ShowBubble(const image_editor::ScreenshotCaptureResult& image);

  // Hides the Screenshot capture bubble.
  void HideBubble();

  // Start the capture flow.
  void Capture(Browser* browser);

  // Handler for when the bubble is dismissed.
  void OnBubbleClosed();

 protected:
  explicit ScreenshotCapturedBubbleController(
      content::WebContents* web_contents);

 private:
  ScreenshotCapturedBubbleController();

  friend class content::WebContentsUserData<ScreenshotCapturedBubbleController>;

  // The web_contents associated with this controller.
  content::WebContents* web_contents_;

  std::unique_ptr<image_editor::ScreenshotFlow> screenshot_flow_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace sharing_hub

#endif  // CHROME_BROWSER_UI_SHARING_HUB_SCREENSHOT_SCREENSHOT_CAPTURED_BUBBLE_CONTROLLER_H_
