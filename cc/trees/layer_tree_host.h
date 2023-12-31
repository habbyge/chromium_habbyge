// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TREES_LAYER_TREE_HOST_H_
#define CC_TREES_LAYER_TREE_HOST_H_

#include <stddef.h>
#include <stdint.h>

#include <limits>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/callback_forward.h"
#include "base/cancelable_callback.h"
#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "cc/base/completion_event.h"
#include "cc/benchmarks/micro_benchmark.h"
#include "cc/benchmarks/micro_benchmark_controller.h"
#include "cc/cc_export.h"
#include "cc/input/browser_controls_state.h"
#include "cc/input/compositor_input_interfaces.h"
#include "cc/input/event_listener_properties.h"
#include "cc/input/input_handler.h"
#include "cc/input/layer_selection_bound.h"
#include "cc/input/scrollbar.h"
#include "cc/layers/layer.h"
#include "cc/layers/layer_collections.h"
#include "cc/layers/layer_list_iterator.h"
#include "cc/metrics/begin_main_frame_metrics.h"
#include "cc/metrics/events_metrics_manager.h"
#include "cc/metrics/frame_sequence_tracker.h"
#include "cc/metrics/web_vital_metrics.h"
#include "cc/paint/node_id.h"
#include "cc/resources/ui_resource_request.h"
#include "cc/trees/browser_controls_params.h"
#include "cc/trees/commit_state.h"
#include "cc/trees/compositor_mode.h"
#include "cc/trees/layer_tree_frame_sink.h"
#include "cc/trees/layer_tree_host_client.h"
#include "cc/trees/layer_tree_settings.h"
#include "cc/trees/mutator_host.h"
#include "cc/trees/paint_holding_reason.h"
#include "cc/trees/presentation_time_callback_buffer.h"
#include "cc/trees/proxy.h"
#include "cc/trees/swap_promise.h"
#include "cc/trees/swap_promise_manager.h"
#include "cc/trees/target_property.h"
#include "cc/trees/viewport_layers.h"
#include "cc/trees/viewport_property_ids.h"
#include "components/viz/common/resources/resource_format.h"
#include "components/viz/common/surfaces/local_surface_id.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "ui/gfx/delegated_ink_metadata.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/overlay_transform.h"

namespace gfx {
struct PresentationFeedback;
class RenderingPipeline;
}

namespace cc {

class DocumentTransitionRequest;
class HeadsUpDisplayLayer;
class LayerTreeHostImpl;
class LayerTreeHostImplClient;
class LayerTreeHostSingleThreadClient;
class LayerTreeMutator;
class MutatorEvents;
class MutatorHost;
class PaintWorkletLayerPainter;
class RasterDarkModeFilter;
class RenderFrameMetadataObserver;
class RenderingStatsInstrumentation;
class TaskGraphRunner;
class UIResourceManager;
class UkmRecorderFactory;

struct CommitState;
struct CompositorCommitData;
struct OverscrollBehavior;
struct RenderingStats;

// Returned from LayerTreeHost::DeferMainFrameUpdate. Automatically un-defers on
// destruction.
class CC_EXPORT ScopedDeferMainFrameUpdate {
 public:
  explicit ScopedDeferMainFrameUpdate(LayerTreeHost* host);
  ~ScopedDeferMainFrameUpdate();

 private:
  base::WeakPtr<LayerTreeHost> host_;
};

class CC_EXPORT LayerTreeHost : public MutatorHostClient {
 public:
  struct CC_EXPORT InitParams {
    InitParams();
    ~InitParams();

    InitParams(InitParams&&);
    InitParams& operator=(InitParams&&);

    LayerTreeHostClient* client = nullptr;
    LayerTreeHostSchedulingClient* scheduling_client = nullptr;
    TaskGraphRunner* task_graph_runner = nullptr;
    LayerTreeSettings const* settings = nullptr;
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner;
    MutatorHost* mutator_host = nullptr;
    RasterDarkModeFilter* dark_mode_filter = nullptr;
    gfx::RenderingPipeline* main_thread_pipeline = nullptr;
    gfx::RenderingPipeline* compositor_thread_pipeline = nullptr;

    // The image worker task runner is used to schedule image decodes. The
    // compositor thread may make sync calls to this thread, analogous to the
    // raster worker threads.
    scoped_refptr<base::SequencedTaskRunner> image_worker_task_runner;

    std::unique_ptr<UkmRecorderFactory> ukm_recorder_factory;
  };

  // Constructs a LayerTreeHost with a compositor thread where scrolling and
  // animation take place. This is used for the web compositor in the renderer
  // process to move work off the main thread which javascript can dominate.
  static std::unique_ptr<LayerTreeHost> CreateThreaded(
      scoped_refptr<base::SingleThreadTaskRunner> impl_task_runner,
      InitParams params);

