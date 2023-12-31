// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_QUOTA_INTERNALS_QUOTA_INTERNALS_PROXY_H_
#define CHROME_BROWSER_UI_WEBUI_QUOTA_INTERNALS_QUOTA_INTERNALS_PROXY_H_

#include <stdint.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner_helpers.h"
#include "content/public/browser/browser_thread.h"
#include "storage/browser/quota/quota_manager.h"
#include "third_party/blink/public/mojom/quota/quota_types.mojom-forward.h"

namespace quota_internals {

class QuotaInternalsHandler;
class GlobalStorageInfo;
class PerHostStorageInfo;
class PerOriginStorageInfo;
typedef std::map<std::string, std::string> Statistics;

// This class is the bridge between QuotaInternalsHandler and QuotaManager.
// Each QuotaInternalsHandler instances creates and owns a instance of this
// class.
class QuotaInternalsProxy
    : public base::RefCountedThreadSafe<
          QuotaInternalsProxy,
          content::BrowserThread::DeleteOnIOThread> {
 public:
  explicit QuotaInternalsProxy(QuotaInternalsHandler* handler);

  QuotaInternalsProxy(const QuotaInternalsProxy&) = delete;
  QuotaInternalsProxy& operator=(const QuotaInternalsProxy&) = delete;

  void RequestInfo(scoped_refptr<storage::QuotaManager> quota_manager);
  void TriggerStoragePressure(
      url::Origin origin,
      scoped_refptr<storage::QuotaManager> quota_manager);

 private:
  friend class base::DeleteHelper<QuotaInternalsProxy>;
  friend struct content::BrowserThread::DeleteOnThread<
      content::BrowserThread::IO>;
  friend class QuotaInternalsHandler;

  using QuotaTableEntries = storage::QuotaManager::QuotaTableEntries;
  using BucketTableEntries = storage::QuotaManager::BucketTableEntries;

  virtual ~QuotaInternalsProxy();

  void ReportAvailableSpace(int64_t available_space);
  void ReportGlobalInfo(const GlobalStorageInfo& data);
  void ReportPerHostInfo(const std::vector<PerHostStorageInfo>& hosts);
  void ReportPerOriginInfo(const std::vector<PerOriginStorageInfo>& origins);
  void ReportStatistics(const Statistics& stats);

  // Called on IO Thread by QuotaManager as callback.
  void DidGetSettings(const storage::QuotaSettings& settings);
  void DidGetCapacity(int64_t total_space, int64_t available_space);
  void DidGetGlobalUsage(blink::mojom::StorageType type,
                         int64_t usage,
                         int64_t unlimited_usage);
  void DidDumpQuotaTable(const QuotaTableEntries& entries);
  void DidDumpBucketTable(const BucketTableEntries& entries);
  void DidGetHostUsage(const std::string& host,
                       blink::mojom::StorageType type,
                       int64_t usage,
                       blink::mojom::UsageBreakdownPtr usage_breakdown);

  // Helper. Called on IO Thread.
  void RequestPerOriginInfo(blink::mojom::StorageType type);
  void VisitHost(const std::string& host, blink::mojom::StorageType type);
  void GetHostUsage(const std::string& host, blink::mojom::StorageType type);

  // Used on UI Thread.
  QuotaInternalsHandler* handler_;

  // Used on IO Thread.
  scoped_refptr<storage::QuotaManager> quota_manager_;
  std::set<std::pair<std::string, blink::mojom::StorageType>> hosts_visited_,
      hosts_pending_;
  std::vector<PerHostStorageInfo> report_pending_;
  base::WeakPtrFactory<QuotaInternalsProxy> weak_factory_{this};
};
}  // namespace quota_internals

#endif  // CHROME_BROWSER_UI_WEBUI_QUOTA_INTERNALS_QUOTA_INTERNALS_PROXY_H_
