// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/app_list/views/apps_grid_context_menu.h"

#include "ash/app_list/app_list_view_delegate.h"
#include "ash/constants/ash_features.h"
#include "ash/resources/vector_icons/vector_icons.h"
#include "ash/strings/grit/ash_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/simple_menu_model.h"
#include "ui/views/controls/menu/menu_model_adapter.h"
#include "ui/views/controls/menu/menu_runner.h"

namespace ash {

AppsGridContextMenu::AppsGridContextMenu(AppListViewDelegate* delegate)
    : delegate_(delegate) {}

AppsGridContextMenu::~AppsGridContextMenu() = default;

bool AppsGridContextMenu::IsMenuShowing() const {
  return menu_runner_ && menu_runner_->IsRunning();
}

void AppsGridContextMenu::ExecuteCommand(int command_id, int event_flags) {
  switch (command_id) {
    case AppsGridCommandId::kReorderByNameAlphabetical:
      delegate_->SortAppList(AppListSortOrder::kNameAlphabetical);
      break;
    case AppsGridCommandId::kReorderByNameReverseAlphabetical:
      delegate_->SortAppList(AppListSortOrder::kNameReverseAlphabetical);
      break;
    default:
      NOTREACHED();
  }
}

void AppsGridContextMenu::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::MenuSourceType source_type) {
  if (!features::IsLauncherAppSortEnabled())
    return;

  // Build the menu model and save it to `context_menu_model_`.
  BuildMenuModel();
  menu_model_adapter_ = std::make_unique<views::MenuModelAdapter>(
      context_menu_model_.get(),
      base::BindRepeating(&AppsGridContextMenu::OnMenuClosed,
                          base::Unretained(this)));
  root_menu_item_view_ = menu_model_adapter_->CreateMenu();

  int run_types = views::MenuRunner::USE_TOUCHABLE_LAYOUT |
                  views::MenuRunner::CONTEXT_MENU |
                  views::MenuRunner::FIXED_ANCHOR;
  menu_runner_ =
      std::make_unique<views::MenuRunner>(root_menu_item_view_, run_types);
  menu_runner_->RunMenuAt(
      source->GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
      views::MenuAnchorPosition::kBubbleBottomRight, source_type);
}

void AppsGridContextMenu::BuildMenuModel() {
  context_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  reorder_name_submenu_ = std::make_unique<ui::SimpleMenuModel>(this);

  // As both of the submenu items are not planned to be launched, the option
  // strings are directly written as the parameters.
  reorder_name_submenu_->AddItem(kReorderByNameAlphabetical, u"Alphabetical");
  reorder_name_submenu_->AddItem(kReorderByNameReverseAlphabetical,
                                 u"Reverse alphabetical");

  context_menu_model_->AddTitle(l10n_util::GetStringUTF16(
      IDS_ASH_LAUNCHER_APPS_GRID_CONTEXT_MENU_REORDER_TITLE));
  context_menu_model_->AddSubMenuWithIcon(
      AppsGridCommandId::kReorderByName,
      l10n_util::GetStringUTF16(
          IDS_ASH_LAUNCHER_APPS_GRID_CONTEXT_MENU_REORDER_BY_NAME),
      reorder_name_submenu_.get(),
      ui::ImageModel::FromVectorIcon(kSortAlphabeticalIcon));
}

void AppsGridContextMenu::OnMenuClosed() {
  menu_runner_.reset();
  reorder_name_submenu_.reset();
  context_menu_model_.reset();
  root_menu_item_view_ = nullptr;
  menu_model_adapter_.reset();
}

}  // namespace ash