  // Constructs a LayerTreeHost without a separate compositor thread, but which
  // behaves and looks the same as a threaded compositor externally, with the
  // exception of the additional client interface. This is used in other cases
  // where the main thread creating this instance can be expected to not become
  // blocked, so moving work to another thread and the overhead it adds are not
  // required.
  static std::unique_ptr<LayerTreeHost> CreateSingleThreaded(
      LayerTreeHostSingleThreadClient* single_thread_client,
      InitParams params);

  LayerTreeHost(const LayerTreeHost&) = delete;
  virtual ~LayerTreeHost();

  LayerTreeHost& operator=(const LayerTreeHost&) = delete;

  // Returns the process global unique identifier for this LayerTreeHost.
  int GetId() const;

  // The commit state for the frame being assembled by the compositor host.
  CommitState* pending_commit_state() {
    DCHECK(task_runner_provider_->IsMainThread());
    return pending_commit_state_.get();
  }
  const CommitState* pending_commit_state() const {
    DCHECK(task_runner_provider_->IsMainThread());
    return pending_commit_state_.get();
  }

  // The commit state for the frame being committed by the compositor.
  CommitState* active_commit_state() {
    DCHECK(task_runner_provider_->IsImplThread());
    return active_commit_state_.get();
  }
  const CommitState* active_commit_state() const {
    DCHECK(task_runner_provider_->IsImplThread());
    return active_commit_state_.get();
  }

  // The current source frame number. This is incremented for each main frame
  // update(commit) pushed to the compositor thread. The initial frame number
  // is 0, and it is incremented once commit is completed (which is before the
  // compositor-thread-side submits its frame for the commit).
  int SourceFrameNumber() const;

  // Returns the UIResourceManager used to create UIResources for
  // UIResourceLayers pushed to the LayerTree.
  UIResourceManager* GetUIResourceManager();

  // Returns the TaskRunnerProvider used to access the main and compositor
  // thread task runners.
  TaskRunnerProvider* GetTaskRunnerProvider();

  // Returns the settings used by this host. These settings are constants given
  // at startup.
  const LayerTreeSettings& GetSettings() const;

  // Sets the LayerTreeMutator interface used to directly mutate the compositor
  // state on the compositor thread. (Compositor-Worker)
  void SetLayerTreeMutator(std::unique_ptr<LayerTreeMutator> mutator);

  // Sets the LayerTreePainter interface used to dispatch the JS paint callback
  // to a worklet thread.
  void SetPaintWorkletLayerPainter(
      std::unique_ptr<PaintWorkletLayerPainter> painter);

  // Attachs a SwapPromise to the Layer tree, that passes through the
  // LayerTreeHost and LayerTreeHostImpl with the next commit and frame
  // submission, which can be used to observe that progress. This also
  // causes a main frame to be requested.
  // See swap_promise.h for how to use SwapPromise.
  void QueueSwapPromise(std::unique_ptr<SwapPromise> swap_promise);

  // Returns the SwapPromiseManager, used to insert SwapPromises dynamically
  // when a main frame is requested.
  SwapPromiseManager* GetSwapPromiseManager();

  std::unique_ptr<EventsMetricsManager::ScopedMonitor>
  GetScopedEventMetricsMonitor(
      EventsMetricsManager::ScopedMonitor::DoneCallback done_callback);
  void ClearEventsMetrics();

  size_t saved_events_metrics_count_for_testing() const {
    return events_metrics_manager_.saved_events_metrics_count_for_testing();
  }

  // Visibility and LayerTreeFrameSink -------------------------------

  // Sets or gets if the LayerTreeHost is visible. When not visible it will:
  // - Not request a new LayerTreeFrameSink from the client.
  // - Stop submitting frames to the display compositor.
  // - Stop producing main frames and committing them.
  // The LayerTreeHost is not visible when first created, so this must be called
  // to make it visible before it will attempt to start producing output.
  void SetVisible(bool visible);
  bool IsVisible() const;

  // Called in response to a LayerTreeFrameSink request made to the client
  // using LayerTreeHostClient::RequestNewLayerTreeFrameSink. The client will
  // be informed of the LayerTreeFrameSink initialization status using
  // DidInitializaLayerTreeFrameSink or DidFailToInitializeLayerTreeFrameSink.
  // The request is completed when the host successfully initializes an
  // LayerTreeFrameSink.
  void SetLayerTreeFrameSink(
      std::unique_ptr<LayerTreeFrameSink> layer_tree_frame_sink);

  // Forces the host to immediately release all references to the
  // LayerTreeFrameSink, if any. Can be safely called any time, but the
  // compositor should not be visible.
  std::unique_ptr<LayerTreeFrameSink> ReleaseLayerTreeFrameSink();

  // Frame Scheduling (main and compositor frames) requests -------

  // Requests a main frame update even if no content has changed. This is used,
  // for instance in the case of RequestAnimationFrame from blink to ensure the
  // main frame update is run on the next tick without pre-emptively forcing a
  // full commit synchronization or layer updates.
  void SetNeedsAnimate();

