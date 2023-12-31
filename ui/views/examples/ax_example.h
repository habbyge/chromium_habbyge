// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_EXAMPLES_AX_EXAMPLE_H_
#define UI_VIEWS_EXAMPLES_AX_EXAMPLE_H_

#include "ui/views/examples/example_base.h"

namespace views {

class Button;

namespace examples {

// ButtonExample simply counts the number of clicks.
class VIEWS_EXAMPLES_EXPORT AxExample : public ExampleBase {
 public:
  AxExample();

  AxExample(const AxExample&) = delete;
  AxExample& operator=(const AxExample&) = delete;

  ~AxExample() override;

  // ExampleBase:
  void CreateExampleView(View* container) override;

 private:
  Button* announce_button_ = nullptr;
};

}  // namespace examples
}  // namespace views

#endif  // UI_VIEWS_EXAMPLES_AX_EXAMPLE_H_
