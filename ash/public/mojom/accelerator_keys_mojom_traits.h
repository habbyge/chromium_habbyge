// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_PUBLIC_MOJOM_ACCELERATOR_KEYS_MOJOM_TRAITS_H_
#define ASH_PUBLIC_MOJOM_ACCELERATOR_KEYS_MOJOM_TRAITS_H_

#include "ash/public/mojom/accelerator_keys.mojom.h"
#include "mojo/public/cpp/bindings/enum_traits.h"
#include "ui/events/keycodes/keyboard_codes_posix.h"

namespace mojo {

template <>
struct EnumTraits<ash::accelerator_keys::mojom::VKey, ::ui::KeyboardCode> {
  static ash::accelerator_keys::mojom::VKey ToMojom(::ui::KeyboardCode);

  static bool FromMojom(::ash::accelerator_keys::mojom::VKey input,
                        ::ui::KeyboardCode* out);
};

}  // namespace mojo

#endif  // ASH_PUBLIC_MOJOM_ACCELERATOR_KEYS_MOJOM_TRAITS_H_