  // Calls SetNeedsAnimate() if there is no main frame already in progress.
  void SetNeedsAnimateIfNotInsideMainFrame();

  // Requests a main frame update and also ensure that the host pulls layer
  // updates from the client, even if no content might have changed, without
  // forcing a full commit synchronization.
  virtual void SetNeedsUpdateLayers();

  // Requests that the next main frame update performs a full commit
  // synchronization.
  virtual void SetNeedsCommit();

  // Notifies that a new viz::LocalSurfaceId has been set, ahead of it becoming
  // activated. Requests that the compositor thread does not produce new frames
  // until it has activated.
  virtual void SetTargetLocalSurfaceId(
      const viz::LocalSurfaceId& target_local_surface_id);

  // Returns true after SetNeedsAnimate(), SetNeedsUpdateLayers() or
  // SetNeedsCommit(), until it is satisfied.
  bool RequestedMainFramePendingForTesting() const;

  // Requests that the next frame re-chooses crisp raster scales for all layers.
  void SetNeedsRecalculateRasterScales();

  // Returns true if a main frame with commit synchronization has been
  // requested.
  bool CommitRequested() const;

  // Prevents the compositor from requesting main frame updates from the client
  // until the ScopedDeferMainFrameUpdate object is destroyed, or
  // StopDeferringCommits is called.
  std::unique_ptr<ScopedDeferMainFrameUpdate> DeferMainFrameUpdate();

  // Notification that the proxy started or stopped deferring main frame updates
  void OnDeferMainFrameUpdatesChanged(bool);

  // Prevents the proxy from committing the layer tree to the compositor,
  // while still allowing main frame lifecycle updates. |timeout|
  // is the interval after which commits will restart if nothing stops
  // deferring sooner. If multiple calls are made to StartDeferringCommits
  // while deferal is active, the first timeout continues to apply.
  bool StartDeferringCommits(base::TimeDelta timeout,
                             PaintHoldingReason reason);

  // Stop deferring commits immediately.
  void StopDeferringCommits(PaintHoldingCommitTrigger);

  // Returns true if commits are currently deferred.
  bool IsDeferringCommits() const;

  // Notification that the proxy started or stopped deferring commits.
  void OnDeferCommitsChanged(bool defer_status, PaintHoldingReason reason);

  // Returns whether there are any outstanding ScopedDeferMainFrameUpdate,
  // though commits may be deferred also when the local_surface_id_from_parent()
  // is not valid.
  bool defer_main_frame_update() const {
    return defer_main_frame_update_count_;
  }

  // Synchronously performs a main frame update and layer updates. Used only in
  // single threaded mode when the compositor's internal scheduling is disabled.
  void LayoutAndUpdateLayers();

  // Synchronously performs a complete main frame update, commit and compositor
  // frame. Used only in single threaded mode when the compositor's internal
  // scheduling is disabled.
  void CompositeForTest(base::TimeTicks frame_begin_time, bool raster);

  // Requests a redraw (compositor frame) for the given rect.
  void SetNeedsRedrawRect(const gfx::Rect& damage_rect);

  // Requests a main frame (including layer updates) and ensures that this main
  // frame results in a redraw for the complete viewport when producing the
  // CompositorFrame.
  void SetNeedsCommitWithForcedRedraw();

  // Input Handling ---------------------------------------------

  // Sets the state of the browser controls. (Used for URL bar animations).
  void UpdateBrowserControlsState(BrowserControlsState constraints,
                                  BrowserControlsState current,
                                  bool animate);

  // Returns the delegate that the input handler uses to communicate with the
  // LayerTreeHostImpl on the compositor thread. Must be dereferenced only on
  // the input handling thread.
  const base::WeakPtr<CompositorDelegateForInput>& GetDelegateForInput() const;

  // Debugging and benchmarks ---------------------------------
  void SetDebugState(const LayerTreeDebugState& debug_state);
  const LayerTreeDebugState& GetDebugState() const;

  // Returns the id of the benchmark on success, 0 otherwise.
  int ScheduleMicroBenchmark(const std::string& benchmark_name,
                             base::Value settings,
                             MicroBenchmark::DoneCallback callback);

  // Returns true if the message was successfully delivered and handled.
  bool SendMessageToMicroBenchmark(int id, base::Value message);

  // When the main thread informs the compositor thread that it is ready to
  // commit, generally it would remain blocked until the main thread state is
  // copied to the pending tree. Calling this would ensure that the main thread
  // remains blocked until the pending tree is activated.
  void SetNextCommitWaitsForActivation();

  // Registers a callback that is run when the next frame successfully makes it
  // to the screen (it's entirely possible some frames may be dropped between
  // the time this is called and the callback is run).
  void RequestPresentationTimeForNextFrame(
      PresentationTimeCallbackBuffer::MainCallback callback);

