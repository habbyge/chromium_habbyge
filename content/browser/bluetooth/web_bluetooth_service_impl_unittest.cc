// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/bluetooth/web_bluetooth_service_impl.h"

#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "content/browser/bluetooth/bluetooth_adapter_factory_wrapper.h"
#include "content/browser/bluetooth/bluetooth_allowed_devices.h"
#include "content/browser/bluetooth/web_bluetooth_pairing_manager.h"
#include "content/public/browser/bluetooth_delegate.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_features.h"
#include "content/public/test/navigation_simulator.h"
#include "content/test/test_render_view_host.h"
#include "content/test/test_web_contents.h"
#include "device/bluetooth/public/cpp/bluetooth_uuid.h"
#include "device/bluetooth/test/mock_bluetooth_adapter.h"
#include "device/bluetooth/test/mock_bluetooth_device.h"
#include "device/bluetooth/test/mock_bluetooth_gatt_characteristic.h"
#include "device/bluetooth/test/mock_bluetooth_gatt_notify_session.h"
#include "device/bluetooth/test/mock_bluetooth_gatt_service.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_remote.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/mojom/bluetooth/web_bluetooth.mojom.h"

namespace content {

namespace {

using ::blink::mojom::WebBluetoothCharacteristicClient;
using ::blink::mojom::WebBluetoothGATTQueryQuantity;
using ::blink::mojom::WebBluetoothRemoteGATTCharacteristicPtr;
using ::blink::mojom::WebBluetoothRemoteGATTServicePtr;
using ::blink::mojom::WebBluetoothResult;
using ::device::BluetoothDevice;
using ::device::BluetoothGattService;
using ::device::BluetoothRemoteGattCharacteristic;
using ::device::BluetoothRemoteGattService;
using ::device::BluetoothUUID;
using ::device::MockBluetoothAdapter;
using ::device::MockBluetoothDevice;
using ::device::MockBluetoothGattCharacteristic;
using ::device::MockBluetoothGattNotifySession;
using ::device::MockBluetoothGattService;
using ::testing::_;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::WithParamInterface;

const char kBatteryServiceUUIDString[] = "0000180f-0000-1000-8000-00805f9b34fb";
using PromptEventCallback =
    base::OnceCallback<void(BluetoothScanningPrompt::Event)>;

class MockWebBluetoothPairingManager : public WebBluetoothPairingManager {
 public:
  MockWebBluetoothPairingManager() = default;
  MockWebBluetoothPairingManager(const MockWebBluetoothPairingManager&) =
      delete;
  MockWebBluetoothPairingManager& operator=(
      const MockWebBluetoothPairingManager&) = delete;
  ~MockWebBluetoothPairingManager() override = default;

  MOCK_METHOD2(PairForCharacteristicReadValue,
               void(const std::string& characteristic_instance_id,
                    blink::mojom::WebBluetoothService::
                        RemoteCharacteristicReadValueCallback read_callback));
  MOCK_METHOD4(PairForCharacteristicWriteValue,
               void(const std::string& characteristic_instance_id,
                    const std::vector<uint8_t>& value,
                    blink::mojom::WebBluetoothWriteType write_type,
                    blink::mojom::WebBluetoothService::
                        RemoteCharacteristicWriteValueCallback callback));
  MOCK_METHOD2(
      PairForDescriptorReadValue,
      void(const std::string& descriptor_instance_id,
           blink::mojom::WebBluetoothService::RemoteDescriptorReadValueCallback
               read_callback));
  MOCK_METHOD3(
      PairForDescriptorWriteValue,
      void(const std::string& descriptor_instance_id,
           const std::vector<uint8_t>& value,
           blink::mojom::WebBluetoothService::RemoteDescriptorWriteValueCallback
               callback));
  MOCK_METHOD3(
      PairForCharacteristicStartNotifications,
      void(const std::string& characteristic_instance_id,
           mojo::AssociatedRemote<
               blink::mojom::WebBluetoothCharacteristicClient> client,
           blink::mojom::WebBluetoothService::
               RemoteCharacteristicStartNotificationsCallback callback));
};

class FakeBluetoothScanningPrompt : public BluetoothScanningPrompt {
 public:
  explicit FakeBluetoothScanningPrompt(
      PromptEventCallback prompt_event_callback)
      : prompt_event_callback_(std::move(prompt_event_callback)) {}
  ~FakeBluetoothScanningPrompt() override = default;

  // Move-only class.
  FakeBluetoothScanningPrompt(const FakeBluetoothScanningPrompt&) = delete;
  FakeBluetoothScanningPrompt& operator=(const FakeBluetoothScanningPrompt&) =
      delete;

  void RunPromptEventCallback(Event event) {
    if (prompt_event_callback_.is_null()) {
      FAIL() << "prompt_event_callback_ is not set";
    }
    std::move(prompt_event_callback_).Run(event);
  }

 private:
  PromptEventCallback prompt_event_callback_;
};

class FakeWebBluetoothCharacteristicClient : WebBluetoothCharacteristicClient {
 public:
  mojo::PendingAssociatedRemote<WebBluetoothCharacteristicClient>
  BindNewEndpointClientAndPassRemote() {
    receiver_.reset();
    return receiver_.BindNewEndpointAndPassDedicatedRemote();
  }

