// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_TEST_MOCK_RESOURCE_CONTEXT_H_
#define CONTENT_PUBLIC_TEST_MOCK_RESOURCE_CONTEXT_H_

#include "base/compiler_specific.h"
#include "content/public/browser/resource_context.h"

namespace content {

class MockResourceContext : public ResourceContext {
 public:
  MockResourceContext();

  MockResourceContext(const MockResourceContext&) = delete;
  MockResourceContext& operator=(const MockResourceContext&) = delete;

  ~MockResourceContext() override;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_TEST_MOCK_RESOURCE_CONTEXT_H_
