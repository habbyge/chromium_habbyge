// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/test/policy_container_utils.h"

#include "base/macros.h"
#include "third_party/blink/public/mojom/frame/policy_container.mojom.h"

namespace content {

blink::mojom::PolicyContainerPtr CreateStubPolicyContainer() {
  mojo::PendingAssociatedRemote<blink::mojom::PolicyContainerHost>
      stub_policy_container_remote;
  ignore_result(
      stub_policy_container_remote.InitWithNewEndpointAndPassReceiver());
  return blink::mojom::PolicyContainer::New(
      blink::mojom::PolicyContainerPolicies::New(),
      std::move(stub_policy_container_remote));
}

}  // namespace content