 protected:
  // WebBluetoothCharacteristicClient implementation:
  void RemoteCharacteristicValueChanged(
      const std::vector<uint8_t>& value) override {
    NOTREACHED();
  }

 private:
  mojo::AssociatedReceiver<WebBluetoothCharacteristicClient> receiver_{this};
};

class FakeBluetoothAdapter : public device::MockBluetoothAdapter {
 public:
  FakeBluetoothAdapter() = default;

  // Move-only class.
  FakeBluetoothAdapter(const FakeBluetoothAdapter&) = delete;
  FakeBluetoothAdapter& operator=(const FakeBluetoothAdapter&) = delete;

  // device::BluetoothAdapter:
  void StartScanWithFilter(
      std::unique_ptr<device::BluetoothDiscoveryFilter> discovery_filter,
      DiscoverySessionResultCallback callback) override {
    std::move(callback).Run(
        /*is_error=*/false,
        device::UMABluetoothDiscoverySessionOutcome::SUCCESS);
  }

  void AddObserver(BluetoothAdapter::Observer* observer) override {}

  void RemoveObserver(BluetoothAdapter::Observer* observer) override {}

  BluetoothDevice* GetDevice(const std::string& address) override {
    for (auto& device : mock_devices_) {
      if (device->GetAddress() == address)
        return device.get();
    }
    return nullptr;
  }

 private:
  ~FakeBluetoothAdapter() override = default;
};

class FakeBluetoothGattService : public NiceMock<MockBluetoothGattService> {
 public:
  FakeBluetoothGattService(MockBluetoothDevice* device,
                           const std::string& identifier,
                           const device::BluetoothUUID& uuid)
      : NiceMock<MockBluetoothGattService>(device,
                                           identifier,
                                           uuid,
                                           /*is_primary=*/true) {}
};

class FakeBluetoothDevice : public NiceMock<MockBluetoothDevice> {
 public:
  explicit FakeBluetoothDevice(MockBluetoothAdapter* adapter)
      : NiceMock<MockBluetoothDevice>(adapter,
                                      /*bluetooth_class=*/0,
                                      /*name=*/"device with battery",
                                      /*address=*/"00:00:01",
                                      /*paired=*/false,
                                      /*connected=*/true) {}

  bool IsGattServicesDiscoveryComplete() const override { return true; }

  std::vector<BluetoothRemoteGattService*> GetGattServices() const override {
    return GetMockServices();
  }

  BluetoothRemoteGattService* GetGattService(
      const std::string& identifier) const override {
    return GetMockService(identifier);
  }
};

class FakeBluetoothCharacteristic
    : public NiceMock<MockBluetoothGattCharacteristic> {
 public:
  FakeBluetoothCharacteristic(MockBluetoothGattService* service,
                              const std::string& identifier,
                              const device::BluetoothUUID& uuid,
                              Properties properties,
                              Permissions permissions)
      : NiceMock<MockBluetoothGattCharacteristic>(service,
                                                  identifier,
                                                  uuid,
                                                  properties,
                                                  permissions) {}

  void StartNotifySession(NotifySessionCallback callback,
                          ErrorCallback error_callback) override {
    if (defer_next_start_notification_) {
      defer_next_start_notification_ = false;
      DCHECK(deferred_start_notification_callback_.is_null());
      DCHECK(deferred_start_notification_error_callback_.is_null());
      deferred_start_notification_callback_ = std::move(callback);
      deferred_start_notification_error_callback_ = std::move(error_callback);
      return;
    }

    std::move(callback).Run(
        std::make_unique<MockBluetoothGattNotifySession>(GetWeakPtr()));
  }

  void ResumeDeferredStartNotification() {
    if (notification_start_error_code_.has_value()) {
      deferred_start_notification_callback_.Reset();
      std::move(deferred_start_notification_error_callback_)
          .Run(notification_start_error_code_.value());
    } else {
      deferred_start_notification_error_callback_.Reset();
      std::move(deferred_start_notification_callback_)
          .Run(std::make_unique<MockBluetoothGattNotifySession>(GetWeakPtr()));
    }
  }

  void DeferNextStartNotification(
      absl::optional<BluetoothGattService::GattErrorCode> error_code) {
    defer_next_start_notification_ = true;
    notification_start_error_code_ = error_code;
  }

 private:
  bool defer_next_start_notification_ = false;
  absl::optional<BluetoothGattService::GattErrorCode>
      notification_start_error_code_;
  NotifySessionCallback deferred_start_notification_callback_;
  ErrorCallback deferred_start_notification_error_callback_;
};

class TestBluetoothDelegate : public BluetoothDelegate {
 public:
  TestBluetoothDelegate() = default;
  ~TestBluetoothDelegate() override = default;
  TestBluetoothDelegate(const TestBluetoothDelegate&) = delete;
  TestBluetoothDelegate& operator=(const TestBluetoothDelegate&) = delete;

