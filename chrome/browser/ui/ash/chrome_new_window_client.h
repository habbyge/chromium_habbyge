// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_CHROME_NEW_WINDOW_CLIENT_H_
#define CHROME_BROWSER_UI_ASH_CHROME_NEW_WINDOW_CLIENT_H_

#include <map>
#include <memory>
#include <string>

#include "ash/public/cpp/new_window_delegate.h"
#include "components/arc/intent_helper/control_camera_app_delegate.h"
#include "components/arc/intent_helper/open_url_delegate.h"
#include "url/gurl.h"

namespace arc {
namespace mojom {
enum class ChromePage;
}
}  // namespace arc

namespace content {
class WebContents;
}

// Handles opening new tabs and windows on behalf of ash (over mojo) and the
// ARC bridge (via a delegate in the browser process).
class ChromeNewWindowClient : public ash::NewWindowDelegate,
                              public arc::OpenUrlDelegate,
                              public arc::ControlCameraAppDelegate {
 public:
  ChromeNewWindowClient();

  ChromeNewWindowClient(const ChromeNewWindowClient&) = delete;
  ChromeNewWindowClient& operator=(const ChromeNewWindowClient&) = delete;

  ~ChromeNewWindowClient() override;

  static ChromeNewWindowClient* Get();

  // Overridden from ash::NewWindowDelegate:
  void NewTab() override;
  void NewWindow(bool incognito, bool should_trigger_session_restore) override;
  void NewWindowForDetachingTab(
      aura::Window* source_window,
      const ui::OSExchangeData& drop_data,
      NewWindowForDetachingTabCallback closure) override;
  void OpenUrl(const GURL& url, bool from_user_interaction) override;
  void OpenCalculator() override;
  void OpenFileManager() override;
  void OpenDownloadsFolder() override;
  void OpenCrosh() override;
  void OpenGetHelp() override;
  void RestoreTab() override;
  void ShowKeyboardShortcutViewer() override;
  void ShowTaskManager() override;
  void OpenDiagnostics() override;
  void OpenFeedbackPage(FeedbackSource source,
                        const std::string& description_template) override;

  // arc::OpenUrlDelegate:
  void OpenUrlFromArc(const GURL& url) override;
  void OpenWebAppFromArc(const GURL& url) override;
  void OpenArcCustomTab(
      const GURL& url,
      int32_t task_id,
      arc::mojom::IntentHelperHost::OnOpenCustomTabCallback callback) override;
  void OpenChromePageFromArc(arc::mojom::ChromePage page) override;
  void OpenAppWithIntent(const GURL& start_url,
                         arc::mojom::LaunchIntentPtr intent) override;

  // arc::ControlCameraAppDelegate:
  void LaunchCameraApp(const std::string& queries, int32_t task_id) override;
  void CloseCameraApp() override;
  bool IsCameraAppEnabled() override;

 private:
  class TabRestoreHelper;

  // Opens a URL in a new tab. Returns the WebContents for the tab that
  // opened the URL. If the URL is for a chrome://settings page, opens settings
  // in a new window and returns null. If the |from_user_interaction| is true
  // then the page will load with a user activation. This means it will be able
  // to autoplay media without restriction.
  content::WebContents* OpenUrlImpl(const GURL& url,
                                    bool from_user_interaction);

  std::unique_ptr<TabRestoreHelper> tab_restore_helper_;

  const std::map<arc::mojom::ChromePage, std::string> os_settings_pages_;

  const std::map<arc::mojom::ChromePage, std::string> browser_settings_pages_;

  const std::map<arc::mojom::ChromePage, std::string> about_pages_;
};

#endif  // CHROME_BROWSER_UI_ASH_CHROME_NEW_WINDOW_CLIENT_H_
