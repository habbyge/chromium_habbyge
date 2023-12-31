// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_SHARESHEET_SHARESHEET_BUBBLE_VIEW_DELEGATE_H_
#define CHROME_BROWSER_UI_ASH_SHARESHEET_SHARESHEET_BUBBLE_VIEW_DELEGATE_H_

#include "build/chromeos_buildflags.h"
#include "chrome/browser/sharesheet/sharesheet_ui_delegate.h"

namespace sharesheet {
class SharesheetServiceDelegator;
}  // namespace sharesheet

namespace ash {
namespace sharesheet {

class SharesheetBubbleView;

// SharesheetBubbleViewDelegate is the SharesheetUiDelegate for
// SharesheetBubbleView.
class SharesheetBubbleViewDelegate : public ::sharesheet::SharesheetUiDelegate {
 public:
  SharesheetBubbleViewDelegate(
      gfx::NativeWindow native_window,
      ::sharesheet::SharesheetServiceDelegator* sharesheet_service_delegator);
  ~SharesheetBubbleViewDelegate() override;
  SharesheetBubbleViewDelegate(const SharesheetBubbleViewDelegate&) = delete;
  SharesheetBubbleViewDelegate& operator=(const SharesheetBubbleViewDelegate&) =
      delete;

  // ::sharesheet::SharesheetUiDelegate:
  void ShowBubble(std::vector<::sharesheet::TargetInfo> targets,
                  apps::mojom::IntentPtr intent,
                  ::sharesheet::DeliveredCallback delivered_callback,
                  ::sharesheet::CloseCallback close_callback) override;

#if BUILDFLAG(IS_CHROMEOS_ASH)
  void ShowNearbyShareBubbleForArc(
      apps::mojom::IntentPtr intent,
      ::sharesheet::DeliveredCallback delivered_callback,
      ::sharesheet::CloseCallback close_callback) override;
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

  void OnActionLaunched() override;

  // ::sharesheet::SharesheetController:
  void SetBubbleSize(int width, int height) override;
  void CloseBubble(::sharesheet::SharesheetResult result) override;
  bool IsBubbleVisible() const override;

 protected:
  friend class SharesheetBubbleViewTest;

  SharesheetBubbleView* GetBubbleViewForTesting();

  // Owned by views.
  SharesheetBubbleView* sharesheet_bubble_view_;
};

}  // namespace sharesheet
}  // namespace ash

#endif  // CHROME_BROWSER_UI_ASH_SHARESHEET_SHARESHEET_BUBBLE_VIEW_DELEGATE_H_
