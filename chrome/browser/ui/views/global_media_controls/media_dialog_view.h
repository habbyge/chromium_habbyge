// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_GLOBAL_MEDIA_CONTROLS_MEDIA_DIALOG_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_GLOBAL_MEDIA_CONTROLS_MEDIA_DIALOG_VIEW_H_

#include <map>
#include <memory>
#include <string>

#include "base/observer_list.h"
#include "components/global_media_controls/public/constants.h"
#include "components/global_media_controls/public/media_dialog_delegate.h"
#include "components/global_media_controls/public/media_item_ui_observer.h"
#include "components/soda/constants.h"
#include "components/soda/soda_installer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"

namespace content {
class WebContents;
}  // namespace content

namespace global_media_controls {
class MediaItemUIListView;
class MediaItemUIView;
}  // namespace global_media_controls

namespace views {
class Label;
class ToggleButton;
}  // namespace views

class MediaDialogViewObserver;
class MediaNotificationService;
class Profile;

// Dialog that shows media controls that control the active media session.
class MediaDialogView : public views::BubbleDialogDelegateView,
                        public global_media_controls::MediaDialogDelegate,
                        public global_media_controls::MediaItemUIObserver,
                        public speech::SodaInstaller::Observer {
 public:
  METADATA_HEADER(MediaDialogView);

  MediaDialogView(const MediaDialogView&) = delete;
  MediaDialogView& operator=(const MediaDialogView&) = delete;

  static views::Widget* ShowDialog(
      views::View* anchor_view,
      MediaNotificationService* service,
      Profile* profile,
      global_media_controls::GlobalMediaControlsEntryPoint entry_point);
  static views::Widget* ShowDialogForPresentationRequest(
      views::View* anchor_view,
      MediaNotificationService* service,
      Profile* profile,
      content::WebContents* contents,
      global_media_controls::GlobalMediaControlsEntryPoint entry_point);
  static void HideDialog();
  static bool IsShowing();

  static MediaDialogView* GetDialogViewForTesting() { return instance_; }

  // global_media_controls::MediaDialogDelegate:
  global_media_controls::MediaItemUI* ShowMediaItem(
      const std::string& id,
      base::WeakPtr<media_message_center::MediaNotificationItem> item) override;
  void HideMediaItem(const std::string& id) override;
  void HideMediaDialog() override;
  void Focus() override;

  // views::View implementation.
  void AddedToWidget() override;
  gfx::Size CalculatePreferredSize() const override;

  // global_media_controls::MediaItemUIObserver implementation.
  void OnMediaItemUISizeChanged() override;
  void OnMediaItemUIMetadataChanged() override;
  void OnMediaItemUIActionsChanged() override;
  void OnMediaItemUIClicked(const std::string& id) override {}
  void OnMediaItemUIDismissed(const std::string& id) override {}
  void OnMediaItemUIDestroyed(const std::string& id) override;

  void AddObserver(MediaDialogViewObserver* observer);
  void RemoveObserver(MediaDialogViewObserver* observer);

  const std::map<const std::string, global_media_controls::MediaItemUIView*>&
  GetItemsForTesting() const;

  const global_media_controls::MediaItemUIListView* GetListViewForTesting()
      const;

 private:
  friend class MediaDialogViewBrowserTest;
  MediaDialogView(
      views::View* anchor_view,
      MediaNotificationService* service,
      Profile* profile,
      content::WebContents* contents,
      global_media_controls::GlobalMediaControlsEntryPoint entry_point);

  ~MediaDialogView() override;

  static MediaDialogView* instance_;

  // True if the dialog has been opened this session.
  static bool has_been_opened_;

  // views::BubbleDialogDelegateView implementation.
  void Init() override;
  void WindowClosing() override;

  // views::Button::PressedCallback
  void OnLiveCaptionButtonPressed();

  void ToggleLiveCaption(bool enabled);
  void UpdateBubbleSize();

  // SodaInstaller::Observer overrides:
  void OnSodaInstalled() override;
  void OnSodaError() override;
  void OnSodaProgress(int combined_progress) override;

  std::unique_ptr<global_media_controls::MediaItemUIView> BuildMediaItemUIView(
      const std::string& id,
      base::WeakPtr<media_message_center::MediaNotificationItem> item);

  MediaNotificationService* const service_;

  Profile* const profile_;

  global_media_controls::MediaItemUIListView* const active_sessions_view_;

  base::ObserverList<MediaDialogViewObserver> observers_;

  // A map of all containers we're currently observing.
  std::map<const std::string, global_media_controls::MediaItemUIView*>
      observed_items_;

  views::View* live_caption_container_ = nullptr;
  views::Label* live_caption_title_ = nullptr;
  views::ToggleButton* live_caption_button_ = nullptr;

  // It stores the WebContents* from which a MediaRouterDialogControllerViews
  // opened the dialog for a presentation request. It is nullptr if the dialog
  // is opened from the toolbar.
  content::WebContents* const web_contents_for_presentation_request_ = nullptr;
  const global_media_controls::GlobalMediaControlsEntryPoint entry_point_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_GLOBAL_MEDIA_CONTROLS_MEDIA_DIALOG_VIEW_H_
