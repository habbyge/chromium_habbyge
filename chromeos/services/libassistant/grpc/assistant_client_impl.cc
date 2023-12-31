// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/libassistant/grpc/assistant_client_impl.h"

#include <memory>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/check.h"
#include "base/containers/flat_set.h"
#include "base/notreached.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "chromeos/assistant/internal/grpc_transport/request_utils.h"
#include "chromeos/assistant/internal/internal_constants.h"
#include "chromeos/assistant/internal/internal_util.h"
#include "chromeos/assistant/internal/proto/shared/proto/v2/bootup_settings_interface.pb.h"
#include "chromeos/assistant/internal/proto/shared/proto/v2/config_settings_interface.pb.h"
#include "chromeos/assistant/internal/proto/shared/proto/v2/display_interface.pb.h"
#include "chromeos/assistant/internal/proto/shared/proto/v2/query_interface.pb.h"
#include "chromeos/services/assistant/public/cpp/features.h"
#include "chromeos/services/libassistant/callback_utils.h"
#include "chromeos/services/libassistant/grpc/assistant_client_v1.h"
#include "chromeos/services/libassistant/grpc/external_services/action_service.h"
#include "chromeos/services/libassistant/grpc/grpc_libassistant_client.h"
#include "chromeos/services/libassistant/grpc/services_status_observer.h"
#include "libassistant/shared/public/assistant_manager.h"

namespace chromeos {
namespace libassistant {

namespace {

// Rpc call config constants.
constexpr int kMaxRpcRetries = 5;
constexpr int kAssistantInteractionDefaultTimeoutMs = 20000;
const chromeos::libassistant::StateConfig kDefaultStateConfig =
    chromeos::libassistant::StateConfig{kMaxRpcRetries,
                                        kAssistantInteractionDefaultTimeoutMs};

// Creates a callback for logging the request status. The callback will
// ignore the returned response as it either doesn't contain any information
// we need or is empty.
template <typename Response>
base::OnceCallback<void(const grpc::Status& status, const Response&)>
GetLoggingCallback(const std::string& request_name) {
  return base::BindOnce(
      [](const std::string& request_name, const grpc::Status& status,
         const Response& ignored) {
        if (status.ok()) {
          DVLOG(2) << request_name << " succeed with ok status.";
        } else {
          LOG(ERROR) << request_name << " failed with a non-ok status.";
          LOG(ERROR) << "Error code: " << status.error_code()
                     << ", error message: " << status.error_message();
        }
      },
      request_name);
}

}  // namespace

AssistantClientImpl::AssistantClientImpl(
    std::unique_ptr<assistant_client::AssistantManager> assistant_manager,
    assistant_client::AssistantManagerInternal* assistant_manager_internal,
    const std::string& libassistant_service_address,
    const std::string& assistant_service_address)
    : AssistantClientV1(std::move(assistant_manager),
                        assistant_manager_internal),
      grpc_services_(libassistant_service_address, assistant_service_address),
      libassistant_client_(grpc_services_.GrpcLibassistantClient()) {}

AssistantClientImpl::~AssistantClientImpl() = default;

void AssistantClientImpl::StartServices(
    ServicesStatusObserver* services_status_observer) {
  grpc_services_.GetServicesStatusProvider().AddObserver(
      services_status_observer);

  StartGrpcServices();

  AssistantClientV1::StartServices(services_status_observer);
}

bool AssistantClientImpl::StartGrpcServices() {
  return grpc_services_.Start();
}

void AssistantClientImpl::ResetAllDataAndShutdown() {
  libassistant_client_.CallServiceMethod(
      ::assistant::api::ResetAllDataAndShutdownRequest(),
      GetLoggingCallback<::assistant::api::ResetAllDataAndShutdownResponse>(
          /*request_name=*/__func__),
      kDefaultStateConfig);
}

void AssistantClientImpl::SendDisplayRequest(
    const OnDisplayRequestRequest& request) {
  libassistant_client_.CallServiceMethod(
      request,
      GetLoggingCallback<::assistant::api::OnDisplayRequestResponse>(
          /*request_name=*/__func__),
      kDefaultStateConfig);
}

void AssistantClientImpl::AddDisplayEventObserver(
    GrpcServicesObserver<OnAssistantDisplayEventRequest>* observer) {
  grpc_services_.AddAssistantDisplayEventObserver(observer);
}

void AssistantClientImpl::AddDeviceStateEventObserver(
    GrpcServicesObserver<OnDeviceStateEventRequest>* observer) {
  grpc_services_.AddDeviceStateEventObserver(observer);
}

void AssistantClientImpl::SendVoicelessInteraction(
    const ::assistant::api::Interaction& interaction,
    const std::string& description,
    const ::assistant::api::VoicelessOptions& options,
    base::OnceCallback<void(bool)> on_done) {
  ::assistant::api::SendQueryRequest request;
  PopulateSendQueryRequest(interaction, description, options, &request);

  libassistant_client_.CallServiceMethod(
      request,
      base::BindOnce(
          [](base::OnceCallback<void(bool)> on_done, const grpc::Status& status,
             const ::assistant::api::SendQueryResponse& response) {
            std::move(on_done).Run(response.success());
          },
          std::move(on_done)),
      kDefaultStateConfig);
}

void AssistantClientImpl::RegisterActionModule(
    assistant_client::ActionModule* action_module) {
  grpc_services_.GetActionService()->RegisterActionModule(action_module);
}

void AssistantClientImpl::SetAuthenticationInfo(const AuthTokens& tokens) {
  ::assistant::api::SetAuthInfoRequest request;
  // Each token exists of a [gaia_id, auth_token] tuple.
  for (const auto& token : tokens) {
    auto* proto = request.add_tokens();
    proto->set_user_id(token.first);
    proto->set_auth_token(token.second);
  }

  libassistant_client_.CallServiceMethod(
      request,
      GetLoggingCallback<::assistant::api::SetAuthInfoResponse>(
          /*request_name=*/__func__),
      kDefaultStateConfig);
}

void AssistantClientImpl::SetInternalOptions(const std::string& locale,
                                             bool spoken_feedback_enabled) {
  auto internal_options = chromeos::assistant::CreateInternalOptionsProto(
      locale, spoken_feedback_enabled);

  ::assistant::api::SetInternalOptionsRequest request;
  *request.mutable_internal_options() = std::move(internal_options);

  libassistant_client_.CallServiceMethod(
      request,
      GetLoggingCallback<::assistant::api::SetInternalOptionsResponse>(
          /*request_name=*/__func__),
      kDefaultStateConfig);
}

// static
std::unique_ptr<AssistantClient> AssistantClient::Create(
    std::unique_ptr<assistant_client::AssistantManager> assistant_manager,
    assistant_client::AssistantManagerInternal* assistant_manager_internal) {
  if (chromeos::assistant::features::IsLibAssistantV2Enabled()) {
    // Note that we should *not* depend on |assistant_manager_internal| for V2,
    // so |assistant_manager_internal| will be nullptr after the migration has
    // done.
    return std::make_unique<AssistantClientImpl>(
        std::move(assistant_manager), assistant_manager_internal,
        assistant::kLibassistantServiceAddress,
        assistant::kAssistantServiceAddress);
  }

  return std::make_unique<AssistantClientV1>(std::move(assistant_manager),
                                             assistant_manager_internal);
}

}  // namespace libassistant
}  // namespace chromeos