  // BluetoothDelegate:
  std::unique_ptr<BluetoothChooser> RunBluetoothChooser(
      RenderFrameHost* frame,
      const BluetoothChooser::EventHandler& event_handler) override {
    return nullptr;
  }
  std::unique_ptr<BluetoothScanningPrompt> ShowBluetoothScanningPrompt(
      RenderFrameHost* frame,
      const BluetoothScanningPrompt::EventHandler& event_handler) override {
    auto prompt =
        std::make_unique<FakeBluetoothScanningPrompt>(std::move(event_handler));
    prompt_ = prompt.get();
    return std::move(prompt);
  }
  void ShowDeviceCredentialsPrompt(content::RenderFrameHost* frame,
                                   const std::u16string& device_identifier,
                                   CredentialsCallback callback) override {
    std::move(callback).Run(DeviceCredentialsPromptResult::kCancelled, u"");
  }
  blink::WebBluetoothDeviceId GetWebBluetoothDeviceId(
      RenderFrameHost* frame,
      const std::string& device_address) override {
    return blink::WebBluetoothDeviceId();
  }
  std::string GetDeviceAddress(RenderFrameHost* frame,
                               const blink::WebBluetoothDeviceId&) override {
    return std::string();
  }
  blink::WebBluetoothDeviceId AddScannedDevice(
      RenderFrameHost* frame,
      const std::string& device_address) override {
    return blink::WebBluetoothDeviceId();
  }
  blink::WebBluetoothDeviceId GrantServiceAccessPermission(
      RenderFrameHost* frame,
      const device::BluetoothDevice* device,
      const blink::mojom::WebBluetoothRequestDeviceOptions* options) override {
    return blink::WebBluetoothDeviceId();
  }
  bool HasDevicePermission(
      RenderFrameHost* frame,
      const blink::WebBluetoothDeviceId& device_id) override {
    return false;
  }
  bool IsAllowedToAccessService(RenderFrameHost* frame,
                                const blink::WebBluetoothDeviceId& device_id,
                                const device::BluetoothUUID& service) override {
    return false;
  }
  bool IsAllowedToAccessAtLeastOneService(
      RenderFrameHost* frame,
      const blink::WebBluetoothDeviceId& device_id) override {
    return false;
  }
  bool IsAllowedToAccessManufacturerData(
      RenderFrameHost* frame,
      const blink::WebBluetoothDeviceId& device_id,
      const uint16_t manufacturer_code) override {
    return false;
  }
  std::vector<blink::mojom::WebBluetoothDevicePtr> GetPermittedDevices(
      RenderFrameHost* frame) override {
    return {};
  }

  void RunBluetoothScanningPromptEventCallback(
      BluetoothScanningPrompt::Event event) {
    if (!prompt_) {
      FAIL() << "ShowBluetoothScanningPrompt must be called before "
             << __func__;
    }
    prompt_->RunPromptEventCallback(event);
  }

  void AddFramePermissionObserver(FramePermissionObserver* observer) override {}
  void RemoveFramePermissionObserver(
      FramePermissionObserver* observer) override {}

 private:
  FakeBluetoothScanningPrompt* prompt_ = nullptr;
};

class TestContentBrowserClient : public ContentBrowserClient {
 public:
  TestContentBrowserClient() = default;
  ~TestContentBrowserClient() override = default;
  TestContentBrowserClient(const TestContentBrowserClient&) = delete;
  TestContentBrowserClient& operator=(const TestContentBrowserClient&) = delete;

  TestBluetoothDelegate* bluetooth_delegate() { return &bluetooth_delegate_; }

 protected:
  // ChromeContentBrowserClient:
  BluetoothDelegate* GetBluetoothDelegate() override {
    return &bluetooth_delegate_;
  }

