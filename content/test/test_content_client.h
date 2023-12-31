// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_TEST_TEST_CONTENT_CLIENT_H_
#define CONTENT_TEST_TEST_CONTENT_CLIENT_H_

#include "base/compiler_specific.h"
#include "content/public/common/content_client.h"

namespace content {

class TestContentClient : public ContentClient {
 public:
  TestContentClient();

  TestContentClient(const TestContentClient&) = delete;
  TestContentClient& operator=(const TestContentClient&) = delete;

  ~TestContentClient() override;

  // ContentClient:
  base::StringPiece GetDataResource(
      int resource_id,
      ui::ResourceScaleFactor scale_factor) override;
};

}  // namespace content

#endif  // CONTENT_TEST_TEST_CONTENT_CLIENT_H_
