// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_STYLE_ORDERED_NAMED_GRID_LINES_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_STYLE_ORDERED_NAMED_GRID_LINES_H_

#include "third_party/blink/renderer/platform/wtf/hash_map.h"
#include "third_party/blink/renderer/platform/wtf/hash_traits.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

struct NamedGridLine {
  NamedGridLine(const String line_name, bool is_in_repeat, bool is_first_repeat)
      : line_name(line_name),
        is_in_repeat(is_in_repeat),
        is_first_repeat(is_first_repeat) {}

  bool operator==(const NamedGridLine& other) const {
    return line_name == other.line_name && is_in_repeat == other.is_in_repeat &&
           is_first_repeat == other.is_first_repeat;
  }

  String line_name;
  bool is_in_repeat;
  bool is_first_repeat;
};

using OrderedNamedGridLines =
    HashMap<size_t,
            Vector<NamedGridLine>,
            WTF::IntHash<size_t>,
            WTF::UnsignedWithZeroKeyHashTraits<size_t>>;

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_STYLE_ORDERED_NAMED_GRID_LINES_H_
