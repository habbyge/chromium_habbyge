// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ARC_MIDIS_ARC_MIDIS_BRIDGE_H_
#define COMPONENTS_ARC_MIDIS_ARC_MIDIS_BRIDGE_H_

#include <stdint.h>

#include "components/arc/mojom/midis.mojom.h"
#include "components/keyed_service/core/keyed_service.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace arc {

class ArcBridgeService;

class ArcMidisBridge : public KeyedService,
                       public mojom::MidisHost {
 public:
  // Returns singleton instance for the given BrowserContext,
  // or nullptr if the browser |context| is not allowed to use ARC.
  static ArcMidisBridge* GetForBrowserContext(content::BrowserContext* context);
  static ArcMidisBridge* GetForBrowserContextForTesting(
      content::BrowserContext* context);

  ArcMidisBridge(content::BrowserContext* context,
                 ArcBridgeService* bridge_service);

  ArcMidisBridge(const ArcMidisBridge&) = delete;
  ArcMidisBridge& operator=(const ArcMidisBridge&) = delete;

  ~ArcMidisBridge() override;

  // Midis Mojo host interface
  void Connect(mojo::PendingReceiver<mojom::MidisServer> receiver,
               mojo::PendingRemote<mojom::MidisClient> client_remote) override;

 private:
  void OnBootstrapMojoConnection(
      mojo::PendingReceiver<mojom::MidisServer> receiver,
      mojo::PendingRemote<mojom::MidisClient> client_remote,
      bool result);
  void OnMojoConnectionError();

  ArcBridgeService* const arc_bridge_service_;  // Owned by ArcServiceManager.
  mojo::Remote<mojom::MidisHost> midis_host_remote_;

  // WeakPtrFactory to use for callbacks.
  base::WeakPtrFactory<ArcMidisBridge> weak_factory_{this};
};

}  // namespace arc

#endif  // COMPONENTS_ARC_MIDIS_ARC_MIDIS_BRIDGE_H_
