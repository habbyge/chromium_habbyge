// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_NET_CROSS_ORIGIN_EMBEDDER_POLICY_REPORTER_H_
#define CONTENT_BROWSER_NET_CROSS_ORIGIN_EMBEDDER_POLICY_REPORTER_H_

#include <initializer_list>
#include <string>

#include "base/memory/weak_ptr.h"
#include "base/strings/string_piece.h"
#include "content/common/content_export.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "net/base/network_isolation_key.h"
#include "services/network/public/mojom/cross_origin_embedder_policy.mojom.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/mojom/frame/reporting_observer.mojom.h"
#include "url/gurl.h"

namespace content {

class StoragePartition;

// Used to report (potential) COEP violations.
// A CrossOriginEmbedderPolicyReporter is retained by an object that represents
// a "setting object" in the browser process such as RenderFrameHostImpl and
// DedicatedWorkerHost. They create a mojo endpoint using Clone and pass it
// around. For example, it's sent to the Network Service via
// network.mojom.URLLoaderFactoryParam.coep_reporter.
// Any functions other than the destructor must not be called after the
// associated StoragePartition is destructed.
// TODO(yhirano): This currently only sends reports to the network. Notify
// the event to the associated ReportingObserver.
class CONTENT_EXPORT CrossOriginEmbedderPolicyReporter final
    : public network::mojom::CrossOriginEmbedderPolicyReporter {
 public:
  // This is defined to investigate a crash. Remove this once the investigation
  // is done.
  enum class Creator : uint32_t {
    kDocument = 0xafafafaf,
    kDedicatedWorker = 0xbfbfbfbf,
    kSharedWorker = 0xcfcfcfcf,
    kServiceWorker = 0xdfdfdfdf,
  };

  CrossOriginEmbedderPolicyReporter(
      Creator creator,
      StoragePartition* storage_partition,
      const GURL& context_url,
      const absl::optional<std::string>& endpoint,
      const absl::optional<std::string>& report_only_endpoint,
      const base::UnguessableToken& reporting_source,
      const net::NetworkIsolationKey& network_isolation_key);
  ~CrossOriginEmbedderPolicyReporter() override;
  CrossOriginEmbedderPolicyReporter(const CrossOriginEmbedderPolicyReporter&) =
      delete;
  CrossOriginEmbedderPolicyReporter& operator=(
      const CrossOriginEmbedderPolicyReporter&) = delete;

  void set_reporting_source(const base::UnguessableToken& reporting_source);

  // network::mojom::CrossOriginEmbedderPolicyReporter implementation.
  void QueueCorpViolationReport(const GURL& blocked_url,
                                network::mojom::RequestDestination destination,
                                bool report_only) override;
  void Clone(
      mojo::PendingReceiver<network::mojom::CrossOriginEmbedderPolicyReporter>
          receiver) override;

  void BindObserver(
      mojo::PendingRemote<blink::mojom::ReportingObserver> observer);

  // https://html.spec.whatwg.org/C/#check-a-navigation-response's-adherence-to-its-embedder-policy
  // Queues a violation report for COEP mismatch for nested frame navigation.
  void QueueNavigationReport(const GURL& blocked_url, bool report_only);

  // https://html.spec.whatwg.org/C/#check-a-global-object's-embedder-policy
  // Queues a violation report for COEP mismatch during the worker
  // initialization.
  void QueueWorkerInitializationReport(const GURL& blocked_url,
                                       bool report_only);

  base::WeakPtr<CrossOriginEmbedderPolicyReporter> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

 private:
  void QueueAndNotify(std::initializer_list<
                          std::pair<base::StringPiece, base::StringPiece>> body,
                      bool report_only);

  const Creator creator_;

  // See the class comment.
  StoragePartition* const storage_partition_;

  const GURL context_url_;
  const absl::optional<std::string> endpoint_;
  const absl::optional<std::string> report_only_endpoint_;
  // This reporting source is not owned by COEPReporter in any way. The
  // COEPReporter is not responsible for cleaning up the reporting source, the
  // actual owner of this token needs to manage the lifecycle (including
  // cleaning up the reporting source from reporting cache).
  base::UnguessableToken reporting_source_;
  const net::NetworkIsolationKey network_isolation_key_;

  mojo::ReceiverSet<network::mojom::CrossOriginEmbedderPolicyReporter>
      receiver_set_;
  mojo::Remote<blink::mojom::ReportingObserver> observer_;

  // This must be the last member.
  base::WeakPtrFactory<CrossOriginEmbedderPolicyReporter> weak_ptr_factory_{
      this};
};

}  // namespace content

#endif  // CONTENT_BROWSER_NET_CROSS_ORIGIN_EMBEDDER_POLICY_REPORTER_H_
