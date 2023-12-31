// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MEDIA_ROUTER_BROWSER_ROUTE_MESSAGE_OBSERVER_H_
#define COMPONENTS_MEDIA_ROUTER_BROWSER_ROUTE_MESSAGE_OBSERVER_H_

#include <stdint.h>

#include <vector>

#include "components/media_router/common/media_route.h"
#include "components/media_router/common/mojom/media_router.mojom.h"

namespace media_router {

class MediaRouter;

// Observes messages originating from the MediaSink connected to a MediaRoute.
// Messages are received from MediaRouter via |OnMessagesReceived|.
// TODO(imcheng): Rename to PresentationConnectionMessageObserver.
class RouteMessageObserver {
 public:
  // |route_id|: ID of MediaRoute to listen for messages.
  RouteMessageObserver(MediaRouter* router, const MediaRoute::Id& route_id);

  RouteMessageObserver(const RouteMessageObserver&) = delete;
  RouteMessageObserver& operator=(const RouteMessageObserver&) = delete;

  virtual ~RouteMessageObserver();

  // Invoked by |router_| whenever messages are received from the route sink.
  // |messages| is guaranteed to be non-empty.
  virtual void OnMessagesReceived(
      std::vector<mojom::RouteMessagePtr> messages) = 0;

  const MediaRoute::Id& route_id() const { return route_id_; }

 private:
  MediaRouter* const router_;
  const MediaRoute::Id route_id_;
};

}  // namespace media_router

#endif  // COMPONENTS_MEDIA_ROUTER_BROWSER_ROUTE_MESSAGE_OBSERVER_H_
