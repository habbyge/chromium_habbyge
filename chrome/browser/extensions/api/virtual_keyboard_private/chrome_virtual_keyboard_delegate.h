// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_VIRTUAL_KEYBOARD_PRIVATE_CHROME_VIRTUAL_KEYBOARD_DELEGATE_H_
#define CHROME_BROWSER_EXTENSIONS_API_VIRTUAL_KEYBOARD_PRIVATE_CHROME_VIRTUAL_KEYBOARD_DELEGATE_H_

#include <string>

#include "ash/public/cpp/clipboard_history_controller.h"
#include "base/compiler_specific.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/browser_context.h"
#include "extensions/browser/api/virtual_keyboard_private/virtual_keyboard_delegate.h"
#include "extensions/common/api/virtual_keyboard.h"

namespace media {
class AudioSystem;
}

namespace extensions {

class ChromeVirtualKeyboardDelegate
    : public VirtualKeyboardDelegate,
      public ash::ClipboardHistoryController::Observer {
 public:
  explicit ChromeVirtualKeyboardDelegate(
      content::BrowserContext* browser_context);

  ChromeVirtualKeyboardDelegate(const ChromeVirtualKeyboardDelegate&) = delete;
  ChromeVirtualKeyboardDelegate& operator=(
      const ChromeVirtualKeyboardDelegate&) = delete;

  ~ChromeVirtualKeyboardDelegate() override;

  // TODO(oka): Create ChromeVirtualKeyboardPrivateDelegate class and move all
  // the methods except for RestrictFeatures into the class for clear separation
  // of virtualKeyboard and virtualKeyboardPrivate API.
  void GetKeyboardConfig(
      OnKeyboardSettingsCallback on_settings_callback) override;
  void OnKeyboardConfigChanged() override;
  bool HideKeyboard() override;
  bool InsertText(const std::u16string& text) override;
  bool OnKeyboardLoaded() override;
  void SetHotrodKeyboard(bool enable) override;
  bool LockKeyboard(bool state) override;
  bool SendKeyEvent(const std::string& type,
                    int char_value,
                    int key_code,
                    const std::string& key_name,
                    int modifiers) override;
  bool ShowLanguageSettings() override;
  bool ShowSuggestionSettings() override;
  bool IsSettingsEnabled() override;
  bool SetVirtualKeyboardMode(int mode_enum,
                              gfx::Rect target_bounds,
                              OnSetModeCallback on_set_mode_callback) override;
  bool SetDraggableArea(
      const api::virtual_keyboard_private::Bounds& rect) override;
  bool SetRequestedKeyboardState(int state_enum) override;
  bool SetOccludedBounds(const std::vector<gfx::Rect>& bounds) override;
  bool SetHitTestBounds(const std::vector<gfx::Rect>& bounds) override;
  bool SetAreaToRemainOnScreen(const gfx::Rect& bounds) override;
  bool SetWindowBoundsInScreen(const gfx::Rect& bounds_in_screen) override;
  void GetClipboardHistory(
      const std::set<std::string>& item_ids_filter,
      OnGetClipboardHistoryCallback get_history_callback) override;
  bool PasteClipboardItem(const std::string& clipboard_item_id) override;
  bool DeleteClipboardItem(const std::string& clipboard_item_id) override;
  api::virtual_keyboard::FeatureRestrictions RestrictFeatures(
      const api::virtual_keyboard::RestrictFeatures::Params& params) override;

 private:
  // ash::ClipboardHistoryController::Observer:
  void OnClipboardHistoryItemListAddedOrRemoved() override;
  void OnClipboardHistoryItemsUpdated(
      const std::vector<base::UnguessableToken>& menu_item_ids) override;

  void OnHasInputDevices(OnKeyboardSettingsCallback on_settings_callback,
                         bool has_audio_input_devices);
  void DispatchConfigChangeEvent(
      std::unique_ptr<base::DictionaryValue> settings);

  content::BrowserContext* browser_context_;
  std::unique_ptr<media::AudioSystem> audio_system_;
  base::WeakPtr<ChromeVirtualKeyboardDelegate> weak_this_;
  base::WeakPtrFactory<ChromeVirtualKeyboardDelegate> weak_factory_{this};
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_VIRTUAL_KEYBOARD_PRIVATE_CHROME_VIRTUAL_KEYBOARD_DELEGATE_H_
