// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_INFOBARS_CONFIRM_INFOBAR_H_
#define CHROME_BROWSER_UI_VIEWS_INFOBARS_CONFIRM_INFOBAR_H_

#include "base/compiler_specific.h"
#include "chrome/browser/ui/views/infobars/infobar_view.h"
#include "components/infobars/core/confirm_infobar_delegate.h"

class ElevationIconSetter;

namespace views {
class Label;
class MdTextButton;
}

// An infobar that shows a message, up to two optional buttons, and an optional,
// right-aligned link.  This is commonly used to do things like:
// "Would you like to do X?  [Yes]  [No]               _Learn More_ [x]"
class ConfirmInfoBar : public InfoBarView {
 public:
  explicit ConfirmInfoBar(std::unique_ptr<ConfirmInfoBarDelegate> delegate);

  ConfirmInfoBar(const ConfirmInfoBar&) = delete;
  ConfirmInfoBar& operator=(const ConfirmInfoBar&) = delete;

  ~ConfirmInfoBar() override;

  // InfoBarView:
  void Layout() override;

  ConfirmInfoBarDelegate* GetDelegate();

  views::MdTextButton* ok_button_for_testing() { return ok_button_; }

 protected:
  // InfoBarView:
  int GetContentMinimumWidth() const override;

 private:
  void OkButtonPressed();
  void CancelButtonPressed();

  // Returns the width of all content other than the label and link.  Layout()
  // uses this to determine how much space the label and link can take.
  int NonLabelWidth() const;

  views::Label* label_ = nullptr;
  views::MdTextButton* ok_button_ = nullptr;
  views::MdTextButton* cancel_button_ = nullptr;
  views::Link* link_ = nullptr;
  std::unique_ptr<ElevationIconSetter> elevation_icon_setter_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_INFOBARS_CONFIRM_INFOBAR_H_
