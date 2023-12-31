// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_EXAMPLES_LABEL_EXAMPLE_H_
#define UI_VIEWS_EXAMPLES_LABEL_EXAMPLE_H_

#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/examples/example_base.h"

namespace views {

class Checkbox;
class Combobox;
class Label;
class View;

namespace examples {

class VIEWS_EXAMPLES_EXPORT LabelExample : public ExampleBase,
                                           public TextfieldController {
 public:
  LabelExample();

  LabelExample(const LabelExample&) = delete;
  LabelExample& operator=(const LabelExample&) = delete;

  ~LabelExample() override;

  // ExampleBase:
  void CreateExampleView(View* container) override;

  void MultilineCheckboxPressed();
  void ShadowsCheckboxPressed();
  void SelectableCheckboxPressed();

  // TextfieldController:
  void ContentsChanged(Textfield* sender,
                       const std::u16string& new_contents) override;

 private:
  // Add a customizable label and various controls to modify its presentation.
  void AddCustomLabel(View* container);

  // Creates and adds a combobox to the layout.
  Combobox* AddCombobox(View* parent,
                        std::u16string name,
                        const char** strings,
                        int count,
                        void (LabelExample::*function)());

  void AlignmentChanged();
  void ElidingChanged();

  Textfield* textfield_ = nullptr;
  Combobox* alignment_ = nullptr;
  Combobox* elide_behavior_ = nullptr;
  Checkbox* multiline_ = nullptr;
  Checkbox* shadows_ = nullptr;
  Checkbox* selectable_ = nullptr;
  Label* custom_label_ = nullptr;
};

}  // namespace examples
}  // namespace views

#endif  // UI_VIEWS_EXAMPLES_LABEL_EXAMPLE_H_
