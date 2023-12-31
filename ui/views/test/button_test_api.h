// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_TEST_BUTTON_TEST_API_H_
#define UI_VIEWS_TEST_BUTTON_TEST_API_H_

namespace ui {
class Event;
}

namespace views {
class Button;

namespace test {

// A wrapper of Button to access private members for testing.
class ButtonTestApi {
 public:
  explicit ButtonTestApi(Button* button) : button_(button) {}

  ButtonTestApi(const ButtonTestApi&) = delete;
  ButtonTestApi& operator=(const ButtonTestApi&) = delete;

  void NotifyClick(const ui::Event& event);

 private:
  Button* button_;
};

}  // namespace test

}  // namespace views

#endif  // UI_VIEWS_TEST_BUTTON_TEST_API_H_
