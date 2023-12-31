// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_FIND_IN_PAGE_FIND_TAB_HELPER_H_
#define IOS_CHROME_BROWSER_FIND_IN_PAGE_FIND_TAB_HELPER_H_

#include <Foundation/Foundation.h>

#include "base/ios/block_types.h"
#include "base/macros.h"
#include "base/scoped_observation.h"
#include "ios/web/public/web_state_observer.h"
#import "ios/web/public/web_state_user_data.h"

@class FindInPageController;
@class FindInPageModel;
@protocol FindInPageResponseDelegate;

// Adds support for the "Find in page" feature.
class FindTabHelper : public web::WebStateObserver,
                      public web::WebStateUserData<FindTabHelper> {
 public:
  FindTabHelper(const FindTabHelper&) = delete;
  FindTabHelper& operator=(const FindTabHelper&) = delete;

  ~FindTabHelper() override;

  enum FindDirection {
    FORWARD,
    REVERSE,
  };

  // Sets the FindInPageResponseDelegate delegate to send responses to
  // StartFinding(), ContinueFinding(), and StopFinding().
  void SetResponseDelegate(id<FindInPageResponseDelegate> response_delegate);

  // Starts an asynchronous Find operation that will call the given completion
  // handler with results.  Highlights matches on the current page.  Always
  // searches in the FORWARD direction.
  void StartFinding(NSString* search_string);

  // Runs an asynchronous Find operation that will call the given completion
  // handler with results.  Highlights matches on the current page.  Uses the
  // previously remembered search string and searches in the given |direction|.
  void ContinueFinding(FindDirection direction);

  // Stops any running find operations and runs the given completion block.
  // Removes any highlighting from the current page.
  void StopFinding();

  // Returns the FindInPageModel that contains the latest find results.
  FindInPageModel* GetFindResult() const;

  // Returns true if the currently loaded page supports Find in Page.
  bool CurrentPageSupportsFindInPage() const;

  // Returns true if the Find in Page UI is currently visible.
  bool IsFindUIActive() const;

  // Marks the Find in Page UI as visible or not.  This method does not directly
  // show or hide the UI.  It simply acts as a marker for whether or not the UI
  // is visible.
  void SetFindUIActive(bool active);

  // Saves the current find text to persistent storage.
  void PersistSearchTerm();

  // Restores the current find text from persistent storage.
  void RestoreSearchTerm();

 private:
  friend class FindTabHelperTest;
  friend class web::WebStateUserData<FindTabHelper>;

  // Private constructor used by CreateForWebState().
  FindTabHelper(web::WebState* web_state);

  // Create the FindInPageController for |web_state|. Only called if/when
  // the WebState is realized.
  void CreateFindInPageController(web::WebState* web_state);

  // web::WebStateObserver.
  void WebStateRealized(web::WebState* web_state) override;
  void WebStateDestroyed(web::WebState* web_state) override;
  void DidFinishNavigation(web::WebState* web_state,
                           web::NavigationContext* navigation_context) override;

  // The ObjC find in page controller (nil if the WebState is not realized).
  FindInPageController* controller_ = nil;

  // The delegate to register with FindInPageController when it is created.
  __weak id<FindInPageResponseDelegate> response_delegate_ = nil;

  // Manage the registration of this instance as a WebStateObserver.
  base::ScopedObservation<web::WebState, web::WebStateObserver> observation_{
      this};

  WEB_STATE_USER_DATA_KEY_DECL();
};

#endif  // IOS_CHROME_BROWSER_FIND_IN_PAGE_FIND_TAB_HELPER_H_
