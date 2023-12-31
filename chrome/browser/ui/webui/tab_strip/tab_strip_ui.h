// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_TAB_STRIP_TAB_STRIP_UI_H_
#define CHROME_BROWSER_UI_WEBUI_TAB_STRIP_TAB_STRIP_UI_H_

#include "chrome/browser/ui/webui/tab_strip/tab_strip.mojom.h"
#include "chrome/browser/ui/webui/tab_strip/thumbnail_tracker.h"
#include "chrome/browser/ui/webui/webui_load_timer.h"
#include "content/public/browser/web_ui_controller.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class Browser;
class TabStripPageHandler;
class TabStripUIEmbedder;

extern const char kWebUITabIdDataType[];
extern const char kWebUITabGroupIdDataType[];

// The WebUI version of the tab strip in the browser. It is currently only
// supported on ChromeOS in tablet mode.
class TabStripUI : public ui::MojoWebUIController,
                   public tab_strip::mojom::PageHandlerFactory {
 public:
  explicit TabStripUI(content::WebUI* web_ui);

  TabStripUI(const TabStripUI&) = delete;
  TabStripUI& operator=(const TabStripUI&) = delete;

  ~TabStripUI() override;

  // Instantiates the implementor of the mojom::PageHandlerFactory mojo
  // interface passing the pending receiver that will be internally bound.
  void BindInterface(
      mojo::PendingReceiver<tab_strip::mojom::PageHandlerFactory> receiver);

  // Initialize TabStripUI with its embedder and the Browser it's
  // running in. Must be called exactly once. The WebUI won't work until
  // this is called.
  void Initialize(Browser* browser, TabStripUIEmbedder* embedder);

  // The embedder should call this whenever the result of
  // Embedder::GetLayout() changes.
  void LayoutChanged();

  // The embedder should call this whenever the tab strip gains keyboard focus.
  void ReceivedKeyboardFocus();

 private:
  void HandleThumbnailUpdate(int extension_tab_id,
                             ThumbnailTracker::CompressedThumbnailData image);

  // tab_strip::mojom::PageHandlerFactory
  void CreatePageHandler(
      mojo::PendingRemote<tab_strip::mojom::Page> page,
      mojo::PendingReceiver<tab_strip::mojom::PageHandler> receiver) override;

  WebuiLoadTimer webui_load_timer_;

  std::unique_ptr<TabStripPageHandler> page_handler_;

  mojo::Receiver<tab_strip::mojom::PageHandlerFactory> page_factory_receiver_{
      this};

  Browser* browser_ = nullptr;

  TabStripUIEmbedder* embedder_ = nullptr;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

#endif  // CHROME_BROWSER_UI_WEBUI_TAB_STRIP_TAB_STRIP_UI_H_
