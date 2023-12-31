// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_WEB_APPLICATIONS_MEDIA_APP_MEDIA_WEB_APP_INFO_H_
#define CHROME_BROWSER_ASH_WEB_APPLICATIONS_MEDIA_APP_MEDIA_WEB_APP_INFO_H_

#include "chrome/browser/web_applications/system_web_apps/system_web_app_delegate.h"
#include "chrome/browser/web_applications/system_web_apps/system_web_app_types.h"

struct WebApplicationInfo;

class MediaSystemAppDelegate : public web_app::SystemWebAppDelegate {
 public:
  explicit MediaSystemAppDelegate(Profile* profile);

  // web_app::SystemWebAppDelegate overrides:
  std::unique_ptr<WebApplicationInfo> GetWebAppInfo() const override;
  bool ShouldIncludeLaunchDirectory() const override;
  bool ShouldShowInLauncher() const override;
  bool ShouldCaptureNavigations() const override;
  bool ShouldShowInSearch() const override;
  bool ShouldShowNewWindowMenuOption() const override;
  bool ShouldReuseExistingWindow() const override;
};

// Return a WebApplicationInfo used to install the app.
std::unique_ptr<WebApplicationInfo> CreateWebAppInfoForMediaWebApp();

#endif  // CHROME_BROWSER_ASH_WEB_APPLICATIONS_MEDIA_APP_MEDIA_WEB_APP_INFO_H_
