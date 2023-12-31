// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/wayland/host/wayland_data_device_base.h"

#include <utility>

#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/threading/thread_task_runner_handle.h"
#include "ui/ozone/platform/wayland/common/wayland_util.h"
#include "ui/ozone/platform/wayland/host/wayland_connection.h"
#include "ui/ozone/platform/wayland/host/wayland_data_offer_base.h"
#include "ui/ozone/platform/wayland/host/wayland_serial_tracker.h"

namespace ui {

WaylandDataDeviceBase::WaylandDataDeviceBase(WaylandConnection* connection)
    : connection_(connection) {}

WaylandDataDeviceBase::~WaylandDataDeviceBase() = default;

const std::vector<std::string>& WaylandDataDeviceBase::GetAvailableMimeTypes()
    const {
  if (!data_offer_) {
    static std::vector<std::string> dummy;
    return dummy;
  }

  return data_offer_->mime_types();
}

bool WaylandDataDeviceBase::ReadSelectionData(
    const std::string& mime_type,
    PlatformClipboard::RequestDataClosure callback) {
  DCHECK(callback);
  if (!data_offer_) {
    std::move(callback).Run(nullptr);
    return false;
  }

  base::ScopedFD fd = data_offer_->Receive(mime_type);
  if (!fd.is_valid()) {
    DPLOG(ERROR) << "Failed to open file descriptor.";
    std::move(callback).Run(nullptr);
    return false;
  }

  connection_->ScheduleFlush();

  // Schedule data reading to be done asynchronously in the thread pool as it
  // may take some time and blocking the UI thread for IO is undesirable.
  // TODO(crbug.com/913422): Use USER_VISIBLE once Clipboard becomes async.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_BLOCKING, base::MayBlock()},
      base::BindOnce(&WaylandDataDeviceBase::ReadFromFD, base::Unretained(this),
                     std::move(fd)),
      std::move(callback));
  return true;
}

void WaylandDataDeviceBase::ResetDataOffer() {
  data_offer_.reset();
}

PlatformClipboard::Data WaylandDataDeviceBase::ReadFromFD(
    base::ScopedFD fd) const {
  std::vector<uint8_t> contents;
  wl::ReadDataFromFD(std::move(fd), &contents);
  return base::RefCountedBytes::TakeVector(&contents);
}

void WaylandDataDeviceBase::RegisterDeferredReadCallback() {
  DCHECK(!deferred_read_callback_);

  deferred_read_callback_.reset(
      wl_display_sync(connection_->display_wrapper()));

  static constexpr wl_callback_listener kListener = {&DeferredReadCallback};

  wl_callback_add_listener(deferred_read_callback_.get(), &kListener, this);

  connection_->ScheduleFlush();
}

void WaylandDataDeviceBase::RegisterDeferredReadClosure(
    base::OnceClosure closure) {
  deferred_read_closure_ = std::move(closure);
}

// static
void WaylandDataDeviceBase::DeferredReadCallback(void* data,
                                                 struct wl_callback* cb,
                                                 uint32_t time) {
  auto* data_device = static_cast<WaylandDataDeviceBase*>(data);
  DCHECK(data_device);
  data_device->DeferredReadCallbackInternal(cb, time);
}

void WaylandDataDeviceBase::DeferredReadCallbackInternal(struct wl_callback* cb,
                                                         uint32_t time) {
  DCHECK(!deferred_read_closure_.is_null());

  // The callback must be reset before invoking the closure because the latter
  // may want to set another callback.  That typically happens when non-trivial
  // data types are dropped; they have fallbacks to plain text so several
  // roundtrips to data are chained.
  deferred_read_callback_.reset();

  std::move(deferred_read_closure_).Run();
}

void WaylandDataDeviceBase::NotifySelectionOffer(
    WaylandDataOfferBase* offer) const {
  if (selection_offer_callback_)
    selection_offer_callback_.Run(offer);
}

absl::optional<wl::Serial> WaylandDataDeviceBase::GetSerialForSelection()
    const {
  return connection_->serial_tracker().GetSerial({wl::SerialType::kTouchPress,
                                                  wl::SerialType::kMousePress,
                                                  wl::SerialType::kKeyPress});
}

}  // namespace ui