 private:
  TestBluetoothDelegate bluetooth_delegate_;
};

class FakeWebBluetoothAdvertisementClientImpl
    : blink::mojom::WebBluetoothAdvertisementClient {
 public:
  FakeWebBluetoothAdvertisementClientImpl() = default;
  ~FakeWebBluetoothAdvertisementClientImpl() override = default;

  // Move-only class.
  FakeWebBluetoothAdvertisementClientImpl(
      const FakeWebBluetoothAdvertisementClientImpl&) = delete;
  FakeWebBluetoothAdvertisementClientImpl& operator=(
      const FakeWebBluetoothAdvertisementClientImpl&) = delete;

  // blink::mojom::WebBluetoothAdvertisementClient:
  void AdvertisingEvent(
      blink::mojom::WebBluetoothAdvertisingEventPtr event) override {}

  void BindReceiver(mojo::PendingAssociatedReceiver<
                    blink::mojom::WebBluetoothAdvertisementClient> receiver) {
    receiver_.Bind(std::move(receiver));
    receiver_.set_disconnect_handler(base::BindOnce(
        &FakeWebBluetoothAdvertisementClientImpl::OnConnectionError,
        base::Unretained(this)));
  }

  void OnConnectionError() { on_connection_error_called_ = true; }

  bool on_connection_error_called() { return on_connection_error_called_; }

 private:
  mojo::AssociatedReceiver<blink::mojom::WebBluetoothAdvertisementClient>
      receiver_{this};
  bool on_connection_error_called_ = false;
};

// A collection of Bluetooth objects which present related
// device/service/characteristic instances for battery device level testing.
class FakeBatteryObjectBundle {
 public:
  explicit FakeBatteryObjectBundle(scoped_refptr<FakeBluetoothAdapter> adapter)
      : adapter_(std::move(adapter)) {
    constexpr char kBatteryServiceId[] = "battery_service_id";
    constexpr char kBatteryLevelCharacteristicId[] = "battery_level_id";
    const device::BluetoothUUID kBatteryServiceUUID(kBatteryServiceUUIDString);
    const device::BluetoothUUID kBatteryLevelCharacteristicUUID(
        "00002a19-0000-1000-8000-00805f9b34fb");

    auto device = std::make_unique<FakeBluetoothDevice>(adapter_.get());
    device_ = device.get();

    auto service = std::make_unique<FakeBluetoothGattService>(
        device.get(), kBatteryServiceId, kBatteryServiceUUID);
    service_ = service.get();

    constexpr BluetoothRemoteGattCharacteristic::Properties
        kTestCharacteristicProperties =
            BluetoothRemoteGattCharacteristic::PROPERTY_BROADCAST |
            BluetoothRemoteGattCharacteristic::PROPERTY_READ |
            BluetoothRemoteGattCharacteristic::PROPERTY_INDICATE;
    auto characteristic = std::make_unique<FakeBluetoothCharacteristic>(
        service_, kBatteryLevelCharacteristicId,
        kBatteryLevelCharacteristicUUID, kTestCharacteristicProperties,
        BluetoothRemoteGattCharacteristic::PERMISSION_NONE);
    characteristic_ = characteristic.get();
    service->AddMockCharacteristic(std::move(characteristic));
    device->AddMockService(std::move(service));
    adapter_->AddMockDevice(std::move(device));
  }

  FakeBatteryObjectBundle& operator=(const FakeBatteryObjectBundle&) = delete;

  FakeBluetoothDevice& device() { return *device_; }

  FakeBluetoothGattService& service() { return *service_; }

  FakeBluetoothCharacteristic& characteristic() { return *characteristic_; }

 private:
  scoped_refptr<FakeBluetoothAdapter> adapter_;
  FakeBluetoothDevice* device_ = nullptr;
  FakeBluetoothGattService* service_ = nullptr;
  FakeBluetoothCharacteristic* characteristic_ = nullptr;
};  // namespace

}  // namespace

class WebBluetoothServiceImplTest : public RenderViewHostImplTestHarness,
                                    public WithParamInterface<bool> {
 public:
  WebBluetoothServiceImplTest() = default;
  ~WebBluetoothServiceImplTest() override = default;

  // Move-only class.
  WebBluetoothServiceImplTest(const WebBluetoothServiceImplTest&) = delete;
  WebBluetoothServiceImplTest& operator=(const WebBluetoothServiceImplTest&) =
      delete;

  void SetUp() override {
    RenderViewHostImplTestHarness::SetUp();

    // Set up an adapter.
    adapter_ = new FakeBluetoothAdapter();
    EXPECT_CALL(*adapter_, IsPresent()).WillRepeatedly(Return(true));
    BluetoothAdapterFactoryWrapper::Get().SetBluetoothAdapterForTesting(
        adapter_);
    battery_object_bundle_ =
        std::make_unique<FakeBatteryObjectBundle>(adapter_);

    // Hook up the test bluetooth delegate.
    old_browser_client_ = SetBrowserClientForTesting(&browser_client_);

    contents()->GetMainFrame()->InitializeRenderFrameIfNeeded();

    // Navigate to a URL so that WebBluetoothServiceImpl::GetOrigin() returns a
    // valid origin. This is required when checking for Bluetooth permissions.
    constexpr char kTestURL[] = "https://my-battery-level.com";
    NavigationSimulator::NavigateAndCommitFromBrowser(contents(),
                                                      GURL(kTestURL));

    // Simulate a frame connected to a bluetooth service.
    service_ =
        contents()->GetMainFrame()->CreateWebBluetoothServiceForTesting();

    // GetAvailability connects the Web Bluetooth service to the adapter.
    base::RunLoop run_loop;
    service_->GetAvailability(base::BindLambdaForTesting(
        [&run_loop](bool success) { run_loop.Quit(); }));
    run_loop.Run();
  }

  void TearDown() override {
    adapter_.reset();
    battery_object_bundle_.reset();
    service_ = nullptr;
    SetBrowserClientForTesting(old_browser_client_);
    RenderViewHostImplTestHarness::TearDown();
  }

  mojo::PendingAssociatedRemote<WebBluetoothCharacteristicClient>
  BindCharacteristicClientAndPassRemote() {
    return characteristic_client_.BindNewEndpointClientAndPassRemote();
  }

 protected:
  blink::mojom::WebBluetoothLeScanFilterPtr CreateScanFilter(
      const std::string& name,
      const std::string& name_prefix) {
    absl::optional<std::vector<device::BluetoothUUID>> services;
    services.emplace();
    services->push_back(device::BluetoothUUID(kBatteryServiceUUIDString));
    return blink::mojom::WebBluetoothLeScanFilter::New(
        services, name, name_prefix, /*manufacturer_data=*/absl::nullopt);
  }

  blink::mojom::WebBluetoothResult RequestScanningStartAndSimulatePromptEvent(
      const blink::mojom::WebBluetoothLeScanFilter& filter,
      FakeWebBluetoothAdvertisementClientImpl* client_impl,
      BluetoothScanningPrompt::Event event) {
    mojo::PendingAssociatedRemote<blink::mojom::WebBluetoothAdvertisementClient>
        client;
    client_impl->BindReceiver(client.InitWithNewEndpointAndPassReceiver());
    auto options = blink::mojom::WebBluetoothRequestLEScanOptions::New();
    options->filters.emplace();
    auto filter_ptr = blink::mojom::WebBluetoothLeScanFilter::New(
        filter.services, filter.name, filter.name_prefix,
        /*manufacturer_data=*/absl::nullopt);
    options->filters->push_back(std::move(filter_ptr));

    // Use two RunLoops to guarantee the order of operations for this test.
    // |callback_loop| guarantees that RequestScanningStartCallback has finished
    // executing and |result| has been populated. |request_loop| ensures that
    // the entire RequestScanningStart flow has finished before the method
    // returns.
    base::RunLoop callback_loop, request_loop;
    blink::mojom::WebBluetoothResult result;
    service_->RequestScanningStart(
        std::move(client), std::move(options),
        base::BindLambdaForTesting(
            [&callback_loop, &result](blink::mojom::WebBluetoothResult r) {
              result = std::move(r);
              callback_loop.Quit();
            }));

    // Post a task to simulate a prompt event during a call to
    // RequestScanningStart().
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindLambdaForTesting(
                       [&callback_loop, &event, &request_loop, this]() {
                         browser_client_.bluetooth_delegate()
                             ->RunBluetoothScanningPromptEventCallback(event);
                         callback_loop.Run();
                         request_loop.Quit();
                       }));
    request_loop.Run();
    return result;
  }

