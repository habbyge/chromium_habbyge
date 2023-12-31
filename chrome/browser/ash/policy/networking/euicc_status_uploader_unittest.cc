// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/policy/networking/euicc_status_uploader.h"

#include "ash/constants/ash_pref_names.h"
#include "base/json/json_string_value_serializer.h"
#include "base/test/values_test_util.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chromeos/dbus/shill/shill_manager_client.h"
#include "chromeos/network/cellular_esim_profile_handler_impl.h"
#include "chromeos/network/managed_network_configuration_handler.h"
#include "chromeos/network/network_handler_test_helper.h"
#include "chromeos/network/network_ui_data.h"
#include "chromeos/network/onc/onc_utils.h"
#include "components/policy/core/common/cloud/mock_cloud_policy_client.h"
#include "components/prefs/testing_pref_service.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/shill/dbus-constants.h"

namespace policy {

namespace {

class FakeCloudPolicyClient : public testing::NiceMock<MockCloudPolicyClient> {
 public:
  void SetStatus(bool status) { status_ = status; }

  enterprise_management::UploadEuiccInfoRequest* GetLastRequest() {
    if (requests_.empty())
      return nullptr;
    return requests_.back().get();
  }

  int num_requests() const { return requests_.size(); }

 private:
  void UploadEuiccInfo(
      std::unique_ptr<enterprise_management::UploadEuiccInfoRequest> request,
      base::OnceCallback<void(bool)> callback) override {
    requests_.push_back(std::move(request));
    std::move(callback).Run(status_);
  }

  std::vector<std::unique_ptr<enterprise_management::UploadEuiccInfoRequest>>
      requests_;
  bool status_ = false;
};

bool RequestsAreEqual(
    const enterprise_management::UploadEuiccInfoRequest& lhs,
    const enterprise_management::UploadEuiccInfoRequest& rhs) {
  return lhs.euicc_count() == rhs.euicc_count() &&
         std::equal(
             std::begin(lhs.esim_profiles()), std::end(lhs.esim_profiles()),
             std::begin(rhs.esim_profiles()), [](const auto& u, const auto& v) {
               return std::tie(u.iccid(), u.smdp_address()) ==
                      std::tie(v.iccid(), v.smdp_address());
             });
}

const char kFakeObjectPath[] = "object-path";
const char kFakeEid[] = "12";

const char kEmptyEuiccStatus[] =
    R"(
{
  "clear_profile_list":false,"esim_profiles":[],"euicc_count":0
})";
const char kEuiccStatusWithOneProfile[] =
    R"({
        "clear_profile_list":false,
        "esim_profiles":
          [
            {"iccid":"iccid-1","smdp_address":"smdp-1"}
          ],
        "euicc_count":2
       })";
const char kEuiccStatusWithTwoProfiles[] =
    R"({
        "clear_profile_list":false,
        "esim_profiles":
          [
            {"iccid":"iccid-1","smdp_address":"smdp-1"},
            {"iccid":"iccid-2","smdp_address":"smdp-2"}
          ],
        "euicc_count":3
       })";

const char kDefaultProfilePath[] = "/profile/default";

const char kCellularDevicePath[] = "/service/cellular1";
const char kCellularDevicePath2[] = "/service/cellular2";

struct FakeESimProfile {
  std::string service_path;
  std::string guid;
  std::string iccid;
  std::string smdp_address;
  bool managed = true;
};
struct EuiccTestData {
  int euicc_count = 0;
  // multiple euicc ids.
  std::vector<FakeESimProfile> profiles;
};

const EuiccTestData kSetupOneEsimProfile = {
    2,
    {{kCellularDevicePath, "guid-1", "iccid-1", "smdp-1", true}}};
const EuiccTestData kSetupTwoEsimProfiles = {
    3,
    {
        {kCellularDevicePath, "guid-1", "iccid-1", "smdp-1", true},
        {kCellularDevicePath2, "guid-2", "iccid-2", "smdp-2", true},
    }};

}  // namespace

class EuiccStatusUploaderTest : public testing::Test {
 public:
  EuiccStatusUploaderTest() {}

