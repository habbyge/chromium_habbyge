// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_WEBRTC_DESKTOP_MEDIA_PICKER_H_
#define CHROME_BROWSER_MEDIA_WEBRTC_DESKTOP_MEDIA_PICKER_H_

#include <memory>
#include <string>
#include <utility>

#include "base/callback_forward.h"
#include "base/memory/ref_counted.h"
#include "content/public/browser/desktop_media_id.h"
#include "content/public/browser/media_stream_request.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/ui_base_types.h"
#include "ui/gfx/native_widget_types.h"

class DesktopMediaList;

namespace content {
class WebContents;
}

// Base class for desktop media picker UI. It's used by Desktop Media API, and
// by ARC to let user choose a desktop media source.
//
// TODO(crbug.com/987001): Rename this class.
class DesktopMediaPicker {
 public:
  using DoneCallback = base::OnceCallback<void(content::DesktopMediaID id)>;

  struct Params {
    Params();
    Params(const Params&);
    Params& operator=(const Params&);
    ~Params();

    // WebContents this picker is relative to, can be null.
    content::WebContents* web_contents = nullptr;
    // The context whose root window is used for dialog placement, cannot be
    // null for Aura.
    gfx::NativeWindow context = nullptr;
    // Parent window the dialog is relative to, only used on Mac.
    gfx::NativeWindow parent = nullptr;
    // The modality used for showing the dialog.
    ui::ModalType modality = ui::ModalType::MODAL_TYPE_CHILD;
    // The name used in the dialog for what is requesting the picker to be
    // shown.
    std::u16string app_name;
    // Can be the same as target_name. If it is not then this is used in the
    // dialog for what is specific target within the app_name is requesting the
    // picker.
    std::u16string target_name;
    // Whether audio capture should be shown as an option in the picker.
    bool request_audio = false;
    // Normally, the media-picker sets the default states for the audio
    // checkboxes. If |force_audio_checkboxes_to_default_checked| is |true|,
    // it sets them all to |checked|.
    bool force_audio_checkboxes_to_default_checked = false;
    // This flag controls the behvior in the case where the picker is invoked to
    // select a screen and there is only one screen available.  If true, the
    // dialog is bypassed entirely and the screen is automatically selected.
    // This behavior is disabled by default because in addition to letting the
    // user select a desktop, the desktop picker also serves to prevent the
    // screen screen from being shared without the user's explicit consent.
    bool select_only_screen = false;
    // Indicates that the caller of this picker is subject to enterprise
    // policies that may restrict the available choices, and a suitable warning
    // should be shown to the user.
    bool restricted_by_policy = false;
  };

  // Creates a picker dialog/confirmation box depending on the value of
  // |request|. If no request is available the default picker, namely
  // DesktopMediaPickerViews is used.
  static std::unique_ptr<DesktopMediaPicker> Create(
      const content::MediaStreamRequest* request = nullptr);

  DesktopMediaPicker() = default;

  DesktopMediaPicker(const DesktopMediaPicker&) = delete;
  DesktopMediaPicker& operator=(const DesktopMediaPicker&) = delete;

  virtual ~DesktopMediaPicker() = default;

  // Shows dialog with list of desktop media sources (screens, windows, tabs)
  // provided by |sources_lists|.
  // Dialog window will call |done_callback| when user chooses one of the
  // sources or closes the dialog.
  virtual void Show(const Params& params,
                    std::vector<std::unique_ptr<DesktopMediaList>> source_lists,
                    DoneCallback done_callback) = 0;
};

#endif  // CHROME_BROWSER_MEDIA_WEBRTC_DESKTOP_MEDIA_PICKER_H_
