// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_CHROME_EXTENSION_CHOOSER_DIALOG_H_
#define CHROME_BROWSER_EXTENSIONS_CHROME_EXTENSION_CHOOSER_DIALOG_H_

namespace content {
class WebContents;
}

namespace permissions {
class ChooserController;
}

class ChromeExtensionChooserDialog {
 public:
  explicit ChromeExtensionChooserDialog(content::WebContents* web_contents)
      : web_contents_(web_contents) {}

  ChromeExtensionChooserDialog(const ChromeExtensionChooserDialog&) = delete;
  ChromeExtensionChooserDialog& operator=(const ChromeExtensionChooserDialog&) =
      delete;

  ~ChromeExtensionChooserDialog() {}

  void ShowDialog(
      std::unique_ptr<permissions::ChooserController> chooser_controller) const;

 private:
  void ShowDialogImpl(
      std::unique_ptr<permissions::ChooserController> chooser_controller) const;

  content::WebContents* web_contents_;
};

#endif  // CHROME_BROWSER_EXTENSIONS_CHROME_EXTENSION_CHOOSER_DIALOG_H_
