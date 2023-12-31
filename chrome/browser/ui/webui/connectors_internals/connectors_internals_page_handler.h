// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_CONNECTORS_INTERNALS_CONNECTORS_INTERNALS_PAGE_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_CONNECTORS_INTERNALS_CONNECTORS_INTERNALS_PAGE_HANDLER_H_

#include "base/memory/weak_ptr.h"
#include "chrome/browser/enterprise/connectors/device_trust/attestation/common/signals_type.h"
#include "chrome/browser/ui/webui/connectors_internals/connectors_internals.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"

class Profile;

namespace enterprise_connectors {

// Concrete implementation of connectors_internals::mojom::PageHandler.
class ConnectorsInternalsPageHandler
    : public connectors_internals::mojom::PageHandler {
 public:
  ConnectorsInternalsPageHandler(
      mojo::PendingReceiver<connectors_internals::mojom::PageHandler> receiver,
      Profile* profile);

  ConnectorsInternalsPageHandler(const ConnectorsInternalsPageHandler&) =
      delete;
  ConnectorsInternalsPageHandler& operator=(
      const ConnectorsInternalsPageHandler&) = delete;

  ~ConnectorsInternalsPageHandler() override;

 private:
  // connectors_internals::mojom::ConnectorsInternalsPageHandler
  void GetZeroTrustState(GetZeroTrustStateCallback callback) override;

  void OnSignalsCollected(GetZeroTrustStateCallback callback,
                          bool is_device_trust_enabled,
                          std::unique_ptr<SignalsType> signals);

  mojo::Receiver<connectors_internals::mojom::PageHandler> receiver_;
  Profile* profile_;

  base::WeakPtrFactory<ConnectorsInternalsPageHandler> weak_ptr_factory_{this};
};

}  // namespace enterprise_connectors

#endif  // CHROME_BROWSER_UI_WEBUI_CONNECTORS_INTERNALS_CONNECTORS_INTERNALS_PAGE_HANDLER_H_
