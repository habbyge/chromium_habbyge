// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_HISTORY_CORE_BROWSER_ANDROID_URLS_SQL_HANDLER_H_
#define COMPONENTS_HISTORY_CORE_BROWSER_ANDROID_URLS_SQL_HANDLER_H_

#include "components/history/core/browser/android/sql_handler.h"

namespace history {

class URLDatabase;

// This class is the SQLHandler implementation for urls table.
class UrlsSQLHandler : public SQLHandler {
 public:
  explicit UrlsSQLHandler(URLDatabase* url_db);

  UrlsSQLHandler(const UrlsSQLHandler&) = delete;
  UrlsSQLHandler& operator=(const UrlsSQLHandler&) = delete;

  ~UrlsSQLHandler() override;

  // Overriden from SQLHandler.
  bool Insert(HistoryAndBookmarkRow* row) override;
  bool Update(const HistoryAndBookmarkRow& row,
              const TableIDRows& ids_set) override;
  bool Delete(const TableIDRows& ids_set) override;

 private:
  URLDatabase* url_db_;
};

}  // namespace history.

#endif  // COMPONENTS_HISTORY_CORE_BROWSER_ANDROID_URLS_SQL_HANDLER_H_
