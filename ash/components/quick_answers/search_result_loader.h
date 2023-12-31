// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_COMPONENTS_QUICK_ANSWERS_SEARCH_RESULT_LOADER_H_
#define ASH_COMPONENTS_QUICK_ANSWERS_SEARCH_RESULT_LOADER_H_

#include <memory>
#include <string>

#include "ash/components/quick_answers/result_loader.h"
#include "ash/components/quick_answers/search_result_parsers/search_response_parser.h"
#include "base/memory/scoped_refptr.h"

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace ash {
namespace quick_answers {

class SearchResultLoader : public ResultLoader {
 public:
  SearchResultLoader(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      ResultLoaderDelegate* delegate);

  SearchResultLoader(const SearchResultLoader&) = delete;
  SearchResultLoader& operator=(const SearchResultLoader&) = delete;

  ~SearchResultLoader() override;

  // ResultLoader:
  void BuildRequest(const PreprocessedOutput& preprocessed_output,
                    BuildRequestCallback callback) const override;
  void ProcessResponse(const PreprocessedOutput& preprocessed_output,
                       std::unique_ptr<std::string> response_body,
                       ResponseParserCallback complete_callback) override;

 private:
  std::unique_ptr<SearchResponseParser> search_response_parser_;
};

}  // namespace quick_answers
}  // namespace ash

#endif  // ASH_COMPONENTS_QUICK_ANSWERS_SEARCH_RESULT_LOADER_H_
