// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/quick_pair/pairing/fast_pair/fake_fast_pair_gatt_service_client.h"
#include "ash/quick_pair/common/logging.h"
#include "ash/quick_pair/pairing/fast_pair/fast_pair_data_encryptor.h"
#include "base/callback_helpers.h"
#include "device/bluetooth/bluetooth_adapter.h"
#include "device/bluetooth/bluetooth_device.h"

namespace ash {
namespace quick_pair {

FakeFastPairGattServiceClient::FakeFastPairGattServiceClient(
    device::BluetoothDevice* device,
    scoped_refptr<device::BluetoothAdapter> adapter,
    base::OnceCallback<void(absl::optional<PairFailure>)> on_initialized_callback)
        : on_initialized_callback_(std::move(on_initialized_callback)) {}

FakeFastPairGattServiceClient::~FakeFastPairGattServiceClient() = default;

void FakeFastPairGattServiceClient::RunOnGattClientInitializedCallback(
    absl::optional<PairFailure> failure) {

  std::move(on_initialized_callback_).Run(failure);
}

device::BluetoothRemoteGattService* FakeFastPairGattServiceClient::gatt_service() {
  return nullptr;
}

void FakeFastPairGattServiceClient::WriteRequestAsync(
    uint8_t message_type,
    uint8_t flags,
    const std::string& provider_address,
    const std::string& seekers_address,
    FastPairDataEncryptor* fast_pair_data_encryptor,
    base::OnceCallback<void(std::vector<uint8_t>, absl::optional<PairFailure>)> write_response_callback) {

  key_based_write_response_callback_ = std::move(write_response_callback);
}

void FakeFastPairGattServiceClient::RunWriteResponseCallback(
    std::vector<uint8_t> data,
    absl::optional<PairFailure> failure) {
  std::move(key_based_write_response_callback_).Run(data, failure);
}

void FakeFastPairGattServiceClient::RunWritePasskeyCallback(
    std::vector<uint8_t> data,
    absl::optional<PairFailure> failure) {
  std::move(passkey_write_response_callback_).Run(data, failure);
}

void FakeFastPairGattServiceClient::WritePasskeyAsync(
    uint8_t message_type,
    uint32_t passkey,
    FastPairDataEncryptor* fast_pair_data_encryptor,
    base::OnceCallback<void(std::vector<uint8_t>, absl::optional<PairFailure>)>
        write_response_callback) {
  passkey_write_response_callback_ = std::move(write_response_callback);
}

void FakeFastPairGattServiceClient::WriteAccountKey(
    std::array<uint8_t, 16> account_key,
    FastPairDataEncryptor* fast_pair_data_encryptor,
    base::OnceCallback<
        void(absl::optional<device::BluetoothGattService::GattErrorCode>)>
        write_account_key_callback) {
  write_account_key_callback_ = std::move(write_account_key_callback);
}

void FakeFastPairGattServiceClient::RunWriteAccountKeyCallback(
    absl::optional<device::BluetoothGattService::GattErrorCode> error) {
  std::move(write_account_key_callback_).Run(error);
}

}  // namespace quick_pair
}  // namespace ash