  void RegisterTestCharacteristic() {
    auto device_options = blink::mojom::WebBluetoothRequestDeviceOptions::New();
    device_options->accept_all_devices = true;
    device_options->optional_services.push_back(
        test_bundle().service().GetUUID());
    const blink::WebBluetoothDeviceId& test_device_id =
        service_->allowed_devices().AddDevice(
            test_bundle().device().GetAddress(), device_options);

    FakeBluetoothCharacteristic& test_characteristic =
        test_bundle().characteristic();

    {
      base::RunLoop run_loop;
      service_->RemoteServerGetPrimaryServices(
          test_device_id, WebBluetoothGATTQueryQuantity::SINGLE,
          test_bundle().service().GetUUID(),
          base::BindLambdaForTesting(
              [&run_loop](
                  WebBluetoothResult result,
                  absl::optional<std::vector<WebBluetoothRemoteGATTServicePtr>>
                      services) {
                EXPECT_EQ(result, WebBluetoothResult::SUCCESS);
                run_loop.Quit();
              }));
      run_loop.Run();
    }

    {
      base::RunLoop run_loop;
      service_->RemoteServiceGetCharacteristics(
          test_bundle().service().GetIdentifier(),
          WebBluetoothGATTQueryQuantity::SINGLE, test_characteristic.GetUUID(),
          base::BindLambdaForTesting(
              [&run_loop](
                  WebBluetoothResult result,
                  absl::optional<
                      std::vector<WebBluetoothRemoteGATTCharacteristicPtr>>
                      characteristic) {
                EXPECT_EQ(result, WebBluetoothResult::SUCCESS);
                run_loop.Quit();
              }));
      run_loop.Run();
    }
  }

  FakeBatteryObjectBundle& test_bundle() const {
    return *battery_object_bundle_;
  }

  scoped_refptr<FakeBluetoothAdapter> adapter_;
  WebBluetoothServiceImpl* service_;
  TestContentBrowserClient browser_client_;
  ContentBrowserClient* old_browser_client_ = nullptr;
  std::unique_ptr<FakeBatteryObjectBundle> battery_object_bundle_;
  FakeWebBluetoothCharacteristicClient characteristic_client_;
};

// TODO(crbug.com/1213499): Delete parameterized test when bonding is deemed
// stable, and the flag is removed.
class WebBluetoothServiceImplBondingTest : public WebBluetoothServiceImplTest {
 public:
  void SetUp() override {
    feature_list_.InitWithFeatureState(features::kWebBluetoothBondOnDemand,
                                       on_demand_bonding_enabled());
    WebBluetoothServiceImplTest::SetUp();
  }

  bool on_demand_bonding_enabled() const { return GetParam(); }

 private:
  base::test::ScopedFeatureList feature_list_;
};