  // Registers a callback that is run when any ongoing scroll-animation ends. If
  // there are no ongoing animations, then the callback is run immediately.
  void RequestScrollAnimationEndNotification(base::OnceClosure callback);

  // Layer tree accessors and modifiers ------------------------

  // Sets or gets the root of the Layer tree. Children of the root Layer are
  // attached to it and will be added/removed along with the root Layer. The
  // LayerTreeHost retains ownership of a reference to the root Layer.
  void SetRootLayer(scoped_refptr<Layer> root_layer);
  Layer* root_layer() { return pending_commit_state()->root_layer.get(); }
  const Layer* root_layer() const {
    return pending_commit_state()->root_layer.get();
  }

  // Sets the collection of viewport property ids, defined to allow viewport
  // pinch-zoom etc. on the compositor thread. This is set only on the
  // main-frame's compositor, i.e., will be unset in OOPIF and UI compositors.
  void RegisterViewportPropertyIds(const ViewportPropertyIds&);

  ViewportPropertyIds ViewportPropertyIdsForTesting() const {
    return pending_commit_state()->viewport_property_ids;
  }
  Layer* InnerViewportScrollLayerForTesting();
  Layer* OuterViewportScrollLayerForTesting();

  ElementId OuterViewportScrollElementId() const;

  // Sets or gets the position of touch handles for a text selection. These are
  // submitted to the display compositor along with the Layer tree's contents
  // allowing it to present the selection handles. This is done because the
  // handles are a UI widget above, and not clipped to, the viewport of this
  // LayerTreeHost.
  void RegisterSelection(const LayerSelection& selection);
  const LayerSelection& selection() const {
    return pending_commit_state()->selection;
  }

  // Sets or gets if the client has any scroll event handlers registered. This
  // allows the threaded compositor to prioritize main frames even when
  // servicing a touch scroll on the compositor thread, in order to give the
  // event handler a chance to be part of each frame.
  void SetHaveScrollEventHandlers(bool have_event_handlers);
  bool have_scroll_event_handlers() const {
    return pending_commit_state()->have_scroll_event_handlers;
  }

  // Set or get what event handlers exist on the layer tree in order to inform
  // the compositor thread if it is able to handle an input event, or it needs
  // to pass it to the main thread to be handled. The class is the type of input
  // event, and for each class there is a properties defining if the compositor
  // thread can handle the event.
  void SetEventListenerProperties(EventListenerClass event_class,
                                  EventListenerProperties event_properties);
  EventListenerProperties event_listener_properties(
      EventListenerClass event_class) const {
    DCHECK(task_runner_provider_->IsMainThread());
    return pending_commit_state()
        ->event_listener_properties[static_cast<size_t>(event_class)];
  }

  // Indicates that its acceptable to throttle the frame rate for this content
  // to prioritize lower power/CPU use.
  void SetEnableFrameRateThrottling(bool enable_frame_rate_throttling);

  void SetViewportRectAndScale(
      const gfx::Rect& device_viewport_rect,
      float device_scale_factor,
      const viz::LocalSurfaceId& local_surface_id_from_parent);

  // VisualDeviceViewportIntersectionRect is the intersection of this
  // compositor's viewport with the "visible size", aka this compositor's
  // viewport intersection with the global viewport (i.e.
  // VisualDeviceViewportSize). It is also specified in the device viewport
  // coordinate space.
  void SetVisualDeviceViewportIntersectionRect(
      const gfx::Rect& intersection_rect);

  // VisualDeviceViewportSize is the size of the global viewport across all
  // compositors that are part of the scene that this compositor contributes to
  // (i.e. the visual viewport), allowing for that scene to be broken up into
  // multiple compositors that each contribute to the whole (e.g. cross-origin
  // iframes are isolated from each other). This is a size instead of a rect
  // because each compositor doesn't know its position relative to other
  // compositors. This is specified in device viewport coordinate space.
  void SetVisualDeviceViewportSize(const gfx::Size&);

  gfx::Rect device_viewport_rect() const {
    return pending_commit_state()->device_viewport_rect;
  }

  void UpdateViewportIsMobileOptimized(bool is_viewport_mobile_optimized);

  // Returns if the viewport is considered to be mobile optimized.
  bool IsMobileOptimized() const;

  void SetPrefersReducedMotion(bool prefers_reduced_motion);

  void SetMayThrottleIfUndrawnFrames(bool may_throttle_if_undrawn_frames);
  bool GetMayThrottleIfUndrawnFramesForTesting() const;

  void SetBrowserControlsParams(const BrowserControlsParams& params);
  void SetBrowserControlsShownRatio(float top_ratio, float bottom_ratio);

  void SetOverscrollBehavior(const OverscrollBehavior& overscroll_behavior);
  const OverscrollBehavior& overscroll_behavior() const {
    return pending_commit_state()->overscroll_behavior;
  }

