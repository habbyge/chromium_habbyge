// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_SHELL_JAVASCRIPT_DIALOG_H_
#define CONTENT_SHELL_BROWSER_SHELL_JAVASCRIPT_DIALOG_H_

#include "base/callback.h"
#include "build/build_config.h"
#include "content/public/browser/javascript_dialog_manager.h"

#if defined(OS_MAC)
#if __OBJC__
@class ShellJavaScriptDialogHelper;
#else
class ShellJavaScriptDialogHelper;
#endif  // __OBJC__
#endif  // defined(OS_MAC)

namespace content {

class ShellJavaScriptDialogManager;

class ShellJavaScriptDialog {
 public:
  ShellJavaScriptDialog(ShellJavaScriptDialogManager* manager,
                        gfx::NativeWindow parent_window,
                        JavaScriptDialogType dialog_type,
                        const std::u16string& message_text,
                        const std::u16string& default_prompt_text,
                        JavaScriptDialogManager::DialogClosedCallback callback);

  ShellJavaScriptDialog(const ShellJavaScriptDialog&) = delete;
  ShellJavaScriptDialog& operator=(const ShellJavaScriptDialog&) = delete;

  ~ShellJavaScriptDialog();

  // Called to cancel a dialog mid-flight.
  void Cancel();

 private:
#if defined(OS_MAC)
  ShellJavaScriptDialogHelper* helper_;  // owned
#elif defined(OS_WIN)
  JavaScriptDialogManager::DialogClosedCallback callback_;
  ShellJavaScriptDialogManager* manager_;
  JavaScriptDialogType dialog_type_;
  HWND dialog_win_;
  std::u16string message_text_;
  std::u16string default_prompt_text_;
  static INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wparam,
                                     LPARAM lparam);
#endif
};

}  // namespace content

#endif  // CONTENT_SHELL_BROWSER_SHELL_JAVASCRIPT_DIALOG_H_