TEST_F(WebBluetoothServiceImplTest, ClearStateDuringRequestDevice) {
  auto options = blink::mojom::WebBluetoothRequestDeviceOptions::New();
  options->accept_all_devices = true;

  base::RunLoop loop;
  service_->RequestDevice(
      std::move(options),
      base::BindLambdaForTesting(
          [&loop](blink::mojom::WebBluetoothResult,
                  blink::mojom::WebBluetoothDevicePtr) { loop.Quit(); }));
  service_->ClearState();
  loop.Run();
}

TEST_F(WebBluetoothServiceImplTest, PermissionAllowed) {
  blink::mojom::WebBluetoothLeScanFilterPtr filter = CreateScanFilter("a", "b");
  absl::optional<WebBluetoothServiceImpl::ScanFilters> filters;
  filters.emplace();
  filters->push_back(filter.Clone());
  EXPECT_FALSE(service_->AreScanFiltersAllowed(filters));

  FakeWebBluetoothAdvertisementClientImpl client_impl;
  blink::mojom::WebBluetoothResult result =
      RequestScanningStartAndSimulatePromptEvent(
          *filter, &client_impl, BluetoothScanningPrompt::Event::kAllow);
  EXPECT_EQ(result, blink::mojom::WebBluetoothResult::SUCCESS);
  // |filters| should be allowed.
  EXPECT_TRUE(service_->AreScanFiltersAllowed(filters));
}

TEST_F(WebBluetoothServiceImplTest, ClearStateDuringRequestScanningStart) {
  blink::mojom::WebBluetoothLeScanFilterPtr filter = CreateScanFilter("a", "b");
  absl::optional<WebBluetoothServiceImpl::ScanFilters> filters;

  FakeWebBluetoothAdvertisementClientImpl client_impl;
  mojo::PendingAssociatedRemote<blink::mojom::WebBluetoothAdvertisementClient>
      client;
  client_impl.BindReceiver(client.InitWithNewEndpointAndPassReceiver());

  auto options = blink::mojom::WebBluetoothRequestLEScanOptions::New();
  options->filters.emplace();
  options->filters->push_back(std::move(filter));

  // Use two RunLoops to guarantee the order of operations for this test.
  // |callback_loop| guarantees that RequestScanningStartCallback has finished
  // executing and |result| has been populated. |request_loop| ensures that the
  // entire RequestScanningStart flow has finished before |result| is checked.
  base::RunLoop callback_loop, request_loop;
  blink::mojom::WebBluetoothResult result;
  service_->RequestScanningStart(
      std::move(client), std::move(options),
      base::BindLambdaForTesting(
          [&callback_loop, &result](blink::mojom::WebBluetoothResult r) {
            result = std::move(r);
            callback_loop.Quit();
          }));

  // Post a task to clear the WebBluetoothService state during a call to
  // RequestScanningStart(). This should cause the RequestScanningStartCallback
  // to be run with an error result.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindLambdaForTesting([&callback_loop, this, &request_loop]() {
        service_->ClearState();
        callback_loop.Run();
        request_loop.Quit();
      }));
  request_loop.Run();

  EXPECT_NE(result, blink::mojom::WebBluetoothResult::SUCCESS);
}

TEST_F(WebBluetoothServiceImplTest, PermissionPromptCanceled) {
  blink::mojom::WebBluetoothLeScanFilterPtr filter = CreateScanFilter("a", "b");
  absl::optional<WebBluetoothServiceImpl::ScanFilters> filters;
  filters.emplace();
  filters->push_back(filter.Clone());
  EXPECT_FALSE(service_->AreScanFiltersAllowed(filters));

  FakeWebBluetoothAdvertisementClientImpl client_impl;
  blink::mojom::WebBluetoothResult result =
      RequestScanningStartAndSimulatePromptEvent(
          *filter, &client_impl, BluetoothScanningPrompt::Event::kCanceled);

  EXPECT_EQ(blink::mojom::WebBluetoothResult::PROMPT_CANCELED, result);
  // |filters| should still not be allowed.
  EXPECT_FALSE(service_->AreScanFiltersAllowed(filters));
}

TEST_F(WebBluetoothServiceImplTest,
       BluetoothScanningPermissionRevokedWhenTabHidden) {
  blink::mojom::WebBluetoothLeScanFilterPtr filter = CreateScanFilter("a", "b");
  absl::optional<WebBluetoothServiceImpl::ScanFilters> filters;
  filters.emplace();
  filters->push_back(filter.Clone());
  FakeWebBluetoothAdvertisementClientImpl client_impl;
  blink::mojom::WebBluetoothResult result =
      RequestScanningStartAndSimulatePromptEvent(
          *filter, &client_impl, BluetoothScanningPrompt::Event::kAllow);
  EXPECT_EQ(result, blink::mojom::WebBluetoothResult::SUCCESS);
  EXPECT_TRUE(service_->AreScanFiltersAllowed(filters));

  contents()->SetVisibilityAndNotifyObservers(Visibility::HIDDEN);

  // The previously granted Bluetooth scanning permission should be revoked.
  EXPECT_FALSE(service_->AreScanFiltersAllowed(filters));
}

