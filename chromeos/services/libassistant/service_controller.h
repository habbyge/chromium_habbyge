// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_LIBASSISTANT_SERVICE_CONTROLLER_H_
#define CHROMEOS_SERVICES_LIBASSISTANT_SERVICE_CONTROLLER_H_

#include "base/component_export.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/scoped_observation.h"
#include "chromeos/services/libassistant/grpc/assistant_client.h"
#include "chromeos/services/libassistant/grpc/assistant_client_observer.h"
#include "chromeos/services/libassistant/grpc/services_status_observer.h"
#include "chromeos/services/libassistant/public/mojom/service.mojom.h"
#include "chromeos/services/libassistant/public/mojom/service_controller.mojom.h"
#include "chromeos/services/libassistant/public/mojom/settings_controller.mojom-forward.h"
#include "libassistant/shared/public/assistant_manager.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote_set.h"

namespace assistant_client {
class AssistantManager;
class AssistantManagerInternal;
}  // namespace assistant_client

namespace chromeos {
namespace libassistant {

class ChromiumApiDelegate;
class LibassistantFactory;

// Component managing the lifecycle of Libassistant,
// exposing methods to start/stop and configure Libassistant.
class COMPONENT_EXPORT(LIBASSISTANT_SERVICE) ServiceController
    : public mojom::ServiceController,
      public ServicesStatusObserver {
 public:
  explicit ServiceController(LibassistantFactory* factory);
  ServiceController(ServiceController&) = delete;
  ServiceController& operator=(ServiceController&) = delete;
  ~ServiceController() override;

  void Bind(mojo::PendingReceiver<mojom::ServiceController> receiver,
            mojom::SettingsController* settings_controller);

  // mojom::ServiceController implementation:
  void Initialize(mojom::BootupConfigPtr libassistant_config,
                  mojo::PendingRemote<network::mojom::URLLoaderFactory>
                      url_loader_factory) override;
  void Start() override;
  void Stop() override;
  void ResetAllDataAndStop() override;
  void AddAndFireStateObserver(
      mojo::PendingRemote<mojom::StateObserver> observer) override;

  // ServicesStatusObserver implementation:
  void OnServicesStatusChanged(ServicesStatus status) override;

  void AddAndFireAssistantClientObserver(AssistantClientObserver* observer);
  void RemoveAssistantClientObserver(AssistantClientObserver* observer);
  void RemoveAllAssistantClientObservers();

  bool IsInitialized() const;
  // Note this is true even when the service is running (as it is still started
  // at that point).
  bool IsStarted() const;
  bool IsRunning() const;

  // Will return nullptr if the service is stopped.
  AssistantClient* assistant_client();
  // Will return nullptr if the service is stopped.
  assistant_client::AssistantManager* assistant_manager();
  // Will return nullptr if the service is stopped.
  assistant_client::AssistantManagerInternal* assistant_manager_internal();

 private:
  // Will be invoked when all Libassistant services are ready to query.
  void OnAllServicesReady();
  // Will be invoked when Libassistant services are started.
  void OnServicesBootingUp();

  void SetStateAndInformObservers(mojom::ServiceState new_state);

  void CreateAndRegisterChromiumApiDelegate(
      mojo::PendingRemote<network::mojom::URLLoaderFactory> url_loader_factory);
  void CreateChromiumApiDelegate(
      mojo::PendingRemote<network::mojom::URLLoaderFactory> url_loader_factory);

  mojom::ServiceState state_ = mojom::ServiceState::kStopped;

  // Called during |Initialize| to apply boot configuration.
  mojom::SettingsController* settings_controller_ = nullptr;

  LibassistantFactory& libassistant_factory_;

  std::unique_ptr<AssistantClient> assistant_client_;
  std::unique_ptr<ChromiumApiDelegate> chromium_api_delegate_;

  mojo::Receiver<mojom::ServiceController> receiver_{this};
  mojo::RemoteSet<mojom::StateObserver> state_observers_;
  base::ObserverList<AssistantClientObserver> assistant_client_observers_;

  base::WeakPtrFactory<ServiceController> weak_factory_{this};
};

using ScopedAssistantClientObserver = base::ScopedObservation<
    ServiceController,
    AssistantClientObserver,
    &ServiceController::AddAndFireAssistantClientObserver,
    &ServiceController::RemoveAssistantClientObserver>;

}  // namespace libassistant
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_LIBASSISTANT_SERVICE_CONTROLLER_H_
