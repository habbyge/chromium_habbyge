// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_OPTIMIZATION_GUIDE_CORE_BERT_MODEL_EXECUTOR_H_
#define COMPONENTS_OPTIMIZATION_GUIDE_CORE_BERT_MODEL_EXECUTOR_H_

#include "components/optimization_guide/core/model_executor.h"
#include "third_party/tflite_support/src/tensorflow_lite_support/cc/task/core/category.h"

namespace optimization_guide {

// A full implementation of a ModelExecutor that executes BERT models.
class BertModelExecutor
    : public ModelExecutor<std::vector<tflite::task::core::Category>,
                           const std::string&> {
 public:
  explicit BertModelExecutor(proto::OptimizationTarget optimization_target);
  ~BertModelExecutor() override;

  using ModelExecutionTask =
      tflite::task::core::BaseTaskApi<std::vector<tflite::task::core::Category>,
                                      const std::string&>;

  // ModelExecutor:
  absl::optional<std::vector<tflite::task::core::Category>> Execute(
      ModelExecutionTask* execution_task,
      const std::string& input) override;
  std::unique_ptr<ModelExecutionTask> BuildModelExecutionTask(
      base::MemoryMappedFile* model_file) override;

 private:
  const proto::OptimizationTarget optimization_target_;
};

}  // namespace optimization_guide

#endif  // COMPONENTS_OPTIMIZATION_GUIDE_CORE_BERT_MODEL_EXECUTOR_H_
