// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_URL_INFO_H_
#define CONTENT_BROWSER_URL_INFO_H_

#include "content/browser/web_exposed_isolation_info.h"
#include "content/common/content_export.h"
#include "content/public/browser/storage_partition_config.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace content {

// This struct is used to package a GURL together with extra state required to
// make SiteInstance/process allocation decisions, e.g. whether the url's
// origin or site is requesting isolation as determined by response headers in
// the corresponding NavigationRequest. The extra state is generally most
// relevant when navigation to the URL is in progress, since once placed into a
// SiteInstance, the extra state will be available via SiteInfo. Otherwise,
// most callsites requiring a UrlInfo can create with a GURL, specifying kNone
// for |origin_isolation_request|. Some examples of where passing kNone for
// |origin_isolation_request| is safe are:
// * at DidCommitNavigation time, since at that point the SiteInstance has
//   already been picked and the navigation can be considered finished,
// * before a response is received (the only way to request isolation is via
//   response headers), and
// * outside of a navigation.
//
// If UrlInfo::origin_isolation_request is kNone, that does *not* imply that
// the URL's origin will not be isolated, and vice versa.  The isolation
// decision involves both response headers and consistency within a
// BrowsingInstance, and once we decide on the isolation outcome for an origin,
// it won't change for the lifetime of the BrowsingInstance.
//
// To check whether a frame ends up in a site-isolated process, use
// SiteInfo::RequiresDedicatedProcess() on its SiteInstance's SiteInfo.  To
// check whether a frame ends up being origin-isolated (e.g., due to the
// Origin-Agent-Cluster header), use SiteInfo::is_origin_keyed().
//
// Note: it is not expected that this struct will be exposed in content/public.
class UrlInfoInit;

struct CONTENT_EXPORT UrlInfo {
 public:
  // Bitmask representing one or more isolation requests.
  enum OriginIsolationRequest {
    // No isolated has been requested.
    kNone = 0,
    // The Origin-Agent-Cluster header is requesting origin-keyed isolation for
    // `url`'s origin.
    kOriginAgentCluster = (1 << 0),
    // The Cross-Origin-Opener-Policy header has triggered a hint to turn on
    // site isolation for `url`'s site.
    kCOOP = (1 << 1)
  };

  UrlInfo();  // Needed for inclusion in SiteInstanceDescriptor.
  UrlInfo(const UrlInfo& other);
  explicit UrlInfo(const UrlInfoInit& init);
  ~UrlInfo();

  // Used to convert GURL to UrlInfo in tests where opt-in isolation is not
  // being tested.
  static UrlInfo CreateForTesting(const GURL& url_in,
                                  absl::optional<StoragePartitionConfig>
                                      storage_partition_config = absl::nullopt);

  // Returns whether this UrlInfo is requesting origin-keyed isolation for
  // `url`'s origin due to the OriginAgentCluster header.
  bool requests_origin_agent_cluster_isolation() const {
    return (origin_isolation_request &
            OriginIsolationRequest::kOriginAgentCluster);
  }

  // Returns whether this UrlInfo is requesting isolation in response to the
  // Cross-Origin-Opener-Policy header.
  bool requests_coop_isolation() const {
    return (origin_isolation_request & OriginIsolationRequest::kCOOP);
  }

  GURL url;

  // This field indicates whether the URL is requesting additional process
  // isolation during the current navigation (e.g., via OriginAgentCluster or
  // COOP response headers).  If URL did not request any isolation, this will
  // be set to kNone. This field is only relevant (1) during a navigation
  // request, (2) up to the point where the origin is placed into a
  // SiteInstance.  Other than these cases, this should be set to kNone.
  OriginIsolationRequest origin_isolation_request =
      OriginIsolationRequest::kNone;

  // If |url| represents a resource inside another resource (e.g. a resource
  // with a urn: URL in WebBundle), origin of the original resource. Otherwise,
  // this is just the origin of |url|.
  url::Origin origin;

  // The StoragePartitionConfig that should be used when loading content from
  // |url|. If absent, ContentBrowserClient::GetStoragePartitionConfig will be
  // used to determine which StoragePartitionConfig to use.
  //
  // If present, this value will be used as the StoragePartitionConfig in the
  // SiteInfo, regardless of its validity. SiteInstances created from a UrlInfo
  // containing a StoragePartitionConfig that isn't compatible with the
  // BrowsingInstance that the SiteInstance should belong to will lead to a
  // CHECK failure.
  absl::optional<StoragePartitionConfig> storage_partition_config;

  // Pages may choose to isolate themselves more strongly than the web's
  // default, thus allowing access to APIs that would be difficult to
  // safely expose otherwise. "Cross-origin isolation", for example, requires
  // assertion of a Cross-Origin-Opener-Policy and
  // Cross-Origin-Embedder-Policy, and unlocks SharedArrayBuffer.
  WebExposedIsolationInfo web_exposed_isolation_info;

  // Indicates that the URL directs to PDF content, which should be isolated
  // from other types of content.
  bool is_pdf = false;

  // Any new UrlInfo fields should be added to UrlInfoInit as well, and the
  // UrlInfo constructor that takes a UrlInfoInit should be updated as well.
};

class CONTENT_EXPORT UrlInfoInit {
 public:
  UrlInfoInit() = delete;
  explicit UrlInfoInit(const GURL& url);
  explicit UrlInfoInit(const UrlInfo& base);
  ~UrlInfoInit();

  UrlInfoInit& operator=(const UrlInfoInit&) = delete;

  UrlInfoInit& WithOriginIsolationRequest(
      UrlInfo::OriginIsolationRequest origin_isolation_request);
  UrlInfoInit& WithOrigin(const url::Origin& origin);
  UrlInfoInit& WithStoragePartitionConfig(
      absl::optional<StoragePartitionConfig> storage_partition_config);
  UrlInfoInit& WithWebExposedIsolationInfo(
      const WebExposedIsolationInfo& web_exposed_isolation_info);
  UrlInfoInit& WithIsPdf(bool is_pdf);

 private:
  UrlInfoInit(UrlInfoInit&);

  friend UrlInfo;

  GURL url_;
  UrlInfo::OriginIsolationRequest origin_isolation_request_ =
      UrlInfo::OriginIsolationRequest::kNone;
  url::Origin origin_;
  absl::optional<StoragePartitionConfig> storage_partition_config_;
  WebExposedIsolationInfo web_exposed_isolation_info_;
  bool is_pdf_ = false;

  // Any new fields should be added to the UrlInfoInit(UrlInfo) constructor.
};  // class UrlInfoInit

}  // namespace content

#endif  // CONTENT_BROWSER_URL_INFO_H_