  void SetUp() override {
    helper_ = std::make_unique<ash::NetworkHandlerTestHelper>();

    EuiccStatusUploader::RegisterLocalStatePrefs(local_state_.registry());
    chromeos::CellularESimProfileHandlerImpl::RegisterLocalStatePrefs(
        local_state_.registry());
    chromeos::NetworkHandler::Get()->InitializePrefServices(nullptr,
                                                            &local_state_);
  }

  std::unique_ptr<EuiccStatusUploader> CreateStatusUploader() {
    return std::make_unique<EuiccStatusUploader>(&cloud_policy_client_,
                                                 &local_state_);
  }

  void SetServerSuccessStatus(bool success) {
    cloud_policy_client_.SetStatus(success);
  }

  const base::Value* GetStoredPref() {
    return local_state_.Get(EuiccStatusUploader::kLastUploadedEuiccStatusPref);
  }

  std::string GetStoredPrefString() {
    const base::Value* last_uploaded_pref = GetStoredPref();
    std::string result;
    JSONStringValueSerializer sz(&result);
    sz.Serialize(*last_uploaded_pref);
    return result;
  }

  void UpdateUploader(EuiccStatusUploader* status_uploader) {
    (static_cast<chromeos::NetworkPolicyObserver*>(status_uploader))
        ->PoliciesApplied(/*userhash=*/std::string());
    status_uploader->FireRetryTimerIfExistsForTesting();
  }

  void SetUpDeviceProfiles(const EuiccTestData& data) {
    // Create |data.euicc_count| fake EUICCs.
    for (int euicc_id = 0; euicc_id < data.euicc_count; euicc_id++) {
      chromeos::HermesManagerClient::Get()->GetTestInterface()->AddEuicc(
          dbus::ObjectPath(kFakeObjectPath), kFakeEid, /*is_active=*/true,
          euicc_id);
    }

    ash::ShillServiceClient::TestInterface* shill_service_client =
        ash::ShillServiceClient::Get()->GetTestInterface();

    base::Value onc_config(base::Value::Type::LIST);

    for (const auto& test_profile : data.profiles) {
      shill_service_client->AddService(
          test_profile.service_path, test_profile.guid, /*name=*/"cellular",
          shill::kTypeCellular, "ready", /*visible=*/true);
      shill_service_client->SetServiceProperty(test_profile.service_path,
                                               shill::kIccidProperty,
                                               base::Value(test_profile.iccid));
      shill_service_client->SetServiceProperty(
          test_profile.service_path, shill::kProfileProperty,
          base::Value(kDefaultProfilePath));
      shill_service_client->SetServiceProperty(
          test_profile.service_path, shill::kUIDataProperty,
          base::Value(chromeos::NetworkUIData::CreateFromONC(
                          ::onc::ONCSource::ONC_SOURCE_DEVICE_POLICY)
                          ->GetAsJson()));

      base::Value single_onc = chromeos::onc::ReadDictionaryFromJson(
          R"({
              "GUID": ")" +
          test_profile.guid + R"(",
              "Name": "Cellular network",
              "Type": "Cellular",
              "Cellular": {
                "SMDPAddress" : ")" +
          test_profile.smdp_address + R"(",
              },
      })");
      onc_config.Append(std::move(single_onc));
    }

    // Set ONC values.
    chromeos::NetworkHandler::Get()
        ->managed_network_configuration_handler()
        ->SetPolicy(::onc::ONC_SOURCE_DEVICE_POLICY,
                    std::string() /* no username hash */, std::move(onc_config),
                    base::DictionaryValue());

