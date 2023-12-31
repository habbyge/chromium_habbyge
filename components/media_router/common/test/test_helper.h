// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MEDIA_ROUTER_COMMON_TEST_TEST_HELPER_H_
#define COMPONENTS_MEDIA_ROUTER_COMMON_TEST_TEST_HELPER_H_

#include "base/timer/mock_timer.h"
#include "build/build_config.h"
#include "components/media_router/common/discovery/media_sink_service_base.h"

class MediaSink;

namespace media_router {

MediaSink CreateCastSink(const std::string& id, const std::string& name);
MediaSink CreateDialSink(const std::string& id, const std::string& name);
MediaSink CreateWiredDisplaySink(const std::string& id,
                                 const std::string& name);

#if !defined(OS_ANDROID)
class TestMediaSinkService : public MediaSinkServiceBase {
 public:
  TestMediaSinkService();
  explicit TestMediaSinkService(const OnSinksDiscoveredCallback& callback);

  TestMediaSinkService(const TestMediaSinkService&) = delete;
  TestMediaSinkService& operator=(const TestMediaSinkService&) = delete;

  ~TestMediaSinkService() override;

  base::MockOneShotTimer* timer() { return timer_; }

 private:
  // Owned by MediaSinkService.
  base::MockOneShotTimer* timer_;
};
#endif  // !defined(OS_ANDROID)

}  // namespace media_router

#endif  // COMPONENTS_MEDIA_ROUTER_COMMON_TEST_TEST_HELPER_H_