  void SetPageScaleFactorAndLimits(float page_scale_factor,
                                   float min_page_scale_factor,
                                   float max_page_scale_factor);
  float page_scale_factor() const {
    return pending_commit_state()->page_scale_factor;
  }
  float min_page_scale_factor() const {
    return pending_commit_state()->min_page_scale_factor;
  }
  float max_page_scale_factor() const {
    return pending_commit_state()->max_page_scale_factor;
  }

  void set_background_color(SkColor color) {
    pending_commit_state()->background_color = color;
  }
  SkColor background_color() const {
    return pending_commit_state()->background_color;
  }

  void set_display_transform_hint(gfx::OverlayTransform hint) {
    pending_commit_state()->display_transform_hint = hint;
  }
  gfx::OverlayTransform display_transform_hint() const {
    return pending_commit_state()->display_transform_hint;
  }

  void StartPageScaleAnimation(const gfx::Vector2d& target_offset,
                               bool use_anchor,
                               float scale,
                               base::TimeDelta duration);
  bool HasPendingPageScaleAnimation() const;

  float device_scale_factor() const {
    return pending_commit_state()->device_scale_factor;
  }

  void SetRecordingScaleFactor(float recording_scale_factor);

  // If this LayerTreeHost needs a valid viz::LocalSurfaceId then commits will
  // be deferred until a valid viz::LocalSurfaceId is provided.
  void SetLocalSurfaceIdFromParent(
      const viz::LocalSurfaceId& local_surface_id_from_parent);

  const viz::LocalSurfaceId& local_surface_id_from_parent() const {
    return pending_commit_state()->local_surface_id_from_parent;
  }

  // Requests the allocation of a new LocalSurfaceId on the compositor thread.
  void RequestNewLocalSurfaceId();

  // Returns the current state of the new LocalSurfaceId request and resets
  // the state.
  bool new_local_surface_id_request_for_testing() const {
    return pending_commit_state()->new_local_surface_id_request;
  }

  // Records the amount of time spent performing an update in response to new
  // blink::VisualProperties.
  void SetVisualPropertiesUpdateDuration(
      base::TimeDelta visual_properties_update_duration);

  void SetDisplayColorSpaces(
      const gfx::DisplayColorSpaces& display_color_spaces);
  const gfx::DisplayColorSpaces& display_color_spaces() const {
    return pending_commit_state()->display_color_spaces;
  }

  bool HasCompositorDrivenScrollAnimationForTesting() const {
    return scroll_animation_.in_progress;
  }

  // This layer tree may be embedded in a hierarchy that has page scale
  // factor controlled at the top level. We represent that scale here as
  // 'external_page_scale_factor', a value that affects raster scale in the
  // same way that page_scale_factor does, but doesn't affect any geometry
  // calculations.
  void SetExternalPageScaleFactor(float page_scale_factor,
                                  bool is_external_pinch_gesture_active);
  bool is_external_pinch_gesture_active_for_testing() {
    return pending_commit_state()->is_external_pinch_gesture_active;
  }

  // Requests that we force send RenderFrameMetadata with the next frame.
  void RequestForceSendMetadata() {
    pending_commit_state()->force_send_metadata_request = true;
  }

  // Returns the state of |force_send_metadata_request_| and resets the
  // variable to false.
  bool TakeForceSendMetadataRequest();

  // Used externally by blink for setting the PropertyTrees when
  // UseLayerLists() is true.
  PropertyTrees* property_trees() { return &property_trees_; }
  const PropertyTrees* property_trees() const { return &property_trees_; }

  void SetPropertyTreesForTesting(const PropertyTrees* property_trees);

  void SetNeedsDisplayOnAllLayers();

  void RegisterLayer(Layer* layer);
  void UnregisterLayer(Layer* layer);
  Layer* LayerById(int id);

  bool PaintContent(const LayerList& update_layer_list);
  bool in_paint_layer_contents() const { return in_paint_layer_contents_; }

  bool in_commit() const {
    return commit_completion_event_ && !commit_completion_event_->IsSignaled();
  }

  void SetHasCopyRequest(bool has_copy_request);
  bool has_copy_request() const { return has_copy_request_; }

  void AddSurfaceRange(const viz::SurfaceRange& surface_range);
  void RemoveSurfaceRange(const viz::SurfaceRange& surface_range);

  // Marks or unmarks a layer are needing PushPropertiesTo in the next commit.
  // These are internal methods, called from the Layer itself when changing a
  // property or completing a PushPropertiesTo.
  void AddLayerShouldPushProperties(Layer* layer);

  void SetPageScaleFromImplSide(float page_scale);
  void SetElasticOverscrollFromImplSide(gfx::Vector2dF elastic_overscroll);
  gfx::Vector2dF elastic_overscroll() const {
    return pending_commit_state()->elastic_overscroll;
  }

