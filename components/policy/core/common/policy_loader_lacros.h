// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_POLICY_CORE_COMMON_POLICY_LOADER_LACROS_H_
#define COMPONENTS_POLICY_CORE_COMMON_POLICY_LOADER_LACROS_H_

#include <stdint.h>

#include <vector>

#include "base/memory/scoped_refptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "chromeos/lacros/lacros_service.h"
#include "components/policy/core/common/async_policy_loader.h"
#include "components/policy/core/common/policy_proto_decoders.h"
#include "components/policy/proto/device_management_backend.pb.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace policy {

// A policy loader for Lacros. The data is taken from Ash and the validitity of
// data is trusted, since they have been validated by Ash.
class POLICY_EXPORT PolicyLoaderLacros
    : public AsyncPolicyLoader,
      public chromeos::LacrosService::Observer {
 public:
  // Creates the policy loader, saving the task_runner internally. Later
  // task_runner is used to have in sequence the process of policy parsing and
  // validation. The |per_profile| parameter specifies which policy should be
  // installed.
  explicit PolicyLoaderLacros(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      PolicyPerProfileFilter per_profile);
  // Not copyable or movable
  PolicyLoaderLacros(const PolicyLoaderLacros&) = delete;
  PolicyLoaderLacros& operator=(const PolicyLoaderLacros&) = delete;
  ~PolicyLoaderLacros() override;

  // AsyncPolicyLoader implementation.
  // Verifies that it runs on correct thread. Detaches from the sequence checker
  // which allows all other methods to check that they are executed on the same
  // sequence. |sequence_checker_| is used for that.
  void InitOnBackgroundThread() override;
  // Loads the policy data from LacrosInitParams and populates it in the bundle
  // that is returned.
  std::unique_ptr<PolicyBundle> Load() override;

  // LacrosChromeServiceDelegateImpl::Observer implementation.
  // Update and reload the policy with new data.
  void OnPolicyUpdated(
      const std::vector<uint8_t>& policy_fetch_response) override;

  // Returns if the main user is managed or not.
  // TODO(crbug/1245077): Remove once Lacros handles all profiles the same way.
  static bool IsMainUserManaged();

  // Return if the main user is affiliated or not.
  static bool IsMainUserAffiliated();

  // Returns the policy data corresponding to the main user to be used by
  // Enterprise Connector policies.
  // TODO(crbug/1245077): Remove once Lacros handles all profiles the same way.
  static const enterprise_management::PolicyData* main_user_policy_data();
  static void set_main_user_policy_data_for_testing(
      const enterprise_management::PolicyData& policy_data);

 private:
  // Task runner for running background jobs.
  const scoped_refptr<base::SequencedTaskRunner> task_runner_;

  // The filter for policy data to install.
  const PolicyPerProfileFilter per_profile_;

  // Serialized blob of PolicyFetchResponse object received from the server.
  absl::optional<std::vector<uint8_t>> policy_fetch_response_;

  // Checks that the method is called on the right sequence.
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace policy

#endif  // COMPONENTS_POLICY_CORE_COMMON_POLICY_LOADER_LACROS_H_
