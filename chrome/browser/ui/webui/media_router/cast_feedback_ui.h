// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_MEDIA_ROUTER_CAST_FEEDBACK_UI_H_
#define CHROME_BROWSER_UI_WEBUI_MEDIA_ROUTER_CAST_FEEDBACK_UI_H_

#include "content/public/browser/web_ui_controller.h"

class Profile;

namespace base {
class ListValue;
}

namespace content {
class WebContents;
class WebUI;
}  // namespace content

namespace media_router {

// The main object controlling the Cast feedback
// (chrome://cast-feedback) page.
class CastFeedbackUI : public content::WebUIController {
 public:
  explicit CastFeedbackUI(content::WebUI* web_ui);
  ~CastFeedbackUI() override;

 private:
  void OnCloseMessage(const base::ListValue*);

  Profile* const profile_;
  content::WebContents* const web_contents_;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace media_router

#endif  // CHROME_BROWSER_UI_WEBUI_MEDIA_ROUTER_CAST_FEEDBACK_UI_H_
