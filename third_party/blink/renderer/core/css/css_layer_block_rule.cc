// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/css_layer_block_rule.h"

#include "third_party/blink/renderer/core/css/style_rule.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"

namespace blink {

CSSLayerBlockRule::CSSLayerBlockRule(StyleRuleLayerBlock* layer_block_rule,
                                     CSSStyleSheet* parent)
    : CSSGroupingRule(layer_block_rule, parent) {}

CSSLayerBlockRule::~CSSLayerBlockRule() = default;

String CSSLayerBlockRule::cssText() const {
  StringBuilder result;
  result.Append("@layer ");
  String name = To<StyleRuleLayerBlock>(group_rule_.Get())->GetNameAsString();
  if (name.length()) {
    result.Append(name);
    result.Append(" ");
  }
  result.Append("{\n");
  AppendCSSTextForItems(result);
  result.Append('}');
  return result.ReleaseString();
}

void CSSLayerBlockRule::Reattach(StyleRuleBase* rule) {
  CSSGroupingRule::Reattach(rule);
}

void CSSLayerBlockRule::Trace(Visitor* visitor) const {
  CSSGroupingRule::Trace(visitor);
}

}  // namespace blink