    // Wait for Shill device and service change notifications to propagate.
    base::RunLoop().RunUntilIdle();
  }

  void ValidateUploadedStatus(const std::string& expected_status_str) {
    base::Value expected_status = base::test::ParseJson(expected_status_str);
    EXPECT_EQ(expected_status, *GetStoredPref());
    EXPECT_TRUE(cloud_policy_client_.GetLastRequest());
    EXPECT_TRUE(RequestsAreEqual(
        *EuiccStatusUploader::ConstructRequestFromStatus(expected_status),
        *cloud_policy_client_.GetLastRequest()));
  }

  void SetLastUploadedValue(const std::string& last_value) {
    local_state_.Set(EuiccStatusUploader::kLastUploadedEuiccStatusPref,
                     base::test::ParseJson(last_value));
  }

  int GetRequestCount() { return cloud_policy_client_.num_requests(); }

 private:
  content::BrowserTaskEnvironment task_environment_;
  FakeCloudPolicyClient cloud_policy_client_;
  TestingPrefServiceSimple local_state_;
  std::unique_ptr<ash::NetworkHandlerTestHelper> helper_;
};

TEST_F(EuiccStatusUploaderTest, EmptySetup) {
  auto status_uploader = CreateStatusUploader();
  // Initial upload request.
  EXPECT_EQ(GetRequestCount(), 1);
  // No value is uploaded yet.
  EXPECT_EQ("{}", GetStoredPrefString());

  // Make server accept requests.
  SetServerSuccessStatus(true);
  UpdateUploader(status_uploader.get());
  EXPECT_EQ(GetRequestCount(), 2);
  // Verify that last uploaded configuration is stored.
  ValidateUploadedStatus(kEmptyEuiccStatus);
}

TEST_F(EuiccStatusUploaderTest, ServerError) {
  auto status_uploader = CreateStatusUploader();
  // Initial upload request.
  EXPECT_EQ(GetRequestCount(), 1);
  // No value is uploaded yet.
  EXPECT_EQ("{}", GetStoredPrefString());

  UpdateUploader(status_uploader.get());
  EXPECT_EQ(GetRequestCount(), 2);
  // Nothing is stored when requests fail.
  EXPECT_EQ("{}", GetStoredPrefString());
}

TEST_F(EuiccStatusUploaderTest, Basic) {
  SetUpDeviceProfiles(kSetupOneEsimProfile);

  auto status_uploader = CreateStatusUploader();
  // Initial upload request.
  EXPECT_EQ(GetRequestCount(), 1);
  // No value is uploaded yet.
  EXPECT_EQ("{}", GetStoredPrefString());

  // Make server accept requests.
  SetServerSuccessStatus(true);
  UpdateUploader(status_uploader.get());
  EXPECT_EQ(GetRequestCount(), 2);
  // Verify that last uploaded configuration is stored.
  ValidateUploadedStatus(kEuiccStatusWithOneProfile);
}

TEST_F(EuiccStatusUploaderTest, MultipleProfiles) {
  SetUpDeviceProfiles(kSetupTwoEsimProfiles);

  auto status_uploader = CreateStatusUploader();
  // Initial upload request.
  EXPECT_EQ(GetRequestCount(), 1);
  // No value is uploaded yet.
  EXPECT_EQ("{}", GetStoredPrefString());

  // Make server accept requests.
  SetServerSuccessStatus(true);
  UpdateUploader(status_uploader.get());
  EXPECT_EQ(GetRequestCount(), 2);

  // Verify that last uploaded configuration is stored.
  ValidateUploadedStatus(kEuiccStatusWithTwoProfiles);
}

TEST_F(EuiccStatusUploaderTest, SameValueAsBefore) {
  // Make server accept requests.
  SetServerSuccessStatus(true);
  // Mark the current state as already uploaded.
  SetUpDeviceProfiles(kSetupOneEsimProfile);
  SetLastUploadedValue(kEuiccStatusWithOneProfile);

  auto status_uploader = CreateStatusUploader();
  // No value is uploaded since it has been previously sent.
  EXPECT_EQ(GetRequestCount(), 0);
}

TEST_F(EuiccStatusUploaderTest, NewValue) {
  // Make server accept requests.
  SetServerSuccessStatus(true);
  // Set up a value different from one that was previously uploaded.
  SetUpDeviceProfiles(kSetupOneEsimProfile);
  SetLastUploadedValue(kEmptyEuiccStatus);

  auto status_uploader = CreateStatusUploader();
  // Verify that last uploaded configuration is stored.
  ValidateUploadedStatus(kEuiccStatusWithOneProfile);
}

}  // namespace policy
