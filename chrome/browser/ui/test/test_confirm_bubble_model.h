// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TEST_TEST_CONFIRM_BUBBLE_MODEL_H_
#define CHROME_BROWSER_UI_TEST_TEST_CONFIRM_BUBBLE_MODEL_H_

#include "base/compiler_specific.h"
#include "chrome/browser/ui/confirm_bubble_model.h"

// A test version of the model for confirmation bubbles.
class TestConfirmBubbleModel : public ConfirmBubbleModel {
 public:
  // Parameters may be NULL depending on the needs of the test.
  TestConfirmBubbleModel(bool* model_deleted,
                         bool* accept_clicked,
                         bool* cancel_clicked,
                         bool* link_clicked);

  TestConfirmBubbleModel(const TestConfirmBubbleModel&) = delete;
  TestConfirmBubbleModel& operator=(const TestConfirmBubbleModel&) = delete;

  ~TestConfirmBubbleModel() override;

  // ConfirmBubbleModel overrides:
  std::u16string GetTitle() const override;
  std::u16string GetMessageText() const override;
  std::u16string GetButtonLabel(ui::DialogButton button) const override;
  void Accept() override;
  void Cancel() override;
  std::u16string GetLinkText() const override;
  void OpenHelpPage() override;

 private:
  bool* model_deleted_;
  bool* accept_clicked_;
  bool* cancel_clicked_;
  bool* link_clicked_;
};

#endif  // CHROME_BROWSER_UI_TEST_TEST_CONFIRM_BUBBLE_MODEL_H_
