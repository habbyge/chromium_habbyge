// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TREES_TREE_SYNCHRONIZER_H_
#define CC_TREES_TREE_SYNCHRONIZER_H_

#include "cc/cc_export.h"
#include "cc/trees/layer_tree_host.h"

namespace cc {

class LayerImpl;
class LayerTreeImpl;
class Layer;

class CC_EXPORT TreeSynchronizer {
 public:
  // Not instantiable.
  TreeSynchronizer() = delete;

  // Accepts a Layer tree and returns a reference to a LayerImpl tree that
  // duplicates the structure of the Layer tree, reusing the LayerImpls in the
  // tree provided by old_layer_impl_root if possible.
  static void SynchronizeTrees(const CommitState*, LayerTreeImpl* tree_impl);

  static void SynchronizeTrees(LayerTreeImpl* pending_tree,
                               LayerTreeImpl* active_tree);

  static void PushLayerProperties(CommitState*, LayerTreeImpl* impl_tree);

  static void PushLayerProperties(LayerTreeImpl* pending_tree,
                                  LayerTreeImpl* active_tree);
};

}  // namespace cc

#endif  // CC_TREES_TREE_SYNCHRONIZER_H_
