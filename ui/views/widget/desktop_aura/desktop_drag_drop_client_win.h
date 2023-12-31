// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_WIDGET_DESKTOP_AURA_DESKTOP_DRAG_DROP_CLIENT_WIN_H_
#define UI_VIEWS_WIDGET_DESKTOP_AURA_DESKTOP_DRAG_DROP_CLIENT_WIN_H_

#include <wrl/client.h>

#include <memory>

#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "ui/aura/client/drag_drop_client.h"
#include "ui/views/views_export.h"

namespace aura {
namespace client {
class DragDropClientObserver;
}
}  // namespace aura

namespace ui {
class DragSourceWin;
}

namespace views {
class DesktopDropTargetWin;
class DesktopWindowTreeHostWin;

class VIEWS_EXPORT DesktopDragDropClientWin
    : public aura::client::DragDropClient {
 public:
  DesktopDragDropClientWin(aura::Window* root_window,
                           HWND window,
                           DesktopWindowTreeHostWin* desktop_host);

  DesktopDragDropClientWin(const DesktopDragDropClientWin&) = delete;
  DesktopDragDropClientWin& operator=(const DesktopDragDropClientWin&) = delete;

  ~DesktopDragDropClientWin() override;

  // Overridden from aura::client::DragDropClient:
  ui::mojom::DragOperation StartDragAndDrop(
      std::unique_ptr<ui::OSExchangeData> data,
      aura::Window* root_window,
      aura::Window* source_window,
      const gfx::Point& screen_location,
      int allowed_operations,
      ui::mojom::DragEventSource source) override;
  void DragCancel() override;
  bool IsDragDropInProgress() override;
  void AddObserver(aura::client::DragDropClientObserver* observer) override;
  void RemoveObserver(aura::client::DragDropClientObserver* observer) override;

  void OnNativeWidgetDestroying(HWND window);

 private:
  bool drag_drop_in_progress_;

  Microsoft::WRL::ComPtr<ui::DragSourceWin> drag_source_;

  scoped_refptr<DesktopDropTargetWin> drop_target_;

  // |this| will get deleted DesktopNativeWidgetAura is notified that the
  // DesktopWindowTreeHost is being destroyed. So desktop_host_ should outlive
  // |this|.
  DesktopWindowTreeHostWin* desktop_host_ = nullptr;

  base::WeakPtrFactory<DesktopDragDropClientWin> weak_factory_{this};
};

}  // namespace views

#endif  // UI_VIEWS_WIDGET_DESKTOP_AURA_DESKTOP_DRAG_DROP_CLIENT_WIN_H_
