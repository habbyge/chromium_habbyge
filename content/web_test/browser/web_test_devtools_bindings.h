// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_WEB_TEST_BROWSER_WEB_TEST_DEVTOOLS_BINDINGS_H_
#define CONTENT_WEB_TEST_BROWSER_WEB_TEST_DEVTOOLS_BINDINGS_H_

#include "base/compiler_specific.h"
#include "content/shell/browser/shell_devtools_frontend.h"

namespace content {

class WebContents;

class WebTestDevToolsBindings : public ShellDevToolsBindings {
 public:
  static GURL MapTestURLIfNeeded(const GURL& test_url, bool* is_devtools_test);

  WebTestDevToolsBindings(WebContents* devtools_contents,
                          WebContents* inspected_contents,
                          const GURL& frontend_url);

  void Attach() override;

  WebTestDevToolsBindings(const WebTestDevToolsBindings&) = delete;
  WebTestDevToolsBindings& operator=(const WebTestDevToolsBindings&) = delete;

  ~WebTestDevToolsBindings() override;

 private:
  class SecondaryObserver;

  // WebContentsObserver implementation.
  void DocumentAvailableInMainFrame(
      RenderFrameHost* render_frame_host) override;

  void NavigateDevToolsFrontend();

  GURL frontend_url_;
  std::unique_ptr<SecondaryObserver> secondary_observer_;
};

}  // namespace content

#endif  // CONTENT_WEB_TEST_BROWSER_WEB_TEST_DEVTOOLS_BINDINGS_H_
