// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/fake_layer_tree_host.h"

#include <memory>
#include <utility>

#include "base/memory/ptr_util.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "cc/animation/animation_host.h"
#include "cc/layers/layer.h"
#include "cc/test/test_task_graph_runner.h"
#include "cc/trees/mutator_host.h"

namespace cc {

FakeLayerTreeHost::FakeLayerTreeHost(FakeLayerTreeHostClient* client,
                                     LayerTreeHost::InitParams params,
                                     CompositorMode mode)
    : LayerTreeHost(std::move(params), mode),
      client_(client),
      needs_commit_(false) {
  scoped_refptr<base::SingleThreadTaskRunner> impl_task_runner =
      mode == CompositorMode::THREADED ? base::ThreadTaskRunnerHandle::Get()
                                       : nullptr;
  SetTaskRunnerProviderForTesting(TaskRunnerProvider::Create(
      base::ThreadTaskRunnerHandle::Get(), impl_task_runner));
  client_->SetLayerTreeHost(this);
}

std::unique_ptr<FakeLayerTreeHost> FakeLayerTreeHost::Create(
    FakeLayerTreeHostClient* client,
    TestTaskGraphRunner* task_graph_runner,
    MutatorHost* mutator_host) {
  LayerTreeSettings settings;
  return Create(client, task_graph_runner, mutator_host, settings);
}

std::unique_ptr<FakeLayerTreeHost> FakeLayerTreeHost::Create(
    FakeLayerTreeHostClient* client,
    TestTaskGraphRunner* task_graph_runner,
    MutatorHost* mutator_host,
    const LayerTreeSettings& settings) {
  return Create(client, task_graph_runner, mutator_host, settings,
                CompositorMode::SINGLE_THREADED);
}

std::unique_ptr<FakeLayerTreeHost> FakeLayerTreeHost::Create(
    FakeLayerTreeHostClient* client,
    TestTaskGraphRunner* task_graph_runner,
    MutatorHost* mutator_host,
    const LayerTreeSettings& settings,
    CompositorMode mode) {
  LayerTreeHost::InitParams params;
  params.client = client;
  params.settings = &settings;
  params.task_graph_runner = task_graph_runner;
  params.mutator_host = mutator_host;
  return base::WrapUnique(
      new FakeLayerTreeHost(client, std::move(params), mode));
}

FakeLayerTreeHost::~FakeLayerTreeHost() {
  client_->SetLayerTreeHost(nullptr);
}

void FakeLayerTreeHost::SetNeedsCommit() { needs_commit_ = true; }

std::unique_ptr<LayerTreeHostImpl> FakeLayerTreeHost::CreateLayerTreeHostImpl(
    LayerTreeHostImplClient* client) {
  DCHECK(!host_impl_);
  auto host_impl = std::make_unique<FakeLayerTreeHostImpl>(
      GetSettings(), &task_runner_provider(), task_graph_runner());
  host_impl_ = host_impl.get();
  return host_impl;
}

void FakeLayerTreeHost::CreateFakeLayerTreeHostImpl() {
  DCHECK(!host_impl_);
  owned_host_impl_ = std::make_unique<FakeLayerTreeHostImpl>(
      GetSettings(), &task_runner_provider(), task_graph_runner());
  host_impl_ = owned_host_impl_.get();
}

LayerImpl* FakeLayerTreeHost::CommitAndCreateLayerImplTree() {
  // TODO(pdr): Update the LayerTreeImpl lifecycle states here so lifecycle
  // violations can be caught.
  // When doing a full commit, we would call
  // layer_tree_host_->ActivateCommitState() and the second argument would come
  // from layer_tree_host_->active_commit_state(); we use pending_commit_state()
  // just to keep the test code simple.
  host_impl_->BeginCommit(pending_commit_state()->source_frame_number);
  TreeSynchronizer::SynchronizeTrees(pending_commit_state(), active_tree());
  active_tree()->SetPropertyTrees(property_trees());
  TreeSynchronizer::PushLayerProperties(pending_commit_state(), active_tree());
  mutator_host()->PushPropertiesTo(host_impl_->mutator_host());

  active_tree()->property_trees()->scroll_tree.PushScrollUpdatesFromMainThread(
      property_trees(), active_tree(),
      GetSettings().commit_fractional_scroll_deltas);

  return active_tree()->root_layer();
}

LayerImpl* FakeLayerTreeHost::CommitAndCreatePendingTree() {
  // pending_commit_state() is used here because this is a phony commit that
  // doesn't actually call WillCommit() or ActivateCommitState().
  pending_tree()->set_source_frame_number(SourceFrameNumber());
  TreeSynchronizer::SynchronizeTrees(pending_commit_state(), pending_tree());
  pending_tree()->SetPropertyTrees(property_trees());
  TreeSynchronizer::PushLayerProperties(pending_commit_state(), pending_tree());
  mutator_host()->PushPropertiesTo(host_impl_->mutator_host());

  pending_tree()->property_trees()->scroll_tree.PushScrollUpdatesFromMainThread(
      property_trees(), pending_tree(),
      GetSettings().commit_fractional_scroll_deltas);
  return pending_tree()->root_layer();
}

}  // namespace cc
