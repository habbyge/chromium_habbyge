// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_CONTAINER_VALUES_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_CONTAINER_VALUES_H_

#include "third_party/blink/renderer/core/css/css_to_length_conversion_data.h"
#include "third_party/blink/renderer/core/css/media_values_dynamic.h"

namespace blink {

class CSSContainerValues : public MediaValuesDynamic {
 public:
  explicit CSSContainerValues(Document& document,
                              const ComputedStyle& style,
                              double width,
                              double height);

  double Width() const override { return width_; }
  double Height() const override { return height_; }

 protected:
  float EmSize() const override;
  float RemSize() const override;
  float ExSize() const override;
  float ChSize() const override;

  // Container width in CSS pixels.
  double width_;
  // Container height in CSS pixels.
  double height_;
  // Container font sizes for resolving relative lengths.
  CSSToLengthConversionData::FontSizes font_sizes_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_CONTAINER_VALUES_H_
