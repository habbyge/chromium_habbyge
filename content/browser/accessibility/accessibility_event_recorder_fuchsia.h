// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ACCESSIBILITY_ACCESSIBILITY_EVENT_RECORDER_FUCHSIA_H_
#define CONTENT_BROWSER_ACCESSIBILITY_ACCESSIBILITY_EVENT_RECORDER_FUCHSIA_H_

#include "content/browser/accessibility/accessibility_event_recorder.h"
#include "content/common/content_export.h"

namespace content {

class CONTENT_EXPORT AccessibilityEventRecorderFuchsia
    : public AccessibilityEventRecorder {
 public:
  AccessibilityEventRecorderFuchsia(BrowserAccessibilityManager* manager,
                                    base::ProcessId pid,
                                    const ui::AXTreeSelector& selector);

  AccessibilityEventRecorderFuchsia(const AccessibilityEventRecorderFuchsia&) =
      delete;
  AccessibilityEventRecorderFuchsia& operator=(
      const AccessibilityEventRecorderFuchsia&) = delete;

  ~AccessibilityEventRecorderFuchsia() override;

 private:
  static AccessibilityEventRecorderFuchsia* instance_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_ACCESSIBILITY_ACCESSIBILITY_EVENT_RECORDER_FUCHSIA_H_