  // Ensures a HUD layer exists if it is needed, and updates the HUD bounds and
  // position. If a HUD layer exists but is no longer needed, it is destroyed.
  void UpdateHudLayer(bool show_hud_info);
  HeadsUpDisplayLayer* hud_layer() { return hud_layer_.get(); }
  const HeadsUpDisplayLayer* hud_layer() const { return hud_layer_.get(); }
  bool is_hud_layer(const Layer*) const;

  virtual void SetNeedsFullTreeSync();
  void ResetNeedsFullTreeSyncForTesting();
  bool needs_full_tree_sync() const {
    return pending_commit_state()->needs_full_tree_sync;
  }

  void SetPropertyTreesNeedRebuild();

  void PushPropertyTreesTo(LayerTreeImpl* tree_impl);
  static void PushLayerTreePropertiesTo(CommitState* commit_state,
                                        LayerTreeImpl* tree_impl);
  void PushLayerTreeHostPropertiesTo(LayerTreeHostImpl* host_impl);
  void MoveChangeTrackingToLayers(LayerTreeImpl* tree_impl);

  MutatorHost* mutator_host() { return mutator_host_; }

  // Returns the layer with the given |element_id|. In layer-list mode, only
  // scrollable layers are registered in this map.
  Layer* LayerByElementId(ElementId element_id);
  const Layer* LayerByElementId(ElementId element_id) const;

  void RegisterElement(ElementId element_id,
                       Layer* layer);
  void UnregisterElement(ElementId element_id);

  // For layer list mode only.
  void UpdateActiveElements();

  void SetElementIdsForTesting();
  void BuildPropertyTreesForTesting();
  void ClearActiveCommitStateForTesting() { active_commit_state_ = nullptr; }

  // Layer iterators.
  LayerListIterator begin();
  LayerListConstIterator begin() const;
  LayerListIterator end();
  LayerListConstIterator end() const;
  LayerListReverseIterator rbegin();
  LayerListReverseConstIterator rbegin() const;
  LayerListReverseIterator rend();
  LayerListReverseConstIterator rend() const;

  // LayerTreeHost interface to Proxy.
  void WillBeginMainFrame();
  void DidBeginMainFrame();
  void BeginMainFrame(const viz::BeginFrameArgs& args);
  void BeginMainFrameNotExpectedSoon();
  void BeginMainFrameNotExpectedUntil(base::TimeTicks time);
  void AnimateLayers(base::TimeTicks monotonic_frame_begin_time);
  void RequestMainFrameUpdate(bool report_metrics);
  void FinishCommitOnImplThread(LayerTreeHostImpl* host_impl);
  // If has_updates is true, returns the CommitState that will drive the commit.
  // Otherwise, returns nullptr.
  CommitState* WillCommit(std::unique_ptr<CompletionEvent> completion,
                          bool has_updates);
  CommitState* ActivateCommitState();
  void WaitForCommitCompletion();
  void CommitComplete();
  void RequestNewLayerTreeFrameSink();
  void DidInitializeLayerTreeFrameSink();
  void DidFailToInitializeLayerTreeFrameSink();
  virtual std::unique_ptr<LayerTreeHostImpl> CreateLayerTreeHostImpl(
      LayerTreeHostImplClient* client);
  void DidLoseLayerTreeFrameSink();
  void DidCommitAndDrawFrame() { client_->DidCommitAndDrawFrame(); }
  void DidReceiveCompositorFrameAck() {
    client_->DidReceiveCompositorFrameAck();
  }
  bool UpdateLayers();
  void DidPresentCompositorFrame(
      uint32_t frame_token,
      std::vector<PresentationTimeCallbackBuffer::MainCallback> callbacks,
      const gfx::PresentationFeedback& feedback);
  // Called when the compositor completed page scale animation.
  void DidCompletePageScaleAnimation();
  void ApplyCompositorChanges(CompositorCommitData* commit_data);
  void ApplyMutatorEvents(std::unique_ptr<MutatorEvents> events);
  void RecordStartOfFrameMetrics();
  void RecordEndOfFrameMetrics(base::TimeTicks frame_begin_time,
                               ActiveFrameSequenceTrackers trackers);
  void NotifyThroughputTrackerResults(CustomTrackerResults results);
  void NotifyTransitionRequestsFinished(
      const std::vector<uint32_t>& sequence_ids);
  // Called during impl side initialization.
  gfx::RenderingPipeline* TakeMainPipeline();
  gfx::RenderingPipeline* TakeCompositorPipeline();

  LayerTreeHostClient* client() { return client_; }
  LayerTreeHostSchedulingClient* scheduling_client() {
    return scheduling_client_;
  }

  void CollectRenderingStats(RenderingStats* stats) const;

  RenderingStatsInstrumentation* rendering_stats_instrumentation() const {
    return rendering_stats_instrumentation_.get();
  }

  Proxy* proxy() { return proxy_.get(); }

  bool IsSingleThreaded() const;
  bool IsThreaded() const;

