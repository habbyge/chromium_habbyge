// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/x11/gl_egl_utility_x11.h"

#include "ui/base/x/visual_picker_glx.h"
#include "ui/base/x/x11_gl_egl_utility.h"
#include "ui/base/x/x11_util.h"
#include "ui/gfx/gpu_extra_info.h"
#include "ui/gfx/linux/gpu_memory_buffer_support_x11.h"
#include "ui/gl/gl_implementation.h"
#include "ui/gl/gl_utils.h"

namespace ui {

GLEGLUtilityX11::GLEGLUtilityX11() = default;
GLEGLUtilityX11::~GLEGLUtilityX11() = default;

void GLEGLUtilityX11::GetAdditionalEGLAttributes(
    EGLenum platform_type,
    std::vector<EGLAttrib>* display_attributes) {
  GetPlatformExtraDisplayAttribs(platform_type, display_attributes);
}

void GLEGLUtilityX11::ChooseEGLAlphaAndBufferSize(EGLint* alpha_size,
                                                  EGLint* buffer_size) {
  ChoosePlatformCustomAlphaAndBufferSize(alpha_size, buffer_size);
}

bool GLEGLUtilityX11::IsTransparentBackgroundSupported() const {
  return ui::IsTransparentBackgroundSupported();
}

void GLEGLUtilityX11::CollectGpuExtraInfo(
    bool enable_native_gpu_memory_buffers,
    gfx::GpuExtraInfo& gpu_extra_info) const {
  // TODO(https://crbug.com/1031269): Enable by default.
  if (enable_native_gpu_memory_buffers) {
    gpu_extra_info.gpu_memory_buffer_support_x11 =
        ui::GpuMemoryBufferSupportX11::GetInstance()->supported_configs();
  }

  if (gl::GetGLImplementation() == gl::kGLImplementationDesktopGL) {
    // Create the VisualPickerGlx singleton now while the GbmSupportX11
    // singleton is busy being created on another thread.
    auto* visual_picker = ui::VisualPickerGlx::GetInstance();

    // With GLX, only BGR(A) buffer formats are supported.  EGL does not have
    // this restriction.
    gpu_extra_info.gpu_memory_buffer_support_x11.erase(
        std::remove_if(gpu_extra_info.gpu_memory_buffer_support_x11.begin(),
                       gpu_extra_info.gpu_memory_buffer_support_x11.end(),
                       [&](gfx::BufferUsageAndFormat usage_and_format) {
                         return visual_picker->GetFbConfigForFormat(
                                    usage_and_format.format) ==
                                x11::Glx::FbConfig{};
                       }),
        gpu_extra_info.gpu_memory_buffer_support_x11.end());
  } else if (gl::GetGLImplementation() == gl::kGLImplementationEGLANGLE) {
    // ANGLE does not yet support EGL_EXT_image_dma_buf_import[_modifiers].
    gpu_extra_info.gpu_memory_buffer_support_x11.clear();
  }
}

bool GLEGLUtilityX11::X11DoesVisualHaveAlphaForTest() const {
  return ui::DoesVisualHaveAlphaForTest();
}

bool GLEGLUtilityX11::HasVisualManager() {
  return true;
}

}  // namespace ui
