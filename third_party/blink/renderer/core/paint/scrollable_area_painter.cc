// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/scrollable_area_painter.h"

#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/paint/custom_scrollbar_theme.h"
#include "third_party/blink/renderer/core/paint/object_paint_properties.h"
#include "third_party/blink/renderer/core/paint/paint_auto_dark_mode.h"
#include "third_party/blink/renderer/core/paint/paint_info.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/scroll/scrollbar_layer_delegate.h"
#include "third_party/blink/renderer/core/scroll/scrollbar_theme.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context_state_saver.h"
#include "third_party/blink/renderer/platform/graphics/graphics_layer.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_recorder.h"
#include "third_party/blink/renderer/platform/graphics/paint/scoped_paint_chunk_properties.h"
#include "third_party/blink/renderer/platform/graphics/paint/scrollbar_display_item.h"

namespace blink {

void ScrollableAreaPainter::PaintResizer(GraphicsContext& context,
                                         const gfx::Vector2d& paint_offset,
                                         const CullRect& cull_rect) {
  const auto* box = GetScrollableArea().GetLayoutBox();
  DCHECK_EQ(box->StyleRef().Visibility(), EVisibility::kVisible);
  if (!box->CanResize())
    return;

  gfx::Rect visual_rect =
      ToGfxRect(GetScrollableArea().ResizerCornerRect(kResizerForPointer));
  visual_rect.Offset(paint_offset);
  if (!cull_rect.Intersects(visual_rect))
    return;

  const auto& client = DisplayItemClientForCorner();
  if (const auto* resizer = GetScrollableArea().Resizer()) {
    CustomScrollbarTheme::PaintIntoRect(*resizer, context,
                                        PhysicalRect(visual_rect));
    return;
  }

  if (DrawingRecorder::UseCachedDrawingIfPossible(context, client,
                                                  DisplayItem::kResizer))
    return;

  DrawingRecorder recorder(context, client, DisplayItem::kResizer, visual_rect);

  DrawPlatformResizerImage(context, IntRect(visual_rect));

  // Draw a frame around the resizer (1px grey line) if there are any scrollbars
  // present.  Clipping will exclude the right and bottom edges of this frame.
  if (GetScrollableArea().NeedsScrollCorner()) {
    GraphicsContextStateSaver state_saver(context);
    context.Clip(IntRect(visual_rect));
    gfx::Rect larger_corner = visual_rect;
    larger_corner.set_size(
        gfx::Size(larger_corner.width() + 1, larger_corner.height() + 1));
    context.SetStrokeColor(Color(217, 217, 217));
    context.SetStrokeThickness(1.0f);
    context.SetFillColor(Color::kTransparent);
    AutoDarkMode auto_dark_mode(PaintAutoDarkMode(
        box->StyleRef(), DarkModeFilter::ElementRole::kBackground));
    context.DrawRect(IntRect(larger_corner), auto_dark_mode);
  }
}

void ScrollableAreaPainter::RecordResizerScrollHitTestData(
    GraphicsContext& context,
    const PhysicalOffset& paint_offset) {
  const auto* box = GetScrollableArea().GetLayoutBox();
  DCHECK_EQ(box->StyleRef().Visibility(), EVisibility::kVisible);
  if (!box->CanResize())
    return;

  gfx::Rect touch_rect =
      ToGfxRect(scrollable_area_->ResizerCornerRect(kResizerForTouch));
  touch_rect.Offset(ToRoundedVector2d(paint_offset));
  context.GetPaintController().RecordScrollHitTestData(
      DisplayItemClientForCorner(), DisplayItem::kResizerScrollHitTest, nullptr,
      touch_rect);
}

void ScrollableAreaPainter::DrawPlatformResizerImage(
    GraphicsContext& context,
    const IntRect& resizer_corner_rect) {
  gfx::Point points[4];
  bool on_left = false;
  if (GetScrollableArea()
          .GetLayoutBox()
          ->ShouldPlaceBlockDirectionScrollbarOnLogicalLeft()) {
    on_left = true;
    points[0].set_x(resizer_corner_rect.x() + 1);
    points[1].set_x(resizer_corner_rect.x() + resizer_corner_rect.width() -
                    resizer_corner_rect.width() / 2);
    points[2].set_x(points[0].x());
    points[3].set_x(resizer_corner_rect.x() + resizer_corner_rect.width() -
                    resizer_corner_rect.width() * 3 / 4);
  } else {
    points[0].set_x(resizer_corner_rect.x() + resizer_corner_rect.width() - 1);
    points[1].set_x(resizer_corner_rect.x() + resizer_corner_rect.width() / 2);
    points[2].set_x(points[0].x());
    points[3].set_x(resizer_corner_rect.x() +
                    resizer_corner_rect.width() * 3 / 4);
  }
  points[0].set_y(resizer_corner_rect.y() + resizer_corner_rect.height() / 2);
  points[1].set_y(resizer_corner_rect.y() + resizer_corner_rect.height() - 1);
  points[2].set_y(resizer_corner_rect.y() +
                  resizer_corner_rect.height() * 3 / 4);
  points[3].set_y(points[1].y());

  PaintFlags paint_flags;
  paint_flags.setStyle(PaintFlags::kStroke_Style);
  paint_flags.setStrokeWidth(1);

  SkPathBuilder line_path;

  AutoDarkMode auto_dark_mode(
      PaintAutoDarkMode(GetScrollableArea().GetLayoutBox()->StyleRef(),
                        DarkModeFilter::ElementRole::kBackground));

  // Draw a dark line, to ensure contrast against a light background
  line_path.moveTo(points[0].x(), points[0].y());
  line_path.lineTo(points[1].x(), points[1].y());
  line_path.moveTo(points[2].x(), points[2].y());
  line_path.lineTo(points[3].x(), points[3].y());
  paint_flags.setColor(SkColorSetARGB(153, 0, 0, 0));
  context.DrawPath(line_path.detach(), paint_flags, auto_dark_mode);

  // Draw a light line one pixel below the light line,
  // to ensure contrast against a dark background
  line_path.moveTo(points[0].x(), points[0].y() + 1);
  line_path.lineTo(points[1].x() + (on_left ? -1 : 1), points[1].y());
  line_path.moveTo(points[2].x(), points[2].y() + 1);
  line_path.lineTo(points[3].x() + (on_left ? -1 : 1), points[3].y());
  paint_flags.setColor(SkColorSetARGB(153, 255, 255, 255));
  context.DrawPath(line_path.detach(), paint_flags, auto_dark_mode);
}

void ScrollableAreaPainter::PaintOverflowControls(
    const PaintInfo& paint_info,
    const gfx::Vector2d& paint_offset) {
  // Don't do anything if we have no overflow.
  const auto& box = *GetScrollableArea().GetLayoutBox();
  if (!box.IsScrollContainer() ||
      box.StyleRef().Visibility() != EVisibility::kVisible)
    return;

  // Overlay overflow controls are painted in the dedicated paint phase, and
  // normal overflow controls are painted in the background paint phase.
  if (GetScrollableArea().ShouldOverflowControlsPaintAsOverlay()) {
    if (paint_info.phase != PaintPhase::kOverlayOverflowControls)
      return;
  } else if (!ShouldPaintSelfBlockBackground(paint_info.phase)) {
    return;
  }

  GraphicsContext& context = paint_info.context;
  const auto* fragment = paint_info.FragmentToPaint(box);
  if (!fragment)
    return;

  const ClipPaintPropertyNode* clip = nullptr;
  const auto* properties = fragment->PaintProperties();
  // TODO(crbug.com/849278): Remove either the DCHECK or the if condition
  // when we figure out in what cases that the box doesn't have properties.
  DCHECK(properties);
  if (properties)
    clip = properties->OverflowControlsClip();

  const TransformPaintPropertyNodeOrAlias* transform = nullptr;
  if (RuntimeEnabledFeatures::CompositeAfterPaintEnabled() &&
      box.IsGlobalRootScroller()) {
    LocalFrameView* frame_view = box.GetFrameView();
    DCHECK(frame_view);
    const auto* page = frame_view->GetPage();
    const auto& viewport = page->GetVisualViewport();
    if (const auto* overscroll_transform =
            viewport.GetOverscrollElasticityTransformNode()) {
      transform = overscroll_transform->Parent();
    }
  }

  absl::optional<ScopedPaintChunkProperties> scoped_paint_chunk_properties;
  if (clip || transform) {
    PaintController& paint_controller = context.GetPaintController();
    PropertyTreeStateOrAlias modified_properties(
        paint_controller.CurrentPaintChunkProperties());
    if (clip)
      modified_properties.SetClip(*clip);
    if (transform)
      modified_properties.SetTransform(*transform);

    scoped_paint_chunk_properties.emplace(paint_controller, modified_properties,
                                          box, DisplayItem::kOverflowControls);
  }

  if (GetScrollableArea().HorizontalScrollbar() &&
      !GetScrollableArea().GraphicsLayerForHorizontalScrollbar()) {
    PaintScrollbar(context, *GetScrollableArea().HorizontalScrollbar(),
                   paint_offset, paint_info.GetCullRect());
  }
  if (GetScrollableArea().VerticalScrollbar() &&
      !GetScrollableArea().GraphicsLayerForVerticalScrollbar()) {
    PaintScrollbar(context, *GetScrollableArea().VerticalScrollbar(),
                   paint_offset, paint_info.GetCullRect());
  }

  if (!GetScrollableArea().GraphicsLayerForScrollCorner()) {
    // We fill our scroll corner with white if we have a scrollbar that doesn't
    // run all the way up to the edge of the box.
    PaintScrollCorner(context, paint_offset, paint_info.GetCullRect());

    // Paint our resizer last, since it sits on top of the scroll corner.
    PaintResizer(context, paint_offset, paint_info.GetCullRect());
  }
}

void ScrollableAreaPainter::PaintScrollbar(GraphicsContext& context,
                                           Scrollbar& scrollbar,
                                           const gfx::Vector2d& paint_offset,
                                           const CullRect& cull_rect) {
  // TODO(crbug.com/1020913): We should not round paint_offset but should
  // consider subpixel accumulation when painting scrollbars.
  gfx::Rect visual_rect = ToGfxRect(scrollbar.FrameRect());
  visual_rect.Offset(paint_offset);
  if (!cull_rect.Intersects(visual_rect))
    return;

  const auto* properties =
      GetScrollableArea().GetLayoutBox()->FirstFragment().PaintProperties();
  DCHECK(properties);
  auto type = scrollbar.Orientation() == kHorizontalScrollbar
                  ? DisplayItem::kScrollbarHorizontal
                  : DisplayItem::kScrollbarVertical;
  absl::optional<ScopedPaintChunkProperties> chunk_properties;
  if (const auto* effect = scrollbar.Orientation() == kHorizontalScrollbar
                               ? properties->HorizontalScrollbarEffect()
                               : properties->VerticalScrollbarEffect()) {
    chunk_properties.emplace(context.GetPaintController(), *effect, scrollbar,
                             type);
  }

  if (scrollbar.IsCustomScrollbar()) {
    scrollbar.Paint(context, paint_offset);

    // Prevent composited scroll hit test on the custom scrollbar which always
    // need main thread scrolling.
    context.GetPaintController().RecordScrollHitTestData(
        scrollbar, DisplayItem::kCustomScrollbarHitTest, nullptr, visual_rect);
    return;
  }

  if (!RuntimeEnabledFeatures::CompositeAfterPaintEnabled()) {
    scrollbar.Paint(context, paint_offset);
    return;
  }

  if (context.GetPaintController().UseCachedItemIfPossible(scrollbar, type))
    return;

  const TransformPaintPropertyNode* scroll_translation = nullptr;
  if (scrollable_area_->ShouldDirectlyCompositeScrollbar(scrollbar))
    scroll_translation = properties->ScrollTranslation();
  auto delegate = base::MakeRefCounted<ScrollbarLayerDelegate>(
      scrollbar, context.DeviceScaleFactor());
  ScrollbarDisplayItem::Record(context, scrollbar, type, delegate, visual_rect,
                               scroll_translation, scrollbar.GetElementId());
}

void ScrollableAreaPainter::PaintScrollCorner(GraphicsContext& context,
                                              const gfx::Vector2d& paint_offset,
                                              const CullRect& cull_rect) {
  IntRect visual_rect = GetScrollableArea().ScrollCornerRect();
  visual_rect.Offset(paint_offset);
  if (!cull_rect.Intersects(ToGfxRect(visual_rect)))
    return;

  if (const auto* scroll_corner = GetScrollableArea().ScrollCorner()) {
    CustomScrollbarTheme::PaintIntoRect(*scroll_corner, context,
                                        PhysicalRect(visual_rect));
    return;
  }

  // We don't want to paint opaque if we have overlay scrollbars, since we need
  // to see what is behind it.
  if (GetScrollableArea().HasOverlayScrollbars())
    return;

  ScrollbarTheme* theme = nullptr;

  if (GetScrollableArea().HorizontalScrollbar()) {
    theme = &GetScrollableArea().HorizontalScrollbar()->GetTheme();
  } else if (GetScrollableArea().VerticalScrollbar()) {
    theme = &GetScrollableArea().VerticalScrollbar()->GetTheme();
  } else {
    NOTREACHED();
  }

  const auto& client = DisplayItemClientForCorner();
  theme->PaintScrollCorner(context, GetScrollableArea().VerticalScrollbar(),
                           client, visual_rect,
                           GetScrollableArea().UsedColorScheme());
}

PaintLayerScrollableArea& ScrollableAreaPainter::GetScrollableArea() const {
  return *scrollable_area_;
}

const DisplayItemClient& ScrollableAreaPainter::DisplayItemClientForCorner()
    const {
  if (const auto* graphics_layer =
          GetScrollableArea().GraphicsLayerForScrollCorner())
    return *graphics_layer;
  return GetScrollableArea().GetScrollCornerDisplayItemClient();
}

}  // namespace blink
