// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_COMPONENTS_QUICK_ANSWERS_RESULT_LOADER_H_
#define ASH_COMPONENTS_QUICK_ANSWERS_RESULT_LOADER_H_

#include <memory>
#include <string>

#include "ash/components/quick_answers/quick_answers_model.h"
#include "ash/components/quick_answers/search_result_parsers/search_response_parser.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
struct ResourceRequest;
}  // namespace network

namespace ash {
namespace quick_answers {

enum class IntentType;
struct PreprocessedOutput;

class ResultLoader {
 public:
  // A delegate interface for the ResultLoader.
  class ResultLoaderDelegate {
   public:
    ResultLoaderDelegate(const ResultLoaderDelegate&) = delete;
    ResultLoaderDelegate& operator=(const ResultLoaderDelegate&) = delete;

    // Invoked when there is a network error.
    virtual void OnNetworkError() {}

    // Invoked when the |quick_answer| is received. Note that |quick_answer| may
    // be |nullptr| if no answer found for the selected content.
    virtual void OnQuickAnswerReceived(
        std::unique_ptr<QuickAnswer> quick_answer) {}

   protected:
    ResultLoaderDelegate() = default;
    virtual ~ResultLoaderDelegate() = default;
  };

  // Callback used when parsing of |quick_answer| is complete. Note that
  // |quick_answer| may be |nullptr|.
  using ResponseParserCallback =
      base::OnceCallback<void(std::unique_ptr<QuickAnswer> quick_answer)>;

  using BuildRequestCallback = base::OnceCallback<void(
      std::unique_ptr<network::ResourceRequest> resource_request,
      const std::string& request_body)>;

  ResultLoader(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      ResultLoaderDelegate* delegate);

  ResultLoader(const ResultLoader&) = delete;
  ResultLoader& operator=(const ResultLoader&) = delete;

  // Virtual for testing.
  virtual ~ResultLoader();

  // Creates ResultLoader based on the |intent_type|.
  static std::unique_ptr<ResultLoader> Create(
      IntentType intent_type,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      ResultLoader::ResultLoaderDelegate* delegate);

  // Starts downloading of |quick_answers| associated with |selected_text|,
  // calling |ResultLoaderDelegate| methods when finished.
  // Note that delegate methods should be called only once per
  // ResultLoader instance. Virtual for testing.
  virtual void Fetch(const PreprocessedOutput& preprocessed_output);

 protected:
  // Builds the request URL from |selected_text|.
  virtual void BuildRequest(const PreprocessedOutput& preprocessed_output,
                            BuildRequestCallback callback) const = 0;

  // Process the |response_body| and invoked the callback with |QuickAnswer|.
  virtual void ProcessResponse(const PreprocessedOutput& preprocessed_output,
                               std::unique_ptr<std::string> response_body,
                               ResponseParserCallback complete_callback) = 0;

 private:
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  ResultLoaderDelegate* const delegate_;

  void OnBuildRequestComplete(
      const PreprocessedOutput& preprocessed_output,
      std::unique_ptr<network::ResourceRequest> resource_request,
      const std::string& request_body);
  void OnSimpleURLLoaderComplete(const PreprocessedOutput& preprocessed_output,
                                 std::unique_ptr<std::string> response_body);
  void OnResultParserComplete(std::unique_ptr<QuickAnswer> quick_answer);

  // Time when the query is issued.
  base::TimeTicks fetch_start_time_;

  base::WeakPtrFactory<ResultLoader> weak_factory_{this};
};

}  // namespace quick_answers
}  // namespace ash

#endif  // ASH_COMPONENTS_QUICK_ANSWERS_RESULT_LOADER_H_
