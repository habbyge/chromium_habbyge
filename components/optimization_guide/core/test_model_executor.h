// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_OPTIMIZATION_GUIDE_CORE_TEST_MODEL_EXECUTOR_H_
#define COMPONENTS_OPTIMIZATION_GUIDE_CORE_TEST_MODEL_EXECUTOR_H_

#include "components/optimization_guide/core/base_model_executor.h"

namespace optimization_guide {

class TestModelExecutor
    : public BaseModelExecutor<std::vector<float>, const std::vector<float>&> {
 public:
  TestModelExecutor() = default;
  ~TestModelExecutor() override = default;

 protected:
  absl::Status Preprocess(const std::vector<TfLiteTensor*>& input_tensors,
                          const std::vector<float>& input) override;

  std::vector<float> Postprocess(
      const std::vector<const TfLiteTensor*>& output_tensors) override;
};

}  // namespace optimization_guide

#endif  // COMPONENTS_OPTIMIZATION_GUIDE_CORE_TEST_MODEL_EXECUTOR_H_
