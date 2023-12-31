// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ARC_SESSION_ARC_CLIENT_ADAPTER_H_
#define COMPONENTS_ARC_SESSION_ARC_CLIENT_ADAPTER_H_

#include <memory>
#include <string>

#include "base/callback_forward.h"
#include "base/files/file_path.h"
#include "base/observer_list.h"
#include "chromeos/dbus/dbus_method_call_status.h"
#include "components/arc/session/arc_start_params.h"
#include "components/arc/session/arc_upgrade_params.h"

namespace cryptohome {
class Identification;
}  // namespace cryptohome

namespace arc {

// An adapter to talk to a Chrome OS daemon to manage lifetime of ARC instance.
class ArcClientAdapter {
 public:
  class Observer {
   public:
    virtual ~Observer() = default;
    virtual void ArcInstanceStopped(bool is_system_shutdown) = 0;
  };

  // DemoModeDelegate contains functions used to load the demo session apps for
  // ARC. The adapter cannot do this directly because ash::DemoSession classes
  // are in //chrome.
  class DemoModeDelegate {
   public:
    virtual ~DemoModeDelegate() = default;

    // Ensures that the demo session offline resources are loaded, if demo mode
    // is enabled. This must be called before GetDemoAppsPath().
    virtual void EnsureOfflineResourcesLoaded(base::OnceClosure callback) = 0;

    // Gets the path of the image containing demo session Android apps. Returns
    // an empty path if demo mode is not enabled.
    virtual base::FilePath GetDemoAppsPath() = 0;
  };

  // Creates a default instance of ArcClientAdapter.
  static std::unique_ptr<ArcClientAdapter> Create();

  ArcClientAdapter(const ArcClientAdapter&) = delete;
  ArcClientAdapter& operator=(const ArcClientAdapter&) = delete;

  virtual ~ArcClientAdapter();

  // StartMiniArc starts ARC with only a handful of ARC processes for Chrome OS
  // login screen.
  virtual void StartMiniArc(StartParams params,
                            chromeos::VoidDBusMethodCallback callback) = 0;

  // UpgradeArc upgrades a mini ARC instance to a full ARC instance.
  virtual void UpgradeArc(UpgradeParams params,
                          chromeos::VoidDBusMethodCallback callback) = 0;

  // Asynchronously stops the ARC instance. |on_shutdown| is true if the method
  // is called due to the browser being shut down. Also backs up the ARC
  // bug report if |should_backup_log| is set to true.
  virtual void StopArcInstance(bool on_shutdown, bool should_backup_log) = 0;

  // Sets a hash string of the profile user IDs and an ARC serial number for the
  // user.
  virtual void SetUserInfo(const cryptohome::Identification& cryptohome_id,
                           const std::string& hash,
                           const std::string& serial_number) = 0;

  // Provides the DemoModeDelegate which will be used to load the demo session
  // apps path.
  virtual void SetDemoModeDelegate(DemoModeDelegate* delegate) = 0;

  // Trims VM's memory by moving it to zram. |callback| is called when the
  // operation is done.
  using TrimVmMemoryCallback =
      base::OnceCallback<void(bool success, const std::string& failure_reason)>;
  virtual void TrimVmMemory(TrimVmMemoryCallback callback) = 0;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 protected:
  ArcClientAdapter();

  base::ObserverList<Observer>::Unchecked observer_list_;
};

}  // namespace arc

#endif  // COMPONENTS_ARC_SESSION_ARC_CLIENT_ADAPTER_H_
