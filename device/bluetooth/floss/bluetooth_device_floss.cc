// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/floss/bluetooth_device_floss.h"

#include <memory>

#include "base/bind.h"
#include "base/notreached.h"
#include "components/device_event_log/device_event_log.h"
#include "dbus/bus.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device/bluetooth/bluetooth_gatt_connection.h"
#include "device/bluetooth/floss/bluetooth_adapter_floss.h"
#include "device/bluetooth/floss/floss_dbus_client.h"
#include "device/bluetooth/floss/floss_dbus_manager.h"

namespace floss {

namespace {

void OnCreateBond(BluetoothDeviceFloss::ConnectCallback callback,
                  const absl::optional<Void>& ret,
                  const absl::optional<Error>& error) {
  if (error.has_value()) {
    BLUETOOTH_LOG(ERROR) << "Failed to create bond: " << error->name << ": "
                         << error->message;
  }

  // In Floss API, |error| is not the error code of the pairing result, but only
  // the error code of the pairing request.
  // TODO(b/202874707): Handle the pairing result properly.
  // TODO(b/192289534): Record UMA metrics.
  auto connect_error =
      error ? absl::optional<BluetoothDeviceFloss::ConnectErrorCode>(
                  BluetoothDeviceFloss::ConnectErrorCode::ERROR_UNKNOWN)
            : absl::nullopt;

  std::move(callback).Run(connect_error);
}

}  // namespace

using AddressType = device::BluetoothDevice::AddressType;
using VendorIDSource = device::BluetoothDevice::VendorIDSource;

BluetoothDeviceFloss::~BluetoothDeviceFloss() = default;

uint32_t BluetoothDeviceFloss::GetBluetoothClass() const {
  NOTIMPLEMENTED();

  return 0;
}

device::BluetoothTransport BluetoothDeviceFloss::GetType() const {
  NOTIMPLEMENTED();

  return device::BluetoothTransport::BLUETOOTH_TRANSPORT_INVALID;
}

std::string BluetoothDeviceFloss::GetAddress() const {
  return address_;
}

AddressType BluetoothDeviceFloss::GetAddressType() const {
  NOTIMPLEMENTED();

  return AddressType::ADDR_TYPE_UNKNOWN;
}

VendorIDSource BluetoothDeviceFloss::GetVendorIDSource() const {
  NOTIMPLEMENTED();

  return VendorIDSource::VENDOR_ID_UNKNOWN;
}

uint16_t BluetoothDeviceFloss::GetVendorID() const {
  NOTIMPLEMENTED();

  return 0;
}

uint16_t BluetoothDeviceFloss::GetProductID() const {
  NOTIMPLEMENTED();

  return 0;
}

uint16_t BluetoothDeviceFloss::GetDeviceID() const {
  NOTIMPLEMENTED();

  return 0;
}

uint16_t BluetoothDeviceFloss::GetAppearance() const {
  NOTIMPLEMENTED();

  return 0;
}

absl::optional<std::string> BluetoothDeviceFloss::GetName() const {
  if (name_.length() == 0)
    return absl::nullopt;

  return name_;
}

bool BluetoothDeviceFloss::IsPaired() const {
  return bond_state_ == FlossAdapterClient::BondState::kBonded;
}

bool BluetoothDeviceFloss::IsConnected() const {
  return is_connected_;
}

bool BluetoothDeviceFloss::IsGattConnected() const {
  NOTIMPLEMENTED();

  return false;
}

bool BluetoothDeviceFloss::IsConnectable() const {
  NOTIMPLEMENTED();

  return false;
}

bool BluetoothDeviceFloss::IsConnecting() const {
  NOTIMPLEMENTED();

  return false;
}

#if defined(OS_CHROMEOS)
bool BluetoothDeviceFloss::IsBlockedByPolicy() const {
  NOTIMPLEMENTED();

  return false;
}
#endif

device::BluetoothDevice::UUIDSet BluetoothDeviceFloss::GetUUIDs() const {
  NOTIMPLEMENTED();

  return {};
}

absl::optional<int8_t> BluetoothDeviceFloss::GetInquiryRSSI() const {
  NOTIMPLEMENTED();

  return absl::nullopt;
}

absl::optional<int8_t> BluetoothDeviceFloss::GetInquiryTxPower() const {
  NOTIMPLEMENTED();

  return absl::nullopt;
}

bool BluetoothDeviceFloss::ExpectingPinCode() const {
  NOTIMPLEMENTED();

  return false;
}

bool BluetoothDeviceFloss::ExpectingPasskey() const {
  NOTIMPLEMENTED();

  return false;
}

bool BluetoothDeviceFloss::ExpectingConfirmation() const {
  NOTIMPLEMENTED();

  return false;
}

void BluetoothDeviceFloss::GetConnectionInfo(ConnectionInfoCallback callback) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::SetConnectionLatency(
    ConnectionLatency connection_latency,
    base::OnceClosure callback,
    ErrorCallback error_callback) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::Connect(
    device::BluetoothDevice::PairingDelegate* pairing_delegate,
    ConnectCallback callback) {
  BLUETOOTH_LOG(EVENT) << "Connecting to " << address_;

  if (IsPaired() || !pairing_delegate) {
    // No need to pair, or unable to, skip straight to connection.
    // TODO(b/202334519): Support connection flow without pairing.
  } else {
    FlossDBusManager::Get()->GetAdapterClient()->CreateBond(
        base::BindOnce(&OnCreateBond, std::move(callback)),
        FlossDeviceId({address_, name_}),
        FlossAdapterClient::BluetoothTransport::kAuto);
  }
}

void BluetoothDeviceFloss::SetPinCode(const std::string& pincode) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::SetPasskey(uint32_t passkey) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::ConfirmPairing() {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::RejectPairing() {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::CancelPairing() {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::Disconnect(base::OnceClosure callback,
                                      ErrorCallback error_callback) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::Forget(base::OnceClosure callback,
                                  ErrorCallback error_callback) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::ConnectToService(
    const device::BluetoothUUID& uuid,
    ConnectToServiceCallback callback,
    ConnectToServiceErrorCallback error_callback) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::ConnectToServiceInsecurely(
    const device::BluetoothUUID& uuid,
    ConnectToServiceCallback callback,
    ConnectToServiceErrorCallback error_callback) {
  NOTIMPLEMENTED();
}

std::unique_ptr<device::BluetoothGattConnection>
BluetoothDeviceFloss::CreateBluetoothGattConnectionObject() {
  NOTIMPLEMENTED();

  return nullptr;
}

void BluetoothDeviceFloss::SetGattServicesDiscoveryComplete(bool complete) {
  NOTIMPLEMENTED();
}

bool BluetoothDeviceFloss::IsGattServicesDiscoveryComplete() const {
  NOTIMPLEMENTED();

  return false;
}

void BluetoothDeviceFloss::Pair(
    device::BluetoothDevice::PairingDelegate* pairing_delegate,
    ConnectCallback callback) {
  NOTIMPLEMENTED();
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
void BluetoothDeviceFloss::ExecuteWrite(
    base::OnceClosure callback,
    ExecuteWriteErrorCallback error_callback) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::AbortWrite(base::OnceClosure callback,
                                      AbortWriteErrorCallback error_callback) {
  NOTIMPLEMENTED();
}
#endif

void BluetoothDeviceFloss::SetName(const std::string& name) {
  name_ = name;
}

void BluetoothDeviceFloss::SetBondState(
    FlossAdapterClient::BondState bond_state) {
  bond_state_ = bond_state;
}

void BluetoothDeviceFloss::SetIsConnected(bool is_connected) {
  is_connected_ = is_connected;
}

void BluetoothDeviceFloss::CreateGattConnectionImpl(
    absl::optional<device::BluetoothUUID> service_uuid) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::DisconnectGatt() {
  NOTIMPLEMENTED();
}

BluetoothDeviceFloss::BluetoothDeviceFloss(BluetoothAdapterFloss* adapter,
                                           const FlossDeviceId& device)
    : BluetoothDevice(adapter), address_(device.address), name_(device.name) {
  // TODO(abps): Add observers and cache data here.
}

void BluetoothDeviceFloss::ConnectInternal(ConnectCallback callback) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::OnConnect(ConnectCallback callback) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::OnConnectError(ConnectCallback callback,
                                          const Error& error) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::OnPairDuringConnect(ConnectCallback callback) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::OnPairDuringConnectError(ConnectCallback callback,
                                                    const Error& error) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::OnDisconnect(base::OnceClosure callback) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::OnDisconnectError(ErrorCallback error_callback,
                                             const Error& error) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::OnPair(ConnectCallback callback) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::OnPairError(ConnectCallback callback,
                                       const Error& error) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::OnCancelPairingError(const Error& error) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceFloss::OnForgetError(ErrorCallback error_callback,
                                         const Error& error) {
  NOTIMPLEMENTED();
}

}  // namespace floss
