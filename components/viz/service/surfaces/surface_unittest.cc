// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "base/bind.h"
#include "base/run_loop.h"
#include "cc/test/scheduler_test_common.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "components/viz/common/surfaces/parent_local_surface_id_allocator.h"
#include "components/viz/common/surfaces/subtree_capture_id.h"
#include "components/viz/service/display_embedder/server_shared_bitmap_manager.h"
#include "components/viz/service/frame_sinks/compositor_frame_sink_support.h"
#include "components/viz/service/frame_sinks/frame_sink_manager_impl.h"
#include "components/viz/service/surfaces/pending_copy_output_request.h"
#include "components/viz/service/surfaces/surface.h"
#include "components/viz/test/begin_frame_args_test.h"
#include "components/viz/test/compositor_frame_helpers.h"
#include "components/viz/test/fake_external_begin_frame_source.h"
#include "components/viz/test/mock_compositor_frame_sink_client.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/size.h"

namespace viz {
namespace {

constexpr FrameSinkId kArbitraryFrameSinkId(1, 1);
constexpr bool kIsRoot = true;

TEST(SurfaceTest, PresentationCallback) {
  constexpr gfx::Size kSurfaceSize(300, 300);
  constexpr gfx::Rect kDamageRect(0, 0);
  const LocalSurfaceId local_surface_id(6, base::UnguessableToken::Create());

  ServerSharedBitmapManager shared_bitmap_manager;
  FrameSinkManagerImpl frame_sink_manager{
      FrameSinkManagerImpl::InitParams(&shared_bitmap_manager)};
  MockCompositorFrameSinkClient client;
  auto support = std::make_unique<CompositorFrameSinkSupport>(
      &client, &frame_sink_manager, kArbitraryFrameSinkId, kIsRoot);
  uint32_t frame_token = 0;
  {
    CompositorFrame frame =
        CompositorFrameBuilder()
            .AddRenderPass(gfx::Rect(kSurfaceSize), kDamageRect)
            .Build();
    frame_token = frame.metadata.frame_token;
    ASSERT_NE(frame_token, 0u);
    EXPECT_CALL(client, DidReceiveCompositorFrameAck(testing::_)).Times(1);
    support->SubmitCompositorFrame(local_surface_id, std::move(frame));
    testing::Mock::VerifyAndClearExpectations(&client);
  }

  {
    // Replaces previous frame. The previous frame with token 1 will be
    // discarded.
    CompositorFrame frame =
        CompositorFrameBuilder()
            .AddRenderPass(gfx::Rect(kSurfaceSize), kDamageRect)
            .Build();
    EXPECT_CALL(client, DidReceiveCompositorFrameAck(testing::_)).Times(1);
    support->SubmitCompositorFrame(local_surface_id, std::move(frame));
    ASSERT_EQ(1u, support->timing_details().size());
    EXPECT_EQ(frame_token, support->timing_details().begin()->first);
    testing::Mock::VerifyAndClearExpectations(&client);
  }
}

TEST(SurfaceTest, SurfaceIds) {
  for (size_t i = 0; i < 3; ++i) {
    ParentLocalSurfaceIdAllocator allocator;
    allocator.GenerateId();
    LocalSurfaceId id1 = allocator.GetCurrentLocalSurfaceId();
    allocator.GenerateId();
    LocalSurfaceId id2 = allocator.GetCurrentLocalSurfaceId();
    EXPECT_NE(id1, id2);
  }
}

void TestCopyResultCallback(bool* called,
                            base::OnceClosure finished,
                            std::unique_ptr<CopyOutputResult> result) {
  *called = true;
  std::move(finished).Run();
}

// Test that CopyOutputRequests can outlive the current frame and be
// aggregated on the next frame.
TEST(SurfaceTest, CopyRequestLifetime) {
  ServerSharedBitmapManager shared_bitmap_manager;
  FrameSinkManagerImpl frame_sink_manager{
      FrameSinkManagerImpl::InitParams(&shared_bitmap_manager)};
  SurfaceManager* surface_manager = frame_sink_manager.surface_manager();
  auto support = std::make_unique<CompositorFrameSinkSupport>(
      nullptr, &frame_sink_manager, kArbitraryFrameSinkId, kIsRoot);

  LocalSurfaceId local_surface_id(6, base::UnguessableToken::Create());
  SurfaceId surface_id(kArbitraryFrameSinkId, local_surface_id);
  CompositorFrame frame = MakeDefaultCompositorFrame();
  support->SubmitCompositorFrame(local_surface_id, std::move(frame));
  Surface* surface = surface_manager->GetSurfaceForId(surface_id);
  ASSERT_TRUE(surface);

  bool copy_called = false;
  base::RunLoop copy_runloop;
  support->RequestCopyOfOutput(PendingCopyOutputRequest{
      local_surface_id, SubtreeCaptureId(),
      std::make_unique<CopyOutputRequest>(
          CopyOutputRequest::ResultFormat::RGBA,
          CopyOutputRequest::ResultDestination::kSystemMemory,
          base::BindOnce(&TestCopyResultCallback, &copy_called,
                         copy_runloop.QuitClosure()))});
  surface->TakeCopyOutputRequestsFromClient();
  EXPECT_TRUE(surface_manager->GetSurfaceForId(surface_id));
  EXPECT_FALSE(copy_called);

  int max_frame = 3, start_id = 200;
  for (int i = 0; i < max_frame; ++i) {
    frame = CompositorFrameBuilder().Build();
    frame.render_pass_list.push_back(CompositorRenderPass::Create());
    frame.render_pass_list.back()->id =
        CompositorRenderPassId{i * 3 + start_id};
    frame.render_pass_list.push_back(CompositorRenderPass::Create());
    frame.render_pass_list.back()->id =
        CompositorRenderPassId{i * 3 + start_id + 1};
    frame.render_pass_list.push_back(CompositorRenderPass::Create());
    frame.render_pass_list.back()->SetNew(
        CompositorRenderPassId{i * 3 + start_id + 2}, gfx::Rect(0, 0, 20, 20),
        gfx::Rect(), gfx::Transform());
    support->SubmitCompositorFrame(local_surface_id, std::move(frame));
  }

  CompositorRenderPassId last_pass_id{(max_frame - 1) * 3 + start_id + 2};
  // The copy request should stay on the Surface until TakeCopyOutputRequests
  // is called.
  EXPECT_FALSE(copy_called);
  EXPECT_EQ(
      1u,
      surface->GetActiveFrame().render_pass_list.back()->copy_requests.size());

  Surface::CopyRequestsMap copy_requests;
  surface->TakeCopyOutputRequests(&copy_requests);
  EXPECT_EQ(1u, copy_requests.size());
  // Last (root) pass should receive copy request.
  ASSERT_EQ(1u, copy_requests.count(last_pass_id));
  EXPECT_FALSE(copy_called);
  copy_requests.clear();  // Deleted requests will auto-send an empty result.
  copy_runloop.Run();
  EXPECT_TRUE(copy_called);
}

}  // namespace
}  // namespace viz