  // Indicates whether this host is configured to use layer lists
  // rather than layer trees. This also implies that property trees
  // are always already built and so cc doesn't have to build them.
  bool IsUsingLayerLists() const;

  // MutatorHostClient implementation.
  bool IsElementInPropertyTrees(ElementId element_id,
                                ElementListType list_type) const override;
  void SetMutatorsNeedCommit() override;
  void SetMutatorsNeedRebuildPropertyTrees() override;
  void SetElementFilterMutated(ElementId element_id,
                               ElementListType list_type,
                               const FilterOperations& filters) override;
  void SetElementBackdropFilterMutated(
      ElementId element_id,
      ElementListType list_type,
      const FilterOperations& backdrop_filters) override;
  void SetElementOpacityMutated(ElementId element_id,
                                ElementListType list_type,
                                float opacity) override;
  void SetElementTransformMutated(ElementId element_id,
                                  ElementListType list_type,
                                  const gfx::Transform& transform) override;
  void SetElementScrollOffsetMutated(
      ElementId element_id,
      ElementListType list_type,
      const gfx::Vector2dF& scroll_offset) override;

  void ElementIsAnimatingChanged(const PropertyToElementIdMap& element_id_map,
                                 ElementListType list_type,
                                 const PropertyAnimationState& mask,
                                 const PropertyAnimationState& state) override;
  void MaximumScaleChanged(ElementId element_id,
                           ElementListType list_type,
                           float maximum_scale) override;

  void OnCustomPropertyMutated(
      PaintWorkletInput::PropertyKey property_key,
      PaintWorkletInput::PropertyValue property_value) override {}

  void ScrollOffsetAnimationFinished() override {}
  gfx::Vector2dF GetScrollOffsetForAnimation(
      ElementId element_id) const override;

  void NotifyAnimationWorkletStateChange(AnimationWorkletMutationState state,
                                         ElementListType tree_type) override {}

  void QueueImageDecode(const PaintImage& image,
                        base::OnceCallback<void(bool)> callback);
  void ImageDecodesFinished(const std::vector<std::pair<int, bool>>& results);

  void RequestBeginMainFrameNotExpected(bool new_state);

  float recording_scale_factor() const { return recording_scale_factor_; }

  void SetSourceURL(ukm::SourceId source_id, const GURL& url);
  base::ReadOnlySharedMemoryRegion CreateSharedMemoryForSmoothnessUkm();

  void SetRenderFrameObserver(
      std::unique_ptr<RenderFrameMetadataObserver> observer);

  std::string LayersAsString() const;

  // Captures the on-screen text content, if success, fills the associated
  // NodeInfo in |content| and return true, otherwise return false.
  bool CaptureContent(std::vector<NodeInfo>* content) const;

  void SetDelegatedInkMetadata(
      std::unique_ptr<gfx::DelegatedInkMetadata> metadata);
  gfx::DelegatedInkMetadata* DelegatedInkMetadataForTesting() {
    return pending_commit_state()->delegated_ink_metadata.get();
  }

  void DidObserveFirstScrollDelay(base::TimeDelta first_scroll_delay,
                                  base::TimeTicks first_scroll_timestamp);

  void AddDocumentTransitionRequest(
      std::unique_ptr<DocumentTransitionRequest> request);

  std::vector<base::OnceClosure> TakeDocumentTransitionCallbacksForTesting();

  // Returns a percentage representing average throughput of last X seconds.
  uint32_t GetAverageThroughput() const;

 protected:
  LayerTreeHost(InitParams params, CompositorMode mode);

  void InitializeThreaded(
      scoped_refptr<base::SingleThreadTaskRunner> main_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> impl_task_runner);
  void InitializeSingleThreaded(
      LayerTreeHostSingleThreadClient* single_thread_client,
      scoped_refptr<base::SingleThreadTaskRunner> main_task_runner);
  void InitializeForTesting(
      std::unique_ptr<TaskRunnerProvider> task_runner_provider,
      std::unique_ptr<Proxy> proxy_for_testing);
  void SetTaskRunnerProviderForTesting(
      std::unique_ptr<TaskRunnerProvider> task_runner_provider);
  void SetUIResourceManagerForTesting(
      std::unique_ptr<UIResourceManager> ui_resource_manager);

  // task_graph_runner() returns a valid value only until the LayerTreeHostImpl
  // is created in CreateLayerTreeHostImpl().
  TaskGraphRunner* task_graph_runner() const { return task_graph_runner_; }

  void OnCommitForSwapPromises();

  void RecordGpuRasterizationHistogram(const LayerTreeHostImpl* host_impl);

  MicroBenchmarkController micro_benchmark_controller_;

  // The pointer that input uses to communicate with the layer tree host impl.
  // Must be dereferenced only from the input-handling thread.
  base::WeakPtr<CompositorDelegateForInput> compositor_delegate_weak_ptr_;

