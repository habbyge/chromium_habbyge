// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_WEB_STATE_LIST_VIEW_SOURCE_BROWSER_AGENT_H_
#define IOS_CHROME_BROWSER_WEB_STATE_LIST_VIEW_SOURCE_BROWSER_AGENT_H_

#import <Foundation/Foundation.h>

#import "ios/chrome/browser/main/browser_user_data.h"

class Browser;
namespace web {
class WebState;
}

// Browser agent that handles the view-source debugging function. This feature
// isn't designed to be release-quality.
class ViewSourceBrowserAgent : public BrowserUserData<ViewSourceBrowserAgent> {
 public:
  ~ViewSourceBrowserAgent() override;

  // Not copyable or moveable.
  ViewSourceBrowserAgent(const ViewSourceBrowserAgent&) = delete;
  ViewSourceBrowserAgent& operator=(const ViewSourceBrowserAgent&) = delete;

  // Extracts the HTML source for the active web state in the agent's browser
  // and creates a new web state displaying that HTML.
  // Source extraction is asynchronous via javascript; if the active web state
  // is changed in the same runloop after this method is called, strange things
  // might happen.
  void ViewSourceForActiveWebState();

 private:
  friend class BrowserUserData<ViewSourceBrowserAgent>;
  explicit ViewSourceBrowserAgent(Browser* browser);
  BROWSER_USER_DATA_KEY_DECL();

  // Inserts a tab into |browser_| showing the |source| for |web_state|.
  void InsertSourceViewTab(NSString* source, web::WebState* web_state);

  // The browser this agent is associated with.
  Browser* browser_;
};

#endif  // IOS_CHROME_BROWSER_WEB_STATE_LIST_VIEW_SOURCE_BROWSER_AGENT_H_
