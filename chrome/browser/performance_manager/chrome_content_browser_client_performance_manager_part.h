// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PERFORMANCE_MANAGER_CHROME_CONTENT_BROWSER_CLIENT_PERFORMANCE_MANAGER_PART_H_
#define CHROME_BROWSER_PERFORMANCE_MANAGER_CHROME_CONTENT_BROWSER_CLIENT_PERFORMANCE_MANAGER_PART_H_

#include "base/compiler_specific.h"
#include "chrome/browser/chrome_content_browser_client_parts.h"

// Allows tracking RenderProcessHost lifetime and proffering the Performance
// Manager interface to new renderers.
class ChromeContentBrowserClientPerformanceManagerPart
    : public ChromeContentBrowserClientParts {
 public:
  ChromeContentBrowserClientPerformanceManagerPart();

  ChromeContentBrowserClientPerformanceManagerPart(
      const ChromeContentBrowserClientPerformanceManagerPart&) = delete;
  ChromeContentBrowserClientPerformanceManagerPart& operator=(
      const ChromeContentBrowserClientPerformanceManagerPart&) = delete;

  ~ChromeContentBrowserClientPerformanceManagerPart() override;

  // ChromeContentBrowserClientParts overrides.
  void ExposeInterfacesToRenderer(
      service_manager::BinderRegistry* registry,
      blink::AssociatedInterfaceRegistry* associated_registry,
      content::RenderProcessHost* render_process_host) override;
};

#endif  // CHROME_BROWSER_PERFORMANCE_MANAGER_CHROME_CONTENT_BROWSER_CLIENT_PERFORMANCE_MANAGER_PART_H_