TEST_F(WebBluetoothServiceImplTest,
       BluetoothScanningPermissionRevokedWhenTabOccluded) {
  blink::mojom::WebBluetoothLeScanFilterPtr filter = CreateScanFilter("a", "b");
  absl::optional<WebBluetoothServiceImpl::ScanFilters> filters;
  filters.emplace();
  filters->push_back(filter.Clone());
  FakeWebBluetoothAdvertisementClientImpl client_impl;
  RequestScanningStartAndSimulatePromptEvent(
      *filter, &client_impl, BluetoothScanningPrompt::Event::kAllow);
  EXPECT_TRUE(service_->AreScanFiltersAllowed(filters));

  contents()->SetVisibilityAndNotifyObservers(Visibility::OCCLUDED);

  // The previously granted Bluetooth scanning permission should be revoked.
  EXPECT_FALSE(service_->AreScanFiltersAllowed(filters));
}

TEST_F(WebBluetoothServiceImplTest,
       BluetoothScanningPermissionRevokedWhenFocusIsLost) {
  blink::mojom::WebBluetoothLeScanFilterPtr filter = CreateScanFilter("a", "b");
  absl::optional<WebBluetoothServiceImpl::ScanFilters> filters;
  filters.emplace();
  filters->push_back(filter.Clone());
  FakeWebBluetoothAdvertisementClientImpl client_impl;
  RequestScanningStartAndSimulatePromptEvent(
      *filter, &client_impl, BluetoothScanningPrompt::Event::kAllow);
  EXPECT_TRUE(service_->AreScanFiltersAllowed(filters));

  main_test_rfh()->GetRenderWidgetHost()->LostFocus();

  // The previously granted Bluetooth scanning permission should be revoked.
  EXPECT_FALSE(service_->AreScanFiltersAllowed(filters));
}

TEST_F(WebBluetoothServiceImplTest,
       BluetoothScanningPermissionRevokedWhenBlocked) {
  blink::mojom::WebBluetoothLeScanFilterPtr filter_1 =
      CreateScanFilter("a", "b");
  absl::optional<WebBluetoothServiceImpl::ScanFilters> filters_1;
  filters_1.emplace();
  filters_1->push_back(filter_1.Clone());
  FakeWebBluetoothAdvertisementClientImpl client_impl_1;
  blink::mojom::WebBluetoothResult result_1 =
      RequestScanningStartAndSimulatePromptEvent(
          *filter_1, &client_impl_1, BluetoothScanningPrompt::Event::kAllow);
  EXPECT_EQ(result_1, blink::mojom::WebBluetoothResult::SUCCESS);
  EXPECT_TRUE(service_->AreScanFiltersAllowed(filters_1));
  EXPECT_FALSE(client_impl_1.on_connection_error_called());

  blink::mojom::WebBluetoothLeScanFilterPtr filter_2 =
      CreateScanFilter("c", "d");
  absl::optional<WebBluetoothServiceImpl::ScanFilters> filters_2;
  filters_2.emplace();
  filters_2->push_back(filter_2.Clone());
  FakeWebBluetoothAdvertisementClientImpl client_impl_2;
  blink::mojom::WebBluetoothResult result_2 =
      RequestScanningStartAndSimulatePromptEvent(
          *filter_2, &client_impl_2, BluetoothScanningPrompt::Event::kAllow);
  EXPECT_EQ(result_2, blink::mojom::WebBluetoothResult::SUCCESS);
  EXPECT_TRUE(service_->AreScanFiltersAllowed(filters_2));
  EXPECT_FALSE(client_impl_2.on_connection_error_called());

  blink::mojom::WebBluetoothLeScanFilterPtr filter_3 =
      CreateScanFilter("e", "f");
  absl::optional<WebBluetoothServiceImpl::ScanFilters> filters_3;
  filters_3.emplace();
  filters_3->push_back(filter_3.Clone());
  FakeWebBluetoothAdvertisementClientImpl client_impl_3;
  blink::mojom::WebBluetoothResult result_3 =
      RequestScanningStartAndSimulatePromptEvent(
          *filter_3, &client_impl_3, BluetoothScanningPrompt::Event::kBlock);
  EXPECT_EQ(blink::mojom::WebBluetoothResult::SCANNING_BLOCKED, result_3);
  EXPECT_FALSE(service_->AreScanFiltersAllowed(filters_3));

  // The previously granted Bluetooth scanning permission should be revoked.
  EXPECT_FALSE(service_->AreScanFiltersAllowed(filters_1));
  EXPECT_FALSE(service_->AreScanFiltersAllowed(filters_2));

  base::RunLoop().RunUntilIdle();

  // All existing scanning clients are disconnected.
  EXPECT_TRUE(client_impl_1.on_connection_error_called());
  EXPECT_TRUE(client_impl_2.on_connection_error_called());
  EXPECT_TRUE(client_impl_3.on_connection_error_called());
}

