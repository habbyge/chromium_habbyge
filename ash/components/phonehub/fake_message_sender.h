// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_COMPONENTS_PHONEHUB_FAKE_MESSAGE_SENDER_H_
#define ASH_COMPONENTS_PHONEHUB_FAKE_MESSAGE_SENDER_H_

#include "ash/components/phonehub/message_sender.h"

#include <stdint.h>
#include <string>
#include <vector>

#include "ash/components/phonehub/proto/phonehub_api.pb.h"

namespace chromeos {
namespace phonehub {

class FakeMessageSender : public MessageSender {
 public:
  FakeMessageSender();
  ~FakeMessageSender() override;

  // MessageSender:
  void SendCrosState(bool notification_enabled,
                     bool camera_roll_enabled) override;
  void SendUpdateNotificationModeRequest(bool do_not_disturb_enabled) override;
  void SendUpdateBatteryModeRequest(bool battery_saver_mode_enabled) override;
  void SendDismissNotificationRequest(int64_t notification_id) override;
  void SendNotificationInlineReplyRequest(
      int64_t notification_id,
      const std::u16string& reply_text) override;
  void SendShowNotificationAccessSetupRequest() override;
  void SendRingDeviceRequest(bool device_ringing_enabled) override;
  void SendFetchCameraRollItemsRequest(
      const proto::FetchCameraRollItemsRequest& request) override;
  void SendFetchCameraRollItemDataRequest(
      const proto::FetchCameraRollItemDataRequest& request) override;
  void SendInitiateCameraRollItemTransferRequest(
      const proto::InitiateCameraRollItemTransferRequest& request) override;

  std::pair<bool, bool> GetRecentCrosState() const;
  bool GetRecentUpdateNotificationModeRequest() const;
  bool GetRecentUpdateBatteryModeRequest() const;
  int64_t GetRecentDismissNotificationRequest() const;
  const std::pair<int64_t, std::u16string>
  GetRecentNotificationInlineReplyRequest() const;
  bool GetRecentRingDeviceRequest() const;
  const proto::FetchCameraRollItemsRequest&
  GetRecentFetchCameraRollItemsRequest() const;
  const proto::FetchCameraRollItemDataRequest&
  GetRecentFetchCameraRollItemDataRequest() const;
  const proto::InitiateCameraRollItemTransferRequest&
  GetRecentInitiateCameraRollItemTransferRequest() const;

  size_t GetCrosStateCallCount() const;

  size_t GetUpdateNotificationModeRequestCallCount() const;

  size_t GetUpdateBatteryModeRequestCallCount() const;

  size_t GetDismissNotificationRequestCallCount() const;

  size_t GetNotificationInlineReplyRequestCallCount() const;

  size_t show_notification_access_setup_request_count() const {
    return show_notification_access_setup_count_;
  }

  size_t GetRingDeviceRequestCallCount() const;

  size_t GetFetchCameraRollItemsRequestCallCount() const;

  size_t GetFetchCameraRollItemDataRequestCallCount() const;

  size_t GetInitiateCameraRollItemTransferRequestCallCount() const;

 private:
  std::vector<std::pair</*is_notifications_setting_enabled*/ bool,
                        /*is_camera_roll_setting_enabled*/ bool>>
      cros_states_;
  std::vector<bool> update_notification_mode_requests_;
  std::vector<bool> update_battery_mode_requests_;
  std::vector<int64_t> dismiss_notification_requests_;
  std::vector<std::pair<int64_t, std::u16string>>
      notification_inline_reply_requests_;
  std::vector<bool> ring_device_requests_;
  std::vector<proto::FetchCameraRollItemsRequest>
      fetch_camera_roll_items_requests_;
  std::vector<proto::FetchCameraRollItemDataRequest>
      fetch_camera_roll_item_data_requests_;
  std::vector<proto::InitiateCameraRollItemTransferRequest>
      initiate_camera_roll_item_transfer_requests_;
  size_t show_notification_access_setup_count_ = 0;
};

}  // namespace phonehub
}  // namespace chromeos

#endif  // ASH_COMPONENTS_PHONEHUB_FAKE_MESSAGE_SENDER_H_
