// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/sharesheet/sharesheet_bubble_view_delegate.h"

#include <utility>

#include "base/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sharesheet/sharesheet_service_delegator.h"
#include "chrome/browser/ui/ash/sharesheet/sharesheet_bubble_view.h"
#include "ui/views/widget/widget.h"

namespace ash {
namespace sharesheet {

SharesheetBubbleViewDelegate::SharesheetBubbleViewDelegate(
    gfx::NativeWindow native_window,
    ::sharesheet::SharesheetServiceDelegator* sharesheet_service_delegator)
    : sharesheet_bubble_view_(
          new SharesheetBubbleView(native_window,
                                   sharesheet_service_delegator)) {}

SharesheetBubbleViewDelegate::~SharesheetBubbleViewDelegate() {
  // Delete the bubble view if not owned by the view tree yet.
  if (!sharesheet_bubble_view_->parent())
    delete sharesheet_bubble_view_;
}

void SharesheetBubbleViewDelegate::ShowBubble(
    std::vector<::sharesheet::TargetInfo> targets,
    apps::mojom::IntentPtr intent,
    ::sharesheet::DeliveredCallback delivered_callback,
    ::sharesheet::CloseCallback close_callback) {
  if (IsBubbleVisible()) {
    if (delivered_callback) {
      std::move(delivered_callback)
          .Run(::sharesheet::SharesheetResult::kErrorAlreadyOpen);
    }
    if (close_callback) {
      std::move(close_callback).Run(views::Widget::ClosedReason::kUnspecified);
    }
    return;
  }
  DCHECK(sharesheet_bubble_view_);
  sharesheet_bubble_view_->ShowBubble(std::move(targets), std::move(intent),
                                      std::move(delivered_callback),
                                      std::move(close_callback));
}

void SharesheetBubbleViewDelegate::ShowNearbyShareBubbleForArc(
    apps::mojom::IntentPtr intent,
    ::sharesheet::DeliveredCallback delivered_callback,
    ::sharesheet::CloseCallback close_callback) {
  if (IsBubbleVisible()) {
    if (delivered_callback) {
      std::move(delivered_callback)
          .Run(::sharesheet::SharesheetResult::kErrorAlreadyOpen);
    }
    if (close_callback) {
      std::move(close_callback).Run(views::Widget::ClosedReason::kUnspecified);
    }
    return;
  }
  DCHECK(sharesheet_bubble_view_);
  sharesheet_bubble_view_->ShowNearbyShareBubbleForArc(
      std::move(intent), std::move(delivered_callback),
      std::move(close_callback));
}

void SharesheetBubbleViewDelegate::OnActionLaunched() {
  DCHECK(sharesheet_bubble_view_);
  sharesheet_bubble_view_->ShowActionView();
}

void SharesheetBubbleViewDelegate::SetBubbleSize(int width, int height) {
  DCHECK(sharesheet_bubble_view_);
  DCHECK_GT(width, 0);
  DCHECK_GT(height, 0);
  sharesheet_bubble_view_->ResizeBubble(width, height);
}

void SharesheetBubbleViewDelegate::CloseBubble(
    ::sharesheet::SharesheetResult result) {
  views::Widget::ClosedReason reason =
      views::Widget::ClosedReason::kUnspecified;

  if (result == ::sharesheet::SharesheetResult::kSuccess) {
    reason = views::Widget::ClosedReason::kAcceptButtonClicked;
  } else if (result == ::sharesheet::SharesheetResult::kCancel) {
    reason = views::Widget::ClosedReason::kCancelButtonClicked;
  }
  DCHECK(sharesheet_bubble_view_);
  sharesheet_bubble_view_->CloseBubble(reason);
}

bool SharesheetBubbleViewDelegate::IsBubbleVisible() const {
  DCHECK(sharesheet_bubble_view_);
  return sharesheet_bubble_view_->GetWidget() &&
         sharesheet_bubble_view_->GetWidget()->IsVisible();
}

SharesheetBubbleView* SharesheetBubbleViewDelegate::GetBubbleViewForTesting() {
  return sharesheet_bubble_view_;
}

}  // namespace sharesheet
}  // namespace ash