TEST_F(WebBluetoothServiceImplTest,
       ReadCharacteristicValueErrorWithValueIgnored) {
  // The contract for calls accepting a
  // BluetoothRemoteGattCharacteristic::ValueCallback callback argument is that
  // when an error occurs, value must be ignored. This test verifies that
  // WebBluetoothServiceImpl::OnCharacteristicReadValue honors that contract
  // and will not pass a value to it's callback
  // (a RemoteCharacteristicReadValueCallback instance) when an error occurs
  // with a non-empty value array.
  const std::vector<uint8_t> read_error_value = {1, 2, 3};
  bool callback_called = false;
  const std::string characteristic_instance_id = "fake-id";
  service_->OnCharacteristicReadValue(
      characteristic_instance_id,
      base::BindLambdaForTesting(
          [&callback_called](
              blink::mojom::WebBluetoothResult result,
              const absl::optional<std::vector<uint8_t>>& value) {
            callback_called = true;
            EXPECT_EQ(
                blink::mojom::WebBluetoothResult::GATT_OPERATION_IN_PROGRESS,
                result);
            EXPECT_FALSE(value.has_value());
          }),
      device::BluetoothGattService::GATT_ERROR_IN_PROGRESS, read_error_value);
  EXPECT_TRUE(callback_called);

  // This test doesn't invoke any methods of the mock adapter. Allow it to be
  // leaked without producing errors.
  Mock::AllowLeak(adapter_.get());
}

#if PAIR_BLUETOOTH_ON_DEMAND()
TEST_P(WebBluetoothServiceImplBondingTest,
       ReadCharacteristicValueNotAuthorized) {
  const std::vector<uint8_t> read_error_value = {1, 2, 3};
  bool read_value_callback_called = false;

  RegisterTestCharacteristic();
  const FakeBluetoothCharacteristic& test_characteristic =
      test_bundle().characteristic();

  MockWebBluetoothPairingManager* pairing_manager =
      new MockWebBluetoothPairingManager();
  service_->SetPairingManagerForTesting(
      std::unique_ptr<WebBluetoothPairingManager>(pairing_manager));

  EXPECT_CALL(*pairing_manager, PairForCharacteristicReadValue(_, _))
      .Times(on_demand_bonding_enabled() ? 1 : 0);

  service_->OnCharacteristicReadValue(
      test_characteristic.GetIdentifier(),
      base::BindLambdaForTesting(
          [&read_value_callback_called](
              blink::mojom::WebBluetoothResult result,
              const absl::optional<std::vector<uint8_t>>& value) {
            read_value_callback_called = true;
            EXPECT_EQ(blink::mojom::WebBluetoothResult::GATT_NOT_AUTHORIZED,
                      result);
            EXPECT_FALSE(value.has_value());
          }),
      device::BluetoothGattService::GATT_ERROR_NOT_AUTHORIZED,
      read_error_value);
  EXPECT_EQ(!on_demand_bonding_enabled(), read_value_callback_called);
}

INSTANTIATE_TEST_SUITE_P(All,
                         WebBluetoothServiceImplBondingTest,
                         testing::Values(false, true));
#endif  // PAIR_BLUETOOTH_ON_DEMAND()

TEST_F(WebBluetoothServiceImplTest, DeferredStartNotifySession) {
  RegisterTestCharacteristic();
  FakeBluetoothCharacteristic& test_characteristic =
      test_bundle().characteristic();

  // Test both failing.
  {
    base::RunLoop run_loop;
    int outstanding_callbacks = 2;

    test_characteristic.DeferNextStartNotification(
        BluetoothGattService::GATT_ERROR_FAILED);

    auto callback = base::BindLambdaForTesting(
        [&run_loop, &outstanding_callbacks](WebBluetoothResult result) {
          EXPECT_EQ(result, WebBluetoothResult::GATT_UNKNOWN_FAILURE);
          if (--outstanding_callbacks == 0)
            run_loop.Quit();
        });
    service_->RemoteCharacteristicStartNotifications(
        test_characteristic.GetIdentifier(),
        BindCharacteristicClientAndPassRemote(), callback);

    service_->RemoteCharacteristicStartNotifications(
        test_characteristic.GetIdentifier(),
        BindCharacteristicClientAndPassRemote(), callback);

    test_characteristic.ResumeDeferredStartNotification();

    run_loop.Run();
  }

  // Test both succeeding.
  {
    base::RunLoop run_loop;
    int outstanding_callbacks = 2;

    test_characteristic.DeferNextStartNotification(
        /*error_code=*/absl::nullopt);

    auto callback = base::BindLambdaForTesting(
        [&run_loop, &outstanding_callbacks](WebBluetoothResult result) {
          EXPECT_EQ(result, WebBluetoothResult::SUCCESS);
          if (--outstanding_callbacks == 0)
            run_loop.Quit();
        });
    service_->RemoteCharacteristicStartNotifications(
        test_characteristic.GetIdentifier(),
        BindCharacteristicClientAndPassRemote(), callback);

    service_->RemoteCharacteristicStartNotifications(
        test_characteristic.GetIdentifier(),
        BindCharacteristicClientAndPassRemote(), callback);

    test_characteristic.ResumeDeferredStartNotification();

    run_loop.Run();
  }
}

}  // namespace content
