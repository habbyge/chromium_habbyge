// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_SERVICE_DISPLAY_EMBEDDER_SKIA_OUTPUT_DEVICE_VULKAN_H_
#define COMPONENTS_VIZ_SERVICE_DISPLAY_EMBEDDER_SKIA_OUTPUT_DEVICE_VULKAN_H_

#include <memory>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/types/pass_key.h"
#include "build/build_config.h"
#include "components/viz/service/display_embedder/skia_output_device.h"
#include "gpu/ipc/common/surface_handle.h"
#include "gpu/vulkan/vulkan_swap_chain.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace gpu {
class VulkanSurface;
}

namespace viz {

class VulkanContextProvider;

class SkiaOutputDeviceVulkan final : public SkiaOutputDevice {
 public:
  SkiaOutputDeviceVulkan(
      base::PassKey<SkiaOutputDeviceVulkan>,
      VulkanContextProvider* context_provider,
      gpu::SurfaceHandle surface_handle,
      gpu::MemoryTracker* memory_tracker,
      DidSwapBufferCompleteCallback did_swap_buffer_complete_callback);

  SkiaOutputDeviceVulkan(const SkiaOutputDeviceVulkan&) = delete;
  SkiaOutputDeviceVulkan& operator=(const SkiaOutputDeviceVulkan&) = delete;

  ~SkiaOutputDeviceVulkan() override;

  static std::unique_ptr<SkiaOutputDeviceVulkan> Create(
      VulkanContextProvider* context_provider,
      gpu::SurfaceHandle surface_handle,
      gpu::MemoryTracker* memory_tracker,
      DidSwapBufferCompleteCallback did_swap_buffer_complete_callback);

#if defined(OS_WIN)
  gpu::SurfaceHandle GetChildSurfaceHandle();
#endif
  // SkiaOutputDevice implementation:
  void Submit(bool sync_cpu, base::OnceClosure callback) override;
  bool Reshape(const gfx::Size& size,
               float device_scale_factor,
               const gfx::ColorSpace& color_space,
               gfx::BufferFormat format,
               gfx::OverlayTransform transform) override;
  void SwapBuffers(BufferPresentedCallback feedback,
                   OutputSurfaceFrame frame) override;
  void PostSubBuffer(const gfx::Rect& rect,
                     BufferPresentedCallback feedback,
                     OutputSurfaceFrame frame) override;
  SkSurface* BeginPaint(
      bool allocate_frame_buffer,
      std::vector<GrBackendSemaphore>* end_semaphores) override;
  void EndPaint() override;

 private:
  struct SkSurfaceSizePair {
   public:
    SkSurfaceSizePair();
    SkSurfaceSizePair(const SkSurfaceSizePair& other);
    ~SkSurfaceSizePair();
    sk_sp<SkSurface> sk_surface;
    uint64_t bytes_allocated = 0u;
  };

  bool Initialize();
  bool RecreateSwapChain(const gfx::Size& size,
                         sk_sp<SkColorSpace> color_space,
                         gfx::OverlayTransform transform);
  void OnPostSubBufferFinished(OutputSurfaceFrame frame,
                               gfx::SwapResult result);

  VulkanContextProvider* const context_provider_;

  const gpu::SurfaceHandle surface_handle_;
  std::unique_ptr<gpu::VulkanSurface> vulkan_surface_;

  absl::optional<gpu::VulkanSwapChain::ScopedWrite> scoped_write_;

#if DCHECK_IS_ON()
  bool image_modified_ = false;
#endif

  // SkSurfaces for swap chain images.
  std::vector<SkSurfaceSizePair> sk_surface_size_pairs_;

  sk_sp<SkColorSpace> color_space_;

  // The swapchain is new created without a frame which convers the whole area
  // of it.
  bool is_new_swap_chain_ = true;

  std::vector<gfx::Rect> damage_of_images_;

  base::WeakPtrFactory<SkiaOutputDeviceVulkan> weak_ptr_factory_{this};
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_SERVICE_DISPLAY_EMBEDDER_SKIA_OUTPUT_DEVICE_VULKAN_H_
