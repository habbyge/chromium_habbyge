// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AUTOFILL_PAYMENTS_LOCAL_CARD_MIGRATION_ERROR_DIALOG_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AUTOFILL_PAYMENTS_LOCAL_CARD_MIGRATION_ERROR_DIALOG_VIEW_H_

#include "chrome/browser/ui/autofill/payments/local_card_migration_dialog.h"
#include "chrome/browser/ui/views/autofill/payments/dialog_view_ids.h"
#include "components/autofill/core/browser/ui/payments/local_card_migration_dialog_controller.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/view.h"
#include "ui/views/window/dialog_delegate.h"

namespace content {
class WebContents;
}  // namespace content

namespace autofill {

class LocalCardMigrationErrorDialogView
    : public LocalCardMigrationDialog,
      public views::BubbleDialogDelegateView {
 public:
  METADATA_HEADER(LocalCardMigrationErrorDialogView);
  LocalCardMigrationErrorDialogView(
      LocalCardMigrationDialogController* controller,
      content::WebContents* web_contents);
  LocalCardMigrationErrorDialogView(const LocalCardMigrationErrorDialogView&) =
      delete;
  LocalCardMigrationErrorDialogView& operator=(
      const LocalCardMigrationErrorDialogView&) = delete;
  ~LocalCardMigrationErrorDialogView() override;

  // LocalCardMigrationDialog:
  void ShowDialog() override;
  void CloseDialog() override;

  // views::BubbleDialogDelegateView:
  void Init() override;
  void WindowClosing() override;

 private:
  LocalCardMigrationDialogController* controller_;

  content::WebContents* web_contents_;
};

}  // namespace autofill

#endif  // CHROME_BROWSER_UI_VIEWS_AUTOFILL_PAYMENTS_LOCAL_CARD_MIGRATION_ERROR_DIALOG_VIEW_H_
