// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_COMPONENTS_TETHER_ASYNCHRONOUS_SHUTDOWN_OBJECT_CONTAINER_H_
#define CHROMEOS_COMPONENTS_TETHER_ASYNCHRONOUS_SHUTDOWN_OBJECT_CONTAINER_H_

namespace chromeos {

namespace tether {

class TetherHostFetcher;
class DisconnectTetheringRequestSender;
class NetworkConfigurationRemover;
class WifiHotspotDisconnector;

// Container for objects owned by the Tether component which have an
// asynchronous shutdown flow (i.e., they cannot be deleted until they complete
// asynchronous operations). Objects which can be shut down synchronously are
// owned by SynchronousShutdownObjectContainer.
class AsynchronousShutdownObjectContainer {
 public:
  AsynchronousShutdownObjectContainer() {}

  AsynchronousShutdownObjectContainer(
      const AsynchronousShutdownObjectContainer&) = delete;
  AsynchronousShutdownObjectContainer& operator=(
      const AsynchronousShutdownObjectContainer&) = delete;

  virtual ~AsynchronousShutdownObjectContainer() {}

  // Shuts down the objects contained by this class and invokes
  // |shutdown_complete_callback| upon completion. This function should only be
  // called once.
  virtual void Shutdown(base::OnceClosure shutdown_complete_callback) = 0;

  virtual TetherHostFetcher* tether_host_fetcher() = 0;
  virtual DisconnectTetheringRequestSender*
  disconnect_tethering_request_sender() = 0;
  virtual NetworkConfigurationRemover* network_configuration_remover() = 0;
  virtual WifiHotspotDisconnector* wifi_hotspot_disconnector() = 0;
};

}  // namespace tether

}  // namespace chromeos

#endif  // CHROMEOS_COMPONENTS_TETHER_ASYNCHRONOUS_SHUTDOWN_OBJECT_CONTAINER_H_