  scoped_refptr<base::SequencedTaskRunner> image_worker_task_runner_;
  std::unique_ptr<UkmRecorderFactory> ukm_recorder_factory_;

 private:
  friend class LayerTreeHostSerializationTest;
  friend class ScopedDeferMainFrameUpdate;

  // This is the number of consecutive frames in which we want the content to be
  // free of slow-paths before toggling the flag.
  enum { kNumFramesToConsiderBeforeRemovingSlowPathFlag = 60 };

  void ApplyViewportChanges(const CompositorCommitData& commit_data);
  void ApplyPageScaleDeltaFromImplSide(float page_scale_delta);
  void InitializeProxy(std::unique_ptr<Proxy> proxy);

  bool DoUpdateLayers();

  void UpdateDeferMainFrameUpdateInternal();

  // Preemptively applies the scroll offset and delta before sending it to the
  // client. This lets the client skip a commit if the value does not change.
  void UpdateScrollOffsetFromImpl(
      const ElementId&,
      const gfx::Vector2dF& delta,
      const absl::optional<TargetSnapAreaElementIds>&);

  const CompositorMode compositor_mode_;

  std::unique_ptr<UIResourceManager> ui_resource_manager_;

  LayerTreeHostClient* client_;
  LayerTreeHostSchedulingClient* scheduling_client_;
  std::unique_ptr<Proxy> proxy_;
  std::unique_ptr<TaskRunnerProvider> task_runner_provider_;

  std::unique_ptr<RenderingStatsInstrumentation>
      rendering_stats_instrumentation_;

  std::unique_ptr<CommitState> pending_commit_state_;
  std::unique_ptr<CommitState> active_commit_state_;

  SwapPromiseManager swap_promise_manager_;

  // |current_layer_tree_frame_sink_| can't be updated until we've successfully
  // initialized a new LayerTreeFrameSink. |new_layer_tree_frame_sink_|
  // contains the new LayerTreeFrameSink that is currently being initialized.
  // If initialization is successful then |new_layer_tree_frame_sink_| replaces
  // |current_layer_tree_frame_sink_|.
  std::unique_ptr<LayerTreeFrameSink> new_layer_tree_frame_sink_;
  std::unique_ptr<LayerTreeFrameSink> current_layer_tree_frame_sink_;

  const LayerTreeSettings settings_;

  bool visible_ = false;

  // If set, then page scale animation has completed, but the client hasn't been
  // notified about it yet.
  bool did_complete_scale_animation_ = false;

  const int id_;
  // Track when we're inside a main frame to see if compositor is being
  // destroyed midway which causes a crash. crbug.com/654672
  bool inside_main_frame_ = false;

  // State cached until impl side is initialized.
  TaskGraphRunner* task_graph_runner_;
  gfx::RenderingPipeline* main_thread_pipeline_;
  gfx::RenderingPipeline* compositor_thread_pipeline_;

  float recording_scale_factor_ = 1.f;
  // Used to track the out-bound state for ApplyViewportChanges.
  bool is_pinch_gesture_active_from_impl_ = false;

  uint32_t defer_main_frame_update_count_ = 0;

  gfx::Rect visual_device_viewport_intersection_rect_;

  PropertyTrees property_trees_;

  scoped_refptr<HeadsUpDisplayLayer> hud_layer_;

  // Layer id to Layer map.
  std::unordered_map<int, Layer*> layer_id_map_;

  // This is for layer tree mode only.
  std::unordered_map<ElementId, Layer*, ElementIdHash> element_layers_map_;

  bool in_paint_layer_contents_ = false;

  // This is true if atleast one layer in the layer tree has a copy request. We
  // use this bool to decide whether we need to compute subtree has copy request
  // for every layer during property tree building.
  bool has_copy_request_ = false;

  MutatorHost* mutator_host_;

  RasterDarkModeFilter* dark_mode_filter_;

  std::unordered_map<int, base::OnceCallback<void(bool)>>
      pending_image_decodes_;

  struct ScrollAnimationState {
    ScrollAnimationState();
    ~ScrollAnimationState();

    // Tracks whether there is an ongoing compositor-driven scroll animation.
    bool in_progress = false;

    // Callback to run when the scroll-animation ends.
    base::OnceClosure end_notification;
  } scroll_animation_;

  std::unique_ptr<CompletionEvent> commit_completion_event_;

  EventsMetricsManager events_metrics_manager_;

  // A list of callbacks that need to be invoked when they are processed.
  base::flat_map<uint32_t, base::OnceClosure> document_transition_callbacks_;

  // Used to vend weak pointers to LayerTreeHost to ScopedDeferMainFrameUpdate
  // objects.
  base::WeakPtrFactory<LayerTreeHost> defer_main_frame_update_weak_ptr_factory_{
      this};
};

}  // namespace cc

#endif  // CC_TREES_LAYER_TREE_HOST_H_
