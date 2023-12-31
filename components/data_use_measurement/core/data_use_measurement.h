// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_DATA_USE_MEASUREMENT_CORE_DATA_USE_MEASUREMENT_H_
#define COMPONENTS_DATA_USE_MEASUREMENT_CORE_DATA_USE_MEASUREMENT_H_

#include <stdint.h>

#include <memory>
#include <string>

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/sequence_checker.h"
#include "build/build_config.h"
#include "components/data_use_measurement/core/data_use_tracker_prefs.h"
#include "components/data_use_measurement/core/data_use_user_data.h"
#include "services/network/public/cpp/network_connection_tracker.h"

#if defined(OS_ANDROID)
#include "base/android/application_status_listener.h"
#endif

class PrefService;

namespace data_use_measurement {

// Records the data use of user traffic and various services in UMA histograms.
// The UMA is broken down by network technology used (Wi-Fi vs cellular). On
// Android, the UMA is further broken down by whether the application was in the
// background or foreground during the request.
// TODO(amohammadkhan): Complete the layered architecture.
// http://crbug.com/527460
class DataUseMeasurement
    : public network::NetworkConnectionTracker::NetworkConnectionObserver {
 public:
  class ServicesDataUseObserver {
   public:
    // Called when services data use is reported.
    virtual void OnServicesDataUse(int32_t service_hash_code,
                                   int64_t recv_bytes,
                                   int64_t sent_bytes) = 0;
  };

  // Returns true if the NTA hash is initiated by user traffic.
  static bool IsUserRequest(int32_t network_traffic_annotation_hash_id);

  // Returns true if the NTA hash is one used by Chrome downloads.
  static bool IsUserDownloadsRequest(
      int32_t network_traffic_annotation_hash_id);

  // Returns true if the NTA hash is one used by metrics (UMA, UKM) component.
  static bool IsMetricsServiceRequest(
      int32_t network_traffic_annotation_hash_id);

  // |pref_service| can be used for accessing local state prefs. Can be null.
  // |network_connection_tracker| is guaranteed to be non-null.
  DataUseMeasurement(
      PrefService* pref_service,
      network::NetworkConnectionTracker* network_connection_tracker);

  DataUseMeasurement(const DataUseMeasurement&) = delete;
  DataUseMeasurement& operator=(const DataUseMeasurement&) = delete;

  ~DataUseMeasurement() override;

#if defined(OS_ANDROID)
  // This function should just be used for testing purposes. A change in
  // application state can be simulated by calling this function.
  void OnApplicationStateChangeForTesting(
      base::android::ApplicationState application_state);
#endif

  void AddServicesDataUseObserver(ServicesDataUseObserver* observer);
  void RemoveServicesDataUseObserver(ServicesDataUseObserver* observer);

  // Should be called to record downstream data used by user-initiated trafffic
  // requests. |is_tab_visible| is set to true if the traffic was initiated from
  // a tab that was visible in foreground when the data use was observed.
  void RecordDownstreamUserTrafficSizeMetric(bool is_tab_visible,
                                             int64_t bytes);

 protected:
  // Specifies that data is received or sent, respectively.
  enum TrafficDirection { DOWNSTREAM, UPSTREAM };

  // Returns the current application state (Foreground or Background). It always
  // returns Foreground if Chrome is not running on Android.
  DataUseUserData::AppState CurrentAppState() const;

  // Records data use histograms of services. It gets the size of exchanged
  // message, its direction (which is upstream or downstream) and reports to the
  // histogram DataUse.Services.{Dimensions} with, services as the buckets.
  // |app_state| indicates the app state which can be foreground, or background.
  void ReportDataUsageServices(int32_t traffic_annotation_hash,
                               TrafficDirection dir,
                               DataUseUserData::AppState app_state,
                               int64_t message_size_bytes);

  // Returns if the current network connection type is cellular.
  bool IsCurrentNetworkCellular() const;

  static void RegisterDataUseComponentLocalStatePrefs(
      PrefRegistrySimple* registry);

  base::ObserverList<ServicesDataUseObserver>::Unchecked
      services_data_use_observer_list_;

 private:
  friend class DataUseMeasurementTest;

  // Records the count of bytes received and sent by Chrome on the network as
  // reported by the operating system. If |force_record_metrics| is true, the
  // data use metrics are always recorded. If |force_record_metrics| is false,
  // data use may be recorded only if it's expected to be high.
  void MaybeRecordNetworkBytesOS(bool force_record_metrics);

  // Makes the full name of the histogram. It is made from |prefix| and suffix
  // which is made based on network and application status. suffix is a string
  // representing whether the data use was on the send ("Upstream") or receive
  // ("Downstream") path, and whether the app was in the "Foreground" or
  // "Background".
  static std::string GetHistogramNameWithConnectionType(
      const char* prefix,
      TrafficDirection dir,
      DataUseUserData::AppState app_state);

  // Makes the full name of the histogram. It is made from |prefix| and suffix
  // which is made based on network and application status. suffix is a string
  // representing whether the data use was on the send ("Upstream") or receive
  // ("Downstream") path, whether the app was in the "Foreground" or
  // "Background", and whether a "Cellular" or "WiFi" network was use. For
  // example, "Prefix.Upstream.Foreground.Cellular" is a possible output.
  // |app_state| indicates the app state which can be foreground, background, or
  // unknown.
  static std::string GetHistogramName(const char* prefix,
                                      TrafficDirection dir,
                                      DataUseUserData::AppState app_state,
                                      bool is_connection_cellular);

#if defined(OS_ANDROID)
  // Called whenever the application transitions from foreground to background
  // and vice versa.
  void OnApplicationStateChange(
      base::android::ApplicationState application_state);
#endif

  // Records data use histograms split on TrafficDirection, AppState and
  // TabState.
  void RecordTabStateHistogram(TrafficDirection dir,
                               DataUseUserData::AppState app_state,
                               bool is_tab_visible,
                               int64_t bytes) const;

  // NetworkConnectionObserver overrides
  void OnConnectionChanged(
      network::mojom::ConnectionType connection_type) override;

#if defined(OS_ANDROID)
  // Application listener store the last known state of the application in this
  // field.
  base::android::ApplicationState app_state_ =
      base::android::APPLICATION_STATE_HAS_RUNNING_ACTIVITIES;

  // ApplicationStatusListener used to monitor whether the application is in the
  // foreground or in the background. It is owned by DataUseMeasurement.
  std::unique_ptr<base::android::ApplicationStatusListener> app_listener_;
#endif

  // Number of bytes received and sent by Chromium as reported by the operating
  // system when it was last queried for traffic statistics. Set to 0 if the
  // operating system was never queried.
  int64_t rx_bytes_os_ = 0;
  int64_t tx_bytes_os_ = 0;

  // Watches for network connection changes. Global singleton object and
  // outlives |this|
  network::NetworkConnectionTracker* network_connection_tracker_;

  // The current connection type.
  network::mojom::ConnectionType connection_type_ =
      network::mojom::ConnectionType::CONNECTION_UNKNOWN;

  // Number of bytes received and sent by Chromium as reported by the network
  // delegate since the operating system was last queried for traffic
  // statistics.
  int64_t bytes_transferred_since_last_traffic_stats_query_ = 0;

  SEQUENCE_CHECKER(sequence_checker_);

  // Records the data usage in prefs.
  DataUseTrackerPrefs data_use_tracker_prefs_;

  base::WeakPtrFactory<DataUseMeasurement> weak_ptr_factory_{this};
};

}  // namespace data_use_measurement

#endif  // COMPONENTS_DATA_USE_MEASUREMENT_CORE_DATA_USE_MEASUREMENT_H_
