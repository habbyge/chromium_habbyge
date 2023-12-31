// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_EXTENSIONS_EXTENSION_CONTEXT_MENU_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_EXTENSIONS_EXTENSION_CONTEXT_MENU_CONTROLLER_H_

#include <memory>

#include "ui/views/context_menu_controller.h"

class ToolbarActionViewController;

namespace views {
class Button;
class MenuItemView;
class MenuModelAdapter;
class MenuRunner;
}  // namespace views

class ExtensionContextMenuController : public views::ContextMenuController {
 public:
  explicit ExtensionContextMenuController(
      ToolbarActionViewController* controller);

  ExtensionContextMenuController(const ExtensionContextMenuController&) =
      delete;
  ExtensionContextMenuController& operator=(
      const ExtensionContextMenuController&) = delete;

  ~ExtensionContextMenuController() override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(views::View* source,
                                  const gfx::Point& point,
                                  ui::MenuSourceType source_type) override;

  bool IsMenuRunning() const;

  views::MenuItemView* menu_for_testing() { return menu_; }

 private:
  void RunExtensionContextMenu(views::Button* source);

  // Callback for MenuModelAdapter.
  void OnMenuClosed();

  // Responsible for converting the context menu model into |menu_|.
  std::unique_ptr<views::MenuModelAdapter> menu_adapter_;

  // Responsible for running the menu.
  std::unique_ptr<views::MenuRunner> menu_runner_;

  // The root MenuItemView for the context menu, or null if no menu is being
  // shown. This is used for testing.
  views::MenuItemView* menu_ = nullptr;

  // This controller contains the data for the extension's context menu.
  ToolbarActionViewController* const controller_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_EXTENSIONS_EXTENSION_CONTEXT_MENU_CONTROLLER_H_
