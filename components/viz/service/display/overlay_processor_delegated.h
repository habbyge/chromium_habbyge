// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_SERVICE_DISPLAY_OVERLAY_PROCESSOR_DELEGATED_H_
#define COMPONENTS_VIZ_SERVICE_DISPLAY_OVERLAY_PROCESSOR_DELEGATED_H_

#include <memory>
#include <vector>

#include "base/containers/flat_map.h"
#include "build/build_config.h"
#include "components/viz/common/display/overlay_strategy.h"
#include "components/viz/common/quads/aggregated_render_pass.h"
#include "components/viz/service/display/output_surface.h"
#include "components/viz/service/display/overlay_candidate.h"
#include "components/viz/service/display/overlay_candidate_temporal_tracker.h"
#include "components/viz/service/display/overlay_processor_ozone.h"
#include "components/viz/service/viz_service_export.h"
#include "gpu/ipc/common/surface_handle.h"

#include "ui/gfx/native_widget_types.h"
#include "ui/ozone/public/overlay_candidates_ozone.h"

namespace viz {

// OverlayProcessor subclass that attempts to promote to overlay all the draw
// quads of the root render pass. This is currently only used by LaCros.
// TODO(petermcneeley): This class and its Apple equivalent(s) will eventually
// be refactored in merged together into a unified delegation processor.
// Delegation will just become an extended feature of ozone and we avoid/push
// down platform specific defines and files where possible.
class VIZ_SERVICE_EXPORT OverlayProcessorDelegated
    : public OverlayProcessorOzone {
 public:
  OverlayProcessorDelegated(
      std::unique_ptr<ui::OverlayCandidatesOzone> overlay_candidates,
      std::vector<OverlayStrategy> available_strategies,
      gpu::SharedImageInterface* shared_image_interface);
  OverlayProcessorDelegated(const OverlayProcessorDelegated&) = delete;
  OverlayProcessorDelegated& operator=(const OverlayProcessorDelegated&) =
      delete;
  ~OverlayProcessorDelegated() override;

  bool DisableSplittingQuads() const override;

  void ProcessForOverlays(
      DisplayResourceProvider* resource_provider,
      AggregatedRenderPassList* render_passes,
      const skia::Matrix44& output_color_matrix,
      const FilterOperationsMap& render_pass_filters,
      const FilterOperationsMap& render_pass_backdrop_filters,
      SurfaceDamageRectList surface_damage_rect_list,
      OutputSurfaceOverlayPlane* output_surface_plane,
      CandidateList* overlay_candidates,
      gfx::Rect* damage_rect,
      std::vector<gfx::Rect>* content_bounds) final;

  // This function takes a pointer to the absl::optional instance so the
  // instance can be reset. When the overlay strategy covers the entire output
  // surface, we no longer need the output surface as a separate overlay. This
  // is also used by SurfaceControl to adjust rotation.
  // TODO(weiliangc): Internalize the |output_surface_plane| inside the overlay
  // processor.
  void AdjustOutputSurfaceOverlay(
      absl::optional<OutputSurfaceOverlayPlane>* output_surface_plane) override;

 private:
  gfx::RectF GetPrimaryPlaneDisplayRect(
      const OverlayProcessorInterface::OutputSurfaceOverlayPlane*
          primary_plane);
  // Iterate through a list of strategies and attempt to overlay with each.
  // Returns true if one of the attempts is successful. Has to be called after
  // InitializeStrategies(). A |primary_plane| represents the output surface's
  // buffer that comes from |BufferQueue|. It is passed in here so it could be
  // pass through to hardware through CheckOverlaySupport. It is not passed
  // through as a const member because the underlay strategy changes the
  // |primary_plane|'s blending setting.
  bool AttemptWithStrategies(
      const skia::Matrix44& output_color_matrix,
      const OverlayProcessorInterface::FilterOperationsMap&
          render_pass_backdrop_filters,
      DisplayResourceProvider* resource_provider,
      AggregatedRenderPassList* render_pass_list,
      SurfaceDamageRectList* surface_damage_rect_list,
      OverlayProcessorInterface::OutputSurfaceOverlayPlane* primary_plane,
      OverlayCandidateList* candidates,
      std::vector<gfx::Rect>* content_bounds);
};
}  // namespace viz

#endif  // COMPONENTS_VIZ_SERVICE_DISPLAY_OVERLAY_PROCESSOR_DELEGATED_H_
