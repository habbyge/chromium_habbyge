// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_SCREEN_ENUMERATION_SCREEN_DETAILS_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_SCREEN_ENUMERATION_SCREEN_DETAILS_H_

#include "third_party/blink/renderer/core/dom/events/event_target.h"
#include "third_party/blink/renderer/core/execution_context/execution_context_lifecycle_observer.h"
#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/heap/handle.h"
#include "ui/display/screen_infos.h"

namespace blink {

class LocalDOMWindow;
class ScreenDetailed;

// Interface exposing multi-screen information.
// https://github.com/webscreens/window-placement
class MODULES_EXPORT ScreenDetails final
    : public EventTargetWithInlineData,
      public ExecutionContextLifecycleObserver {
  DEFINE_WRAPPERTYPEINFO();

 public:
  explicit ScreenDetails(LocalDOMWindow* window);

  // Web-exposed interface:
  const HeapVector<Member<ScreenDetailed>>& screens() const;
  ScreenDetailed* currentScreen() const;
  DEFINE_ATTRIBUTE_EVENT_LISTENER(screenschange, kScreenschange)
  DEFINE_ATTRIBUTE_EVENT_LISTENER(currentscreenchange, kCurrentscreenchange)

  // EventTargetWithInlineData:
  const AtomicString& InterfaceName() const override;
  ExecutionContext* GetExecutionContext() const override;

  // ExecutionContextLifecycleObserver:
  void ContextDestroyed() override;

  void Trace(Visitor*) const override;

  // Called when there is a visual property update with potentially new
  // multi-screen information.
  void UpdateScreenInfos(LocalDOMWindow* window,
                         const display::ScreenInfos& new_infos);

 private:
  // Returns the first unused index, starting at one.  Internal and external
  // are numbered separately.  This is used for ScreenDetailed::label strings.
  uint32_t GetNewLabelIdx(bool is_internal);
  void WillRemoveScreen(const ScreenDetailed& screen);

  // The ScreenInfos sent by the previous UpdateScreenInfos call.
  display::ScreenInfos prev_screen_infos_;
  int64_t current_display_id_ = display::ScreenInfo::kInvalidDisplayId;
  HeapVector<Member<ScreenDetailed>> screens_;

  // Active set of ids used in labels.
  HashSet<uint32_t> internal_label_ids_;
  HashSet<uint32_t> external_label_ids_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_SCREEN_ENUMERATION_SCREENS_H_
