// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/unified/top_shortcut_button.h"

#include "ash/style/ash_color_provider.h"
#include "ash/style/element_style.h"
#include "ash/system/tray/tray_popup_utils.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/animation/flood_fill_ink_drop_ripple.h"
#include "ui/views/animation/ink_drop_highlight.h"
#include "ui/views/animation/ink_drop_impl.h"
#include "ui/views/animation/ink_drop_mask.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/view_class_properties.h"

namespace ash {

TopShortcutButton::TopShortcutButton(PressedCallback callback,
                                     const gfx::VectorIcon& icon,
                                     int accessible_name_id)
    : views::ImageButton(std::move(callback)), icon_(icon) {
  SetImageHorizontalAlignment(ALIGN_CENTER);
  SetImageVerticalAlignment(ALIGN_MIDDLE);
  if (accessible_name_id)
    SetTooltipText(l10n_util::GetStringUTF16(accessible_name_id));
  TrayPopupUtils::ConfigureTrayPopupButton(this);
  views::InstallCircleHighlightPathGenerator(this);
}

TopShortcutButton::~TopShortcutButton() = default;

void TopShortcutButton::PaintButtonContents(gfx::Canvas* canvas) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(AshColorProvider::Get()->GetControlsLayerColor(
      AshColorProvider::ControlsLayerType::kControlBackgroundColorInactive));
  flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawPath(views::GetHighlightPath(this), flags);

  views::ImageButton::PaintButtonContents(canvas);
}

void TopShortcutButton::OnThemeChanged() {
  views::ImageButton::OnThemeChanged();
  element_style::DecorateSmallIconButton(this, icon_,
                                         /*toggled_=*/false,
                                         /*has_border=*/false);
  views::FocusRing::Get(this)->SetColor(
      AshColorProvider::Get()->GetControlsLayerColor(
          AshColorProvider::ControlsLayerType::kFocusRingColor));
  SchedulePaint();
}

BEGIN_METADATA(TopShortcutButton, views::ImageButton)
END_METADATA

}  // namespace ash
