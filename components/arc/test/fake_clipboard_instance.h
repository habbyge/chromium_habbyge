// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ARC_TEST_FAKE_CLIPBOARD_INSTANCE_H_
#define COMPONENTS_ARC_TEST_FAKE_CLIPBOARD_INSTANCE_H_

#include "components/arc/mojom/clipboard.mojom.h"
#include "mojo/public/cpp/bindings/pending_remote.h"

namespace arc {

class FakeClipboardInstance : public mojom::ClipboardInstance {
 public:
  FakeClipboardInstance();

  FakeClipboardInstance(const FakeClipboardInstance&) = delete;
  FakeClipboardInstance& operator=(const FakeClipboardInstance&) = delete;

  ~FakeClipboardInstance() override;

  int num_host_clipboard_updated() const { return num_host_clipboard_updated_; }

  // mojom::ClipboardInstance overrides:
  void Init(mojo::PendingRemote<mojom::ClipboardHost> host_remote,
            InitCallback callback) override;
  void InitDeprecated(
      mojo::PendingRemote<mojom::ClipboardHost> host_remote) override;
  void OnHostClipboardUpdated() override;

 private:
  int num_host_clipboard_updated_ = 0;
};

}  // namespace arc

#endif  // COMPONENTS_ARC_TEST_FAKE_CLIPBOARD_INSTANCE_H_
