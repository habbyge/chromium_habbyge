// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_FIND_BAR_FIND_BAR_PLATFORM_HELPER_H_
#define CHROME_BROWSER_UI_FIND_BAR_FIND_BAR_PLATFORM_HELPER_H_

#include <memory>
#include <string>

class FindBarController;

// Handles all platform specific operations on behalf of the FindBarController.
class FindBarPlatformHelper {
 public:
  static std::unique_ptr<FindBarPlatformHelper> Create(
      FindBarController* find_bar_controller);

  FindBarPlatformHelper(const FindBarPlatformHelper&) = delete;
  FindBarPlatformHelper& operator=(const FindBarPlatformHelper&) = delete;

  virtual ~FindBarPlatformHelper();

  // Called when the user changes the find text to |text|.
  virtual void OnUserChangedFindText(std::u16string text) = 0;

 protected:
  explicit FindBarPlatformHelper(FindBarController* find_bar_controller);

  // Owns FindBarPlatformHelper.
  FindBarController* const find_bar_controller_;
};

#endif  // CHROME_BROWSER_UI_FIND_BAR_FIND_BAR_PLATFORM_HELPER_H_
