// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prefetch/prefetch_proxy/prefetch_proxy_tab_helper.h"

#include <string>

#include "base/barrier_closure.h"
#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/feature_list.h"
#include "base/metrics/histogram.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/rand_util.h"
#include "base/time/time.h"
#include "chrome/browser/chrome_content_browser_client.h"
#include "chrome/browser/navigation_predictor/navigation_predictor_keyed_service_factory.h"
#include "chrome/browser/net/prediction_options.h"
#include "chrome/browser/prefetch/no_state_prefetch/no_state_prefetch_manager_factory.h"
#include "chrome/browser/prefetch/prefetch_proxy/prefetch_proxy_features.h"
#include "chrome/browser/prefetch/prefetch_proxy/prefetch_proxy_network_context_client.h"
#include "chrome/browser/prefetch/prefetch_proxy/prefetch_proxy_origin_decider.h"
#include "chrome/browser/prefetch/prefetch_proxy/prefetch_proxy_params.h"
#include "chrome/browser/prefetch/prefetch_proxy/prefetch_proxy_prefetch_metrics_collector.h"
#include "chrome/browser/prefetch/prefetch_proxy/prefetch_proxy_proxy_configurator.h"
#include "chrome/browser/prefetch/prefetch_proxy/prefetch_proxy_service.h"
#include "chrome/browser/prefetch/prefetch_proxy/prefetch_proxy_service_factory.h"
#include "chrome/browser/prefetch/prefetch_proxy/prefetch_proxy_subresource_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_settings.h"
#include "components/google/core/common/google_util.h"
#include "components/language/core/browser/pref_names.h"
#include "components/no_state_prefetch/browser/no_state_prefetch_manager.h"
#include "components/page_load_metrics/browser/metrics_web_contents_observer.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url_service.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/network_service_instance.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/service_worker_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/user_agent.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "net/base/isolation_info.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/cookies/cookie_store.h"
#include "net/http/http_status_code.h"
#include "net/http/http_util.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/cert_verifier/public/mojom/cert_verifier_service_factory.mojom.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/cpp/wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/network_service.mojom.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "url/origin.h"

namespace {

absl::optional<base::TimeDelta> GetTotalPrefetchTime(
    network::mojom::URLResponseHead* head) {
  DCHECK(head);

  base::Time start = head->request_time;
  base::Time end = head->response_time;

  if (start.is_null() || end.is_null())
    return absl::nullopt;

  return end - start;
}

absl::optional<base::TimeDelta> GetPrefetchConnectTime(
    network::mojom::URLResponseHead* head) {
  DCHECK(head);

  base::TimeTicks start = head->load_timing.connect_timing.connect_start;
  base::TimeTicks end = head->load_timing.connect_timing.connect_end;

  if (start.is_null() || end.is_null())
    return absl::nullopt;

  return end - start;
}

void InformPLMOfLikelyPrefetching(content::WebContents* web_contents) {
  page_load_metrics::MetricsWebContentsObserver* metrics_web_contents_observer =
      page_load_metrics::MetricsWebContentsObserver::FromWebContents(
          web_contents);
  if (!metrics_web_contents_observer)
    return;

  metrics_web_contents_observer->OnPrefetchLikely();
}

void OnGotCookieList(
    const GURL& url,
    PrefetchProxyTabHelper::OnEligibilityResultCallback result_callback,
    const net::CookieAccessResultList& cookie_list,
    const net::CookieAccessResultList& excluded_cookies) {
  if (!cookie_list.empty()) {
    std::move(result_callback)
        .Run(url, false,
             PrefetchProxyPrefetchStatus::kPrefetchNotEligibleUserHasCookies);
    return;
  }

  // Cookies are tricky because cookies for different paths or a higher level
  // domain (e.g.: m.foo.com and foo.com) may not show up in |cookie_list|, but
  // they will show up in |excluded_cookies|. To check for any cookies for a
  // domain, compare the domains of the prefetched |url| and the domains of all
  // the returned cookies.
  bool excluded_cookie_has_tld = false;
  for (const auto& cookie_result : excluded_cookies) {
    if (cookie_result.cookie.IsExpired(base::Time::Now())) {
      // Expired cookies don't count.
      continue;
    }

    if (url.DomainIs(cookie_result.cookie.DomainWithoutDot())) {
      excluded_cookie_has_tld = true;
      break;
    }
  }

  if (excluded_cookie_has_tld) {
    std::move(result_callback)
        .Run(url, false,
             PrefetchProxyPrefetchStatus::kPrefetchNotEligibleUserHasCookies);
    return;
  }

  std::move(result_callback).Run(url, true, absl::nullopt);
}

void CookieSetHelper(base::RepeatingClosure run_me,
                     net::CookieAccessResult access_result) {
  run_me.Run();
}

bool ShouldStartSpareRenderer() {
  if (!PrefetchProxyStartsSpareRenderer()) {
    return false;
  }

  for (content::RenderProcessHost::iterator iter(
           content::RenderProcessHost::AllHostsIterator());
       !iter.IsAtEnd(); iter.Advance()) {
    if (iter.GetCurrentValue()->IsUnused()) {
      // There is already a spare renderer.
      return false;
    }
  }

  return true;
}

bool ShouldConsiderDecoyRequestForStatus(PrefetchProxyPrefetchStatus status) {
  switch (status) {
    case PrefetchProxyPrefetchStatus::kPrefetchNotEligibleUserHasCookies:
    case PrefetchProxyPrefetchStatus::kPrefetchNotEligibleUserHasServiceWorker:
      // If the prefetch is not eligible because of cookie or a service worker,
      // then maybe send a decoy.
      return true;
    case PrefetchProxyPrefetchStatus::kPrefetchNotEligibleGoogleDomain:
    case PrefetchProxyPrefetchStatus::kPrefetchNotEligibleSchemeIsNotHttps:
    case PrefetchProxyPrefetchStatus::kPrefetchNotEligibleHostIsIPAddress:
    case PrefetchProxyPrefetchStatus::
        kPrefetchNotEligibleNonDefaultStoragePartition:
    case PrefetchProxyPrefetchStatus::kPrefetchPositionIneligible:
    case PrefetchProxyPrefetchStatus::kPrefetchIneligibleRetryAfter:
    case PrefetchProxyPrefetchStatus::kPrefetchProxyNotAvailable:
      // These statuses don't relate to any user state, so don't send a decoy
      // request.
      return false;
    case PrefetchProxyPrefetchStatus::kPrefetchUsedNoProbe:
    case PrefetchProxyPrefetchStatus::kPrefetchUsedProbeSuccess:
    case PrefetchProxyPrefetchStatus::kPrefetchNotUsedProbeFailed:
    case PrefetchProxyPrefetchStatus::kPrefetchNotStarted:
    case PrefetchProxyPrefetchStatus::kPrefetchNotFinishedInTime:
    case PrefetchProxyPrefetchStatus::kPrefetchFailedNetError:
    case PrefetchProxyPrefetchStatus::kPrefetchFailedNon2XX:
    case PrefetchProxyPrefetchStatus::kPrefetchFailedNotHTML:
    case PrefetchProxyPrefetchStatus::kPrefetchSuccessful:
    case PrefetchProxyPrefetchStatus::kNavigatedToLinkNotOnSRP:
    case PrefetchProxyPrefetchStatus::kSubresourceThrottled:
    case PrefetchProxyPrefetchStatus::kPrefetchUsedNoProbeWithNSP:
    case PrefetchProxyPrefetchStatus::kPrefetchUsedProbeSuccessWithNSP:
    case PrefetchProxyPrefetchStatus::kPrefetchNotUsedProbeFailedWithNSP:
    case PrefetchProxyPrefetchStatus::kPrefetchUsedNoProbeNSPAttemptDenied:
    case PrefetchProxyPrefetchStatus::kPrefetchUsedProbeSuccessNSPAttemptDenied:
    case PrefetchProxyPrefetchStatus::
        kPrefetchNotUsedProbeFailedNSPAttemptDenied:
    case PrefetchProxyPrefetchStatus::kPrefetchUsedNoProbeNSPNotStarted:
    case PrefetchProxyPrefetchStatus::kPrefetchUsedProbeSuccessNSPNotStarted:
    case PrefetchProxyPrefetchStatus::kPrefetchNotUsedProbeFailedNSPNotStarted:
    case PrefetchProxyPrefetchStatus::kPrefetchIsPrivacyDecoy:
    case PrefetchProxyPrefetchStatus::kPrefetchIsStale:
    case PrefetchProxyPrefetchStatus::kPrefetchIsStaleWithNSP:
    case PrefetchProxyPrefetchStatus::kPrefetchIsStaleNSPAttemptDenied:
    case PrefetchProxyPrefetchStatus::kPrefetchIsStaleNSPNotStarted:
    case PrefetchProxyPrefetchStatus::kPrefetchNotUsedCookiesChanged:
    case PrefetchProxyPrefetchStatus::kPrefetchFailedRedirectsDisabled:
      // These statuses should not be returned by the eligibility checks, and
      // thus not be passed in here.
      NOTREACHED();
      return false;
  }
}

}  // namespace

PrefetchProxyTabHelper::PrefetchMetrics::PrefetchMetrics() = default;
PrefetchProxyTabHelper::PrefetchMetrics::~PrefetchMetrics() = default;

PrefetchProxyTabHelper::AfterSRPMetrics::AfterSRPMetrics() = default;
PrefetchProxyTabHelper::AfterSRPMetrics::AfterSRPMetrics(
    const AfterSRPMetrics& other) = default;
PrefetchProxyTabHelper::AfterSRPMetrics::~AfterSRPMetrics() = default;

PrefetchProxyTabHelper::CurrentPageLoad::CurrentPageLoad(
    content::NavigationHandle* handle)
    : profile_(handle ? Profile::FromBrowserContext(
                            handle->GetWebContents()->GetBrowserContext())
                      : nullptr),
      navigation_start_(handle ? handle->NavigationStart() : base::TimeTicks()),
      srp_metrics_(
          base::MakeRefCounted<PrefetchProxyTabHelper::PrefetchMetrics>()) {}

PrefetchProxyTabHelper::CurrentPageLoad::~CurrentPageLoad() {
  if (PrefetchProxyStartsSpareRenderer()) {
    UMA_HISTOGRAM_COUNTS_100("PrefetchProxy.SpareRenderer.CountStartedOnSRP",
                             number_of_spare_renderers_started_);
  }

  if (!profile_)
    return;

  PrefetchProxyService* service =
      PrefetchProxyServiceFactory::GetForProfile(profile_);
  if (!service) {
    return;
  }

  for (const GURL& url : no_state_prefetched_urls_) {
    service->DestroySubresourceManagerForURL(url);
  }
  for (const GURL& url : urls_to_no_state_prefetch_) {
    service->DestroySubresourceManagerForURL(url);
  }
}

static content::ServiceWorkerContext* g_service_worker_context_for_test =
    nullptr;

// static
void PrefetchProxyTabHelper::SetServiceWorkerContextForTest(
    content::ServiceWorkerContext* context) {
  g_service_worker_context_for_test = context;
}

PrefetchProxyTabHelper::PrefetchProxyTabHelper(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents) {
  page_ = std::make_unique<CurrentPageLoad>(nullptr);
  profile_ = Profile::FromBrowserContext(web_contents->GetBrowserContext());

  NavigationPredictorKeyedService* navigation_predictor_service =
      NavigationPredictorKeyedServiceFactory::GetForProfile(profile_);
  if (navigation_predictor_service) {
    navigation_predictor_service->AddObserver(this);
  }

  // Make sure the global service is up and running so that the service worker
  // registrations can be queried before the first navigation prediction.
  PrefetchProxyServiceFactory::GetForProfile(profile_);
}

PrefetchProxyTabHelper::~PrefetchProxyTabHelper() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  NavigationPredictorKeyedService* navigation_predictor_service =
      NavigationPredictorKeyedServiceFactory::GetForProfile(profile_);
  if (navigation_predictor_service) {
    navigation_predictor_service->RemoveObserver(this);
  }
}

void PrefetchProxyTabHelper::AddObserverForTesting(Observer* observer) {
  observer_list_.AddObserver(observer);
}

void PrefetchProxyTabHelper::RemoveObserverForTesting(Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

network::mojom::NetworkContext*
PrefetchProxyTabHelper::GetIsolatedContextForTesting() const {
  return page_->isolated_network_context_.get();
}

absl::optional<PrefetchProxyTabHelper::AfterSRPMetrics>
PrefetchProxyTabHelper::after_srp_metrics() const {
  if (page_->after_srp_metrics_) {
    return *(page_->after_srp_metrics_);
  }
  return absl::nullopt;
}

// static
bool PrefetchProxyTabHelper::IsProfileEligible(Profile* profile) {
  if (profile->IsOffTheRecord()) {
    return false;
  }

  if (PrefetchProxyOnlyForLiteMode()) {
    return data_reduction_proxy::DataReductionProxySettings::
        IsDataSaverEnabledByUser(profile->IsOffTheRecord(),
                                 profile->GetPrefs());
  }
  return true;
}

bool PrefetchProxyTabHelper::IsProfileEligible() const {
  return IsProfileEligible(profile_);
}

void PrefetchProxyTabHelper::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!navigation_handle->IsInPrimaryMainFrame()) {
    return;
  }

  // This check is only relevant for detecting AMP pages. For this feature, AMP
  // pages won't get sped up any so just ignore them.
  if (navigation_handle->IsSameDocument()) {
    return;
  }

  // Don't take any actions during a prerender since it was probably triggered
  // by another instance of this class and we don't want to interfere.
  prerender::NoStatePrefetchManager* no_state_prefetch_manager =
      prerender::NoStatePrefetchManagerFactory::GetForBrowserContext(profile_);
  if (no_state_prefetch_manager &&
      no_state_prefetch_manager->IsWebContentsPrerendering(web_contents())) {
    return;
  }

  // User is navigating, don't bother prefetching further.
  page_->url_loaders_.clear();

  if (page_->srp_metrics_->prefetch_attempted_count_ > 0) {
    UMA_HISTOGRAM_COUNTS_100(
        "PrefetchProxy.Prefetch.Mainframe.TotalRedirects",
        page_->srp_metrics_->prefetch_total_redirect_count_);
  }

  const GURL& url = navigation_handle->GetURL();

  // If the cookies associated with |url| have changed since the initial
  // eligibility check, then we shouldn't serve prefetched resources.
  if (HaveCookiesChanged(url)) {
    OnPrefetchStatusUpdate(
        url, PrefetchProxyPrefetchStatus::kPrefetchNotUsedCookiesChanged);
    return;
  }

  PrepareToServe(url);
}

void PrefetchProxyTabHelper::PrepareToServe(const GURL& url) {
  // TODO(https://crbug.com/1238926): At this point in the navigation it's not
  // guaranteed that we serve the prefetch, so consider moving the cookies to
  // the interception path for robustness.
  if (page_->prefetched_responses_.find(url) !=
      page_->prefetched_responses_.end()) {
    // Content older than 5 minutes should not be served.
    auto time_it = page_->prefetch_start_times_.find(url);
    if (time_it != page_->prefetch_start_times_.end() &&
        base::TimeTicks::Now() <
            time_it->second + PrefetchProxyCacheableDuration()) {
      // Start copying any needed cookies over to the main profile if this page
      // was prefetched.
      CopyIsolatedCookiesOnAfterSRPClick(url);
    }
  }

  // Notify the subresource manager (if applicable)  that its page is being
  // navigated to so that the prefetched subresources can be used from cache.
  PrefetchProxyService* service =
      PrefetchProxyServiceFactory::GetForProfile(profile_);
  if (!service)
    return;

  PrefetchProxySubresourceManager* subresource_manager =
      service->GetSubresourceManagerForURL(url);
  if (!subresource_manager)
    return;

  subresource_manager->NotifyPageNavigatedToAfterSRP();
}

void PrefetchProxyTabHelper::NotifyPrefetchProbeLatency(
    base::TimeDelta probe_latency) {
  page_->probe_latency_ = probe_latency;
}

void PrefetchProxyTabHelper::ReportProbeResult(
    const GURL& url,
    PrefetchProxyProbeResult result) {
  if (!page_->prefetch_metrics_collector_) {
    return;
  }
  page_->prefetch_metrics_collector_->OnMainframeNavigationProbeResult(url,
                                                                       result);
}

void PrefetchProxyTabHelper::OnPrefetchStatusUpdate(
    const GURL& url,
    PrefetchProxyPrefetchStatus usage) {
  page_->prefetch_status_by_url_[url] = usage;
}

PrefetchProxyPrefetchStatus
PrefetchProxyTabHelper::MaybeUpdatePrefetchStatusWithNSPContext(
    const GURL& url,
    PrefetchProxyPrefetchStatus status) const {
  switch (status) {
    // These are the statuses we want to update.
    case PrefetchProxyPrefetchStatus::kPrefetchUsedNoProbe:
    case PrefetchProxyPrefetchStatus::kPrefetchUsedProbeSuccess:
    case PrefetchProxyPrefetchStatus::kPrefetchNotUsedProbeFailed:
    case PrefetchProxyPrefetchStatus::kPrefetchIsStale:
      break;
    // These statuses are not applicable since the prefetch was not used after
    // the click.
    case PrefetchProxyPrefetchStatus::kPrefetchNotStarted:
    case PrefetchProxyPrefetchStatus::kPrefetchNotEligibleGoogleDomain:
    case PrefetchProxyPrefetchStatus::kPrefetchNotEligibleUserHasCookies:
    case PrefetchProxyPrefetchStatus::kPrefetchNotEligibleUserHasServiceWorker:
    case PrefetchProxyPrefetchStatus::kPrefetchNotEligibleSchemeIsNotHttps:
    case PrefetchProxyPrefetchStatus::kPrefetchNotEligibleHostIsIPAddress:
    case PrefetchProxyPrefetchStatus::
        kPrefetchNotEligibleNonDefaultStoragePartition:
    case PrefetchProxyPrefetchStatus::kPrefetchNotFinishedInTime:
    case PrefetchProxyPrefetchStatus::kPrefetchFailedNetError:
    case PrefetchProxyPrefetchStatus::kPrefetchFailedNon2XX:
    case PrefetchProxyPrefetchStatus::kPrefetchFailedNotHTML:
    case PrefetchProxyPrefetchStatus::kPrefetchSuccessful:
    case PrefetchProxyPrefetchStatus::kNavigatedToLinkNotOnSRP:
    case PrefetchProxyPrefetchStatus::kSubresourceThrottled:
    case PrefetchProxyPrefetchStatus::kPrefetchPositionIneligible:
    case PrefetchProxyPrefetchStatus::kPrefetchIneligibleRetryAfter:
    case PrefetchProxyPrefetchStatus::kPrefetchProxyNotAvailable:
    case PrefetchProxyPrefetchStatus::kPrefetchIsPrivacyDecoy:
    case PrefetchProxyPrefetchStatus::kPrefetchNotUsedCookiesChanged:
    case PrefetchProxyPrefetchStatus::kPrefetchFailedRedirectsDisabled:
      return status;
    // These statuses we are going to update to, and this is the only place that
    // they are set so they are not expected to be passed in.
    case PrefetchProxyPrefetchStatus::kPrefetchUsedNoProbeWithNSP:
    case PrefetchProxyPrefetchStatus::kPrefetchUsedProbeSuccessWithNSP:
    case PrefetchProxyPrefetchStatus::kPrefetchNotUsedProbeFailedWithNSP:
    case PrefetchProxyPrefetchStatus::kPrefetchUsedNoProbeNSPAttemptDenied:
    case PrefetchProxyPrefetchStatus::kPrefetchUsedProbeSuccessNSPAttemptDenied:
    case PrefetchProxyPrefetchStatus::
        kPrefetchNotUsedProbeFailedNSPAttemptDenied:
    case PrefetchProxyPrefetchStatus::kPrefetchUsedNoProbeNSPNotStarted:
    case PrefetchProxyPrefetchStatus::kPrefetchUsedProbeSuccessNSPNotStarted:
    case PrefetchProxyPrefetchStatus::kPrefetchNotUsedProbeFailedNSPNotStarted:
    case PrefetchProxyPrefetchStatus::kPrefetchIsStaleWithNSP:
    case PrefetchProxyPrefetchStatus::kPrefetchIsStaleNSPAttemptDenied:
    case PrefetchProxyPrefetchStatus::kPrefetchIsStaleNSPNotStarted:
      NOTREACHED();
      return status;
  }

  bool no_state_prefetch_not_started =
      base::Contains(page_->urls_to_no_state_prefetch_, url);

  bool no_state_prefetch_complete =
      base::Contains(page_->no_state_prefetched_urls_, url);

  bool no_state_prefetch_failed =
      base::Contains(page_->failed_no_state_prefetch_urls_, url);

  if (!no_state_prefetch_not_started && !no_state_prefetch_complete &&
      !no_state_prefetch_failed) {
    return status;
  }

  // At most one of those bools should be true.
  DCHECK(no_state_prefetch_not_started ^ no_state_prefetch_complete ^
         no_state_prefetch_failed);

  if (no_state_prefetch_complete) {
    switch (status) {
      case PrefetchProxyPrefetchStatus::kPrefetchUsedNoProbe:
        return PrefetchProxyPrefetchStatus::kPrefetchUsedNoProbeWithNSP;
      case PrefetchProxyPrefetchStatus::kPrefetchUsedProbeSuccess:
        return PrefetchProxyPrefetchStatus::kPrefetchUsedProbeSuccessWithNSP;
      case PrefetchProxyPrefetchStatus::kPrefetchNotUsedProbeFailed:
        return PrefetchProxyPrefetchStatus::kPrefetchNotUsedProbeFailedWithNSP;
      case PrefetchProxyPrefetchStatus::kPrefetchIsStale:
        return PrefetchProxyPrefetchStatus::kPrefetchIsStaleWithNSP;
      default:
        break;
    }
  }

  if (no_state_prefetch_failed) {
    switch (status) {
      case PrefetchProxyPrefetchStatus::kPrefetchUsedNoProbe:
        return PrefetchProxyPrefetchStatus::
            kPrefetchUsedNoProbeNSPAttemptDenied;
      case PrefetchProxyPrefetchStatus::kPrefetchUsedProbeSuccess:
        return PrefetchProxyPrefetchStatus::
            kPrefetchUsedProbeSuccessNSPAttemptDenied;
      case PrefetchProxyPrefetchStatus::kPrefetchNotUsedProbeFailed:
        return PrefetchProxyPrefetchStatus::
            kPrefetchNotUsedProbeFailedNSPAttemptDenied;
      case PrefetchProxyPrefetchStatus::kPrefetchIsStale:
        return PrefetchProxyPrefetchStatus::kPrefetchIsStaleNSPAttemptDenied;
      default:
        break;
    }
  }

  if (no_state_prefetch_not_started) {
    switch (status) {
      case PrefetchProxyPrefetchStatus::kPrefetchUsedNoProbe:
        return PrefetchProxyPrefetchStatus::kPrefetchUsedNoProbeNSPNotStarted;
      case PrefetchProxyPrefetchStatus::kPrefetchUsedProbeSuccess:
        return PrefetchProxyPrefetchStatus::
            kPrefetchUsedProbeSuccessNSPNotStarted;
      case PrefetchProxyPrefetchStatus::kPrefetchNotUsedProbeFailed:
        return PrefetchProxyPrefetchStatus::
            kPrefetchNotUsedProbeFailedNSPNotStarted;
      case PrefetchProxyPrefetchStatus::kPrefetchIsStale:
        return PrefetchProxyPrefetchStatus::kPrefetchIsStaleNSPNotStarted;
      default:
        break;
    }
  }

  NOTREACHED();
  return status;
}

std::unique_ptr<PrefetchProxyTabHelper::AfterSRPMetrics>
PrefetchProxyTabHelper::ComputeAfterSRPMetricsBeforeCommit(
    content::NavigationHandle* handle) const {
  if (page_->srp_metrics_->predicted_urls_count_ <= 0) {
    return nullptr;
  }

  auto metrics = std::make_unique<AfterSRPMetrics>();

  metrics->url_ = handle->GetURL();
  metrics->prefetch_eligible_count_ =
      page_->srp_metrics_->prefetch_eligible_count_;

  metrics->probe_latency_ = page_->probe_latency_;

  // Check every url in the redirect chain for a status, starting at the end
  // and working backwards. Note: When a redirect chain is eligible all the
  // way to the end, the status is already propagated. But if a redirect was
  // not eligible then this will find its last known status.
  DCHECK(!handle->GetRedirectChain().empty());
  absl::optional<PrefetchProxyPrefetchStatus> status;
  absl::optional<size_t> prediction_position;
  for (auto back_iter = handle->GetRedirectChain().rbegin();
       back_iter != handle->GetRedirectChain().rend(); ++back_iter) {
    GURL chain_url = *back_iter;
    auto status_iter = page_->prefetch_status_by_url_.find(chain_url);
    if (!status && status_iter != page_->prefetch_status_by_url_.end()) {
      status = MaybeUpdatePrefetchStatusWithNSPContext(chain_url,
                                                       status_iter->second);
    }

    // Same check for the original prediction ordering.
    auto position_iter = page_->original_prediction_ordering_.find(chain_url);
    if (!prediction_position &&
        position_iter != page_->original_prediction_ordering_.end()) {
      prediction_position = position_iter->second;
    }
  }

  if (status) {
    metrics->prefetch_status_ = *status;
  } else {
    metrics->prefetch_status_ =
        PrefetchProxyPrefetchStatus::kNavigatedToLinkNotOnSRP;
  }
  metrics->clicked_link_srp_position_ = prediction_position;

  return metrics;
}

void PrefetchProxyTabHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!navigation_handle->IsInPrimaryMainFrame()) {
    return;
  }

  // This check is only relevant for detecting AMP pages. For this feature, AMP
  // pages won't get sped up any so just ignore them.
  if (navigation_handle->IsSameDocument()) {
    return;
  }

  if (!navigation_handle->HasCommitted()) {
    return;
  }

  // Don't take any actions during a prerender since it was probably triggered
  // by another instance of this class and we don't want to interfere.
  prerender::NoStatePrefetchManager* no_state_prefetch_manager =
      prerender::NoStatePrefetchManagerFactory::GetForBrowserContext(profile_);
  if (no_state_prefetch_manager &&
      no_state_prefetch_manager->IsWebContentsPrerendering(web_contents())) {
    return;
  }

  // Ensure there's no ongoing prefetches.
  page_->url_loaders_.clear();

  GURL url = navigation_handle->GetURL();

  std::unique_ptr<CurrentPageLoad> new_page =
      std::make_unique<CurrentPageLoad>(navigation_handle);

  if (page_->srp_metrics_->predicted_urls_count_ > 0) {
    page_->prefetch_metrics_collector_->OnMainframeNavigatedTo(url);

    // If the previous page load was a Google SRP, the AfterSRPMetrics class
    // needs to be created now from the SRP's |page_| and then set on the new
    // one when we set it at the end of this method.
    new_page->after_srp_metrics_ =
        ComputeAfterSRPMetricsBeforeCommit(navigation_handle);

    // See if the page being navigated to was prerendered. If so, copy over its
    // subresource manager and networking pipes.
    PrefetchProxyService* service =
        PrefetchProxyServiceFactory::GetForProfile(profile_);
    std::unique_ptr<PrefetchProxySubresourceManager> manager =
        service->TakeSubresourceManagerForURL(url);
    if (manager) {
      new_page->subresource_manager_ = std::move(manager);
      new_page->isolated_cookie_manager_ =
          std::move(page_->isolated_cookie_manager_);
      new_page->isolated_url_loader_factory_ =
          std::move(page_->isolated_url_loader_factory_);
      new_page->isolated_network_context_ =
          std::move(page_->isolated_network_context_);
    }
  }

  // |page_| is reset on commit so that any available cached prefetches that
  // result from a redirect get used.
  page_ = std::move(new_page);
}

void PrefetchProxyTabHelper::OnVisibilityChanged(
    content::Visibility visibility) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!PrefetchProxyIsEnabled()) {
    return;
  }

  // Start prefetching if the tab has become visible and prefetching is
  // inactive. Hidden and occluded visibility is ignored here so that pending
  // prefetches can finish.
  if (visibility == content::Visibility::VISIBLE && !PrefetchingActive())
    Prefetch();
}

std::unique_ptr<PrefetchedMainframeResponseContainer>
PrefetchProxyTabHelper::TakePrefetchResponse(const GURL& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = page_->prefetched_responses_.find(url);
  if (it == page_->prefetched_responses_.end())
    return nullptr;

  // Content older than 5 minutes should not be served.
  auto time_it = page_->prefetch_start_times_.find(url);
  if (time_it == page_->prefetch_start_times_.end() ||
      base::TimeTicks::Now() >
          time_it->second + PrefetchProxyCacheableDuration()) {
    OnPrefetchStatusUpdate(url, PrefetchProxyPrefetchStatus::kPrefetchIsStale);
    return nullptr;
  }
  std::unique_ptr<PrefetchedMainframeResponseContainer> response =
      std::move(it->second);
  page_->prefetched_responses_.erase(it);
  page_->prefetch_start_times_.erase(time_it);
  return response;
}

std::unique_ptr<PrefetchedMainframeResponseContainer>
PrefetchProxyTabHelper::CopyPrefetchResponseForNSP(const GURL& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = page_->prefetched_responses_.find(url);
  if (it == page_->prefetched_responses_.end())
    return nullptr;

  return it->second->Clone();
}

bool PrefetchProxyTabHelper::PrefetchingActive() const {
  return page_ && !page_->url_loaders_.empty();
}

void PrefetchProxyTabHelper::Prefetch() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(PrefetchProxyIsEnabled());

  if (!page_->srp_metrics_->navigation_to_prefetch_start_.has_value()) {
    page_->srp_metrics_->navigation_to_prefetch_start_ =
        base::TimeTicks::Now() - page_->navigation_start_;
    DCHECK_GT(page_->srp_metrics_->navigation_to_prefetch_start_.value(),
              base::TimeDelta());
  }

  if (PrefetchProxyCloseIdleSockets() && page_->isolated_network_context_) {
    page_->isolated_network_context_->CloseIdleConnections(base::DoNothing());
  }

  if (web_contents()->GetVisibility() != content::Visibility::VISIBLE) {
    // |OnVisibilityChanged| will restart prefetching when the tab becomes
    // visible again.
    return;
  }

  DCHECK_GT(PrefetchProxyMaximumNumberOfConcurrentPrefetches(), 0U);

  while (
      // Checks that the total number of prefetches has not been met.
      !(PrefetchProxyMaximumNumberOfPrefetches().has_value() &&
        page_->decoy_requests_attempted_ +
                page_->srp_metrics_->prefetch_attempted_count_ >=
            PrefetchProxyMaximumNumberOfPrefetches().value()) &&

      // Checks that there are still urls to prefetch.
      !page_->urls_to_prefetch_.empty() &&

      // Checks that the max number of concurrent prefetches has not been met.
      page_->url_loaders_.size() <
          PrefetchProxyMaximumNumberOfConcurrentPrefetches()) {
    StartSinglePrefetch();
  }
}

void PrefetchProxyTabHelper::StartSinglePrefetch() {
  DCHECK(!page_->urls_to_prefetch_.empty());
  DCHECK(!(PrefetchProxyMaximumNumberOfPrefetches().has_value() &&
           page_->decoy_requests_attempted_ +
                   page_->srp_metrics_->prefetch_attempted_count_ >=
               PrefetchProxyMaximumNumberOfPrefetches().value()));
  DCHECK(page_->url_loaders_.size() <
         PrefetchProxyMaximumNumberOfConcurrentPrefetches());

  GURL url = page_->urls_to_prefetch_[0];
  page_->urls_to_prefetch_.erase(page_->urls_to_prefetch_.begin());

  // Only update these metrics on normal prefetches.
  if (page_->decoy_urls_.find(url) == page_->decoy_urls_.end()) {
    page_->srp_metrics_->prefetch_attempted_count_++;
    // The status is updated to be successful or failed when it finishes.
    OnPrefetchStatusUpdate(
        url, PrefetchProxyPrefetchStatus::kPrefetchNotFinishedInTime);
  } else {
    page_->decoy_requests_attempted_++;
    OnPrefetchStatusUpdate(
        url, PrefetchProxyPrefetchStatus::kPrefetchIsPrivacyDecoy);
  }

  url::Origin origin = url::Origin::Create(url);
  net::IsolationInfo isolation_info = net::IsolationInfo::Create(
      net::IsolationInfo::RequestType::kMainFrame, origin, origin,
      net::SiteForCookies::FromOrigin(origin));
  network::ResourceRequest::TrustedParams trusted_params;
  trusted_params.isolation_info = isolation_info;

  std::unique_ptr<network::ResourceRequest> request =
      std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "GET";
  request->enable_load_timing = true;
  request->load_flags = net::LOAD_DISABLE_CACHE | net::LOAD_PREFETCH;
  request->credentials_mode = network::mojom::CredentialsMode::kInclude;
  request->headers.SetHeader(content::kCorsExemptPurposeHeaderName, "prefetch");
  // Remove the user agent header if it was set so that the network context's
  // default is used.
  request->headers.RemoveHeader("User-Agent");
  request->trusted_params = trusted_params;
  request->site_for_cookies = trusted_params.isolation_info.site_for_cookies();

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("navigation_predictor_srp_prefetch",
                                          R"(
          semantics {
            sender: "Navigation Predictor SRP Prefetch Loader"
            description:
              "Prefetches the mainframe HTML of a page linked from a Google "
              "Search Result Page (SRP). This is done out-of-band of normal "
              "prefetches to allow total isolation of this request from the "
              "rest of browser traffic and user state like cookies and cache."
            trigger:
              "Used for sites off of Google SRPs (Search Result Pages) only "
              "for Lite mode users when the feature is enabled."
            data: "None."
            destination: WEBSITE
          }
          policy {
            cookies_allowed: NO
            setting:
              "Users can control Lite mode on Android via the settings menu. "
              "Lite mode is not available on iOS, and on desktop only for "
              "developer testing."
            policy_exception_justification: "Not implemented."
        })");

  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(request), traffic_annotation);

  // base::Unretained is safe because |loader| is owned by |this|.
  loader->SetOnRedirectCallback(
      base::BindRepeating(&PrefetchProxyTabHelper::OnPrefetchRedirect,
                          base::Unretained(this), loader.get(), url));
  loader->SetAllowHttpErrorResults(true);
  loader->SetTimeoutDuration(PrefetchProxyTimeoutDuration());
  loader->DownloadToString(
      GetURLLoaderFactory(),
      base::BindOnce(&PrefetchProxyTabHelper::OnPrefetchComplete,
                     base::Unretained(this), loader.get(), url, isolation_info),
      PrefetchProxyMainframeBodyLengthLimit());

  page_->url_loaders_.emplace(std::move(loader));

  // Start a spare renderer now so that it will be ready by the time it is
  // useful to have.
  if (ShouldStartSpareRenderer()) {
    StartSpareRenderer();
  }
}

void PrefetchProxyTabHelper::OnPrefetchRedirect(
    network::SimpleURLLoader* loader,
    const GURL& original_url,
    const net::RedirectInfo& redirect_info,
    const network::mojom::URLResponseHead& response_head,
    std::vector<std::string>* removed_headers) {
  DCHECK(PrefetchingActive());

  // Currently all redirects are disabled when using the prefetch proxy. See
  // crbug.com/1266876 for more details.
  OnPrefetchStatusUpdate(
      original_url,
      PrefetchProxyPrefetchStatus::kPrefetchFailedRedirectsDisabled);

  // Cancels the current request.
  DCHECK(page_->url_loaders_.find(loader) != page_->url_loaders_.end());
  page_->url_loaders_.erase(page_->url_loaders_.find(loader));

  // Continue prefetching other urls.
  Prefetch();
}

void PrefetchProxyTabHelper::OnPrefetchComplete(
    network::SimpleURLLoader* loader,
    const GURL& url,
    const net::IsolationInfo& isolation_info,
    std::unique_ptr<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(PrefetchingActive());

  if (page_->decoy_urls_.find(url) != page_->decoy_urls_.end()) {
    if (loader->CompletionStatus()) {
      page_->prefetch_metrics_collector_->OnDecoyPrefetchComplete(
          url, page_->original_prediction_ordering_.find(url)->second,
          loader->ResponseInfo() ? loader->ResponseInfo()->Clone() : nullptr,
          loader->CompletionStatus().value());
    }

    for (auto& observer : observer_list_) {
      observer.OnDecoyPrefetchCompleted(url);
    }

    // Do nothing with the response, i.e.: don't cache it.

    // Cancels the current request.
    DCHECK(page_->url_loaders_.find(loader) != page_->url_loaders_.end());
    page_->url_loaders_.erase(page_->url_loaders_.find(loader));

    Prefetch();
    return;
  }

  base::UmaHistogramSparse("PrefetchProxy.Prefetch.Mainframe.NetError",
                           std::abs(loader->NetError()));

  if (loader->CompletionStatus()) {
    page_->prefetch_metrics_collector_->OnMainframeResourcePrefetched(
        url, page_->original_prediction_ordering_.find(url)->second,
        loader->ResponseInfo() ? loader->ResponseInfo()->Clone() : nullptr,
        loader->CompletionStatus().value());
  }

  if (loader->NetError() != net::OK) {
    OnPrefetchStatusUpdate(
        url, PrefetchProxyPrefetchStatus::kPrefetchFailedNetError);

    for (auto& observer : observer_list_) {
      observer.OnPrefetchCompletedWithError(url, loader->NetError());
    }
  }

  if (loader->NetError() == net::OK && body && loader->ResponseInfo()) {
    network::mojom::URLResponseHeadPtr head = loader->ResponseInfo()->Clone();

    DCHECK(!head->proxy_server.is_direct());

    HandlePrefetchResponse(url, isolation_info, std::move(head),
                           std::move(body));
  }

  DCHECK(page_->url_loaders_.find(loader) != page_->url_loaders_.end());
  page_->url_loaders_.erase(page_->url_loaders_.find(loader));

  Prefetch();
}

void PrefetchProxyTabHelper::HandlePrefetchResponse(
    const GURL& url,
    const net::IsolationInfo& isolation_info,
    network::mojom::URLResponseHeadPtr head,
    std::unique_ptr<std::string> body) {
  DCHECK(!head->was_fetched_via_cache);

  if (!head->headers)
    return;

  UMA_HISTOGRAM_COUNTS_10M("PrefetchProxy.Prefetch.Mainframe.BodyLength",
                           body->size());

  absl::optional<base::TimeDelta> total_time = GetTotalPrefetchTime(head.get());
  if (total_time) {
    UMA_HISTOGRAM_CUSTOM_TIMES("PrefetchProxy.Prefetch.Mainframe.TotalTime",
                               *total_time, base::Milliseconds(10),
                               base::Seconds(30), 100);
  }

  absl::optional<base::TimeDelta> connect_time =
      GetPrefetchConnectTime(head.get());
  if (connect_time) {
    UMA_HISTOGRAM_TIMES("PrefetchProxy.Prefetch.Mainframe.ConnectTime",
                        *connect_time);
  }

  int response_code = head->headers->response_code();

  base::UmaHistogramSparse("PrefetchProxy.Prefetch.Mainframe.RespCode",
                           response_code);

  if (response_code < 200 || response_code >= 300) {
    OnPrefetchStatusUpdate(url,
                           PrefetchProxyPrefetchStatus::kPrefetchFailedNon2XX);
    for (auto& observer : observer_list_) {
      observer.OnPrefetchCompletedWithError(url, response_code);
    }

    if (response_code == net::HTTP_SERVICE_UNAVAILABLE) {
      base::TimeDelta retry_after;
      std::string retry_after_string;
      if (head->headers->EnumerateHeader(nullptr, "Retry-After",
                                         &retry_after_string) &&
          net::HttpUtil::ParseRetryAfterHeader(
              retry_after_string, base::Time::Now(), &retry_after)) {
        PrefetchProxyService* service =
            PrefetchProxyServiceFactory::GetForProfile(profile_);
        service->origin_decider()->ReportOriginRetryAfter(url, retry_after);
      }
    }

    return;
  }

  if (head->mime_type != "text/html") {
    OnPrefetchStatusUpdate(url,
                           PrefetchProxyPrefetchStatus::kPrefetchFailedNotHTML);
    return;
  }

  std::unique_ptr<PrefetchedMainframeResponseContainer> response =
      std::make_unique<PrefetchedMainframeResponseContainer>(
          isolation_info, std::move(head), std::move(body));
  page_->prefetched_responses_.emplace(url, std::move(response));
  page_->prefetch_start_times_.emplace(url, base::TimeTicks::Now());
  page_->srp_metrics_->prefetch_successful_count_++;

  OnPrefetchStatusUpdate(url, PrefetchProxyPrefetchStatus::kPrefetchSuccessful);

  MaybeDoNoStatePrefetch(url);

  for (auto& observer : observer_list_) {
    observer.OnPrefetchCompletedSuccessfully(url);
  }
}

void PrefetchProxyTabHelper::MaybeDoNoStatePrefetch(const GURL& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!PrefetchProxyNoStatePrefetchSubresources()) {
    return;
  }

  // Not all prefetches are eligible for NSP, which fetches subresources.
  if (!base::Contains(page_->allowed_to_prefetch_subresources_, url))
    return;

  page_->urls_to_no_state_prefetch_.push_back(url);
  DoNoStatePrefetch();
}

void PrefetchProxyTabHelper::DoNoStatePrefetch() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (page_->urls_to_no_state_prefetch_.empty()) {
    return;
  }

  // Ensure there is not an active navigation.
  if (web_contents()->GetController().GetPendingEntry()) {
    return;
  }

  absl::optional<size_t> max_attempts =
      PrefetchProxyMaximumNumberOfNoStatePrefetchAttempts();
  if (max_attempts.has_value() &&
      page_->number_of_no_state_prefetch_attempts_ >= max_attempts.value()) {
    return;
  }

  prerender::NoStatePrefetchManager* no_state_prefetch_manager =
      prerender::NoStatePrefetchManagerFactory::GetForBrowserContext(profile_);
  if (!no_state_prefetch_manager) {
    return;
  }

  PrefetchProxyService* service =
      PrefetchProxyServiceFactory::GetForProfile(profile_);
  if (!service) {
    return;
  }

  GURL url = page_->urls_to_no_state_prefetch_[0];

  // Don't start another NSP until the previous one finishes.
  {
    PrefetchProxySubresourceManager* manager =
        service->GetSubresourceManagerForURL(url);
    if (manager && manager->has_nsp_handle()) {
      return;
    }
  }

  // The manager must be created here so that the mainframe response can be
  // given to the URLLoaderInterceptor in this call stack, but may be destroyed
  // before the end of the method if the handle is not created.
  PrefetchProxySubresourceManager* manager =
      service->OnAboutToNoStatePrefetch(url, CopyPrefetchResponseForNSP(url));
  DCHECK_EQ(manager, service->GetSubresourceManagerForURL(url));

  manager->SetPrefetchMetricsCollector(page_->prefetch_metrics_collector_);

  manager->SetCreateIsolatedLoaderFactoryCallback(
      base::BindRepeating(&PrefetchProxyTabHelper::CreateNewURLLoaderFactory,
                          weak_factory_.GetWeakPtr()));

  content::SessionStorageNamespace* session_storage_namespace =
      web_contents()->GetController().GetDefaultSessionStorageNamespace();
  gfx::Size size = web_contents()->GetContainerBounds().size();

  std::unique_ptr<prerender::NoStatePrefetchHandle> handle =
      no_state_prefetch_manager->AddIsolatedPrerender(
          url, session_storage_namespace, size);

  if (!handle) {
    // Clean up the prefetch response in |service| since it wasn't used.
    service->DestroySubresourceManagerForURL(url);
    // Don't use |manager| again!

    page_->failed_no_state_prefetch_urls_.push_back(url);

    // Try the next URL.
    page_->urls_to_no_state_prefetch_.erase(
        page_->urls_to_no_state_prefetch_.begin());
    DoNoStatePrefetch();
    return;
  }

  page_->number_of_no_state_prefetch_attempts_++;

  // It is possible for the manager to be destroyed during the NoStatePrefetch
  // navigation. If this happens, abort the NSP and try again.
  manager = service->GetSubresourceManagerForURL(url);
  if (!manager) {
    handle->OnCancel();
    handle.reset();

    page_->failed_no_state_prefetch_urls_.push_back(url);

    // Try the next URL.
    page_->urls_to_no_state_prefetch_.erase(
        page_->urls_to_no_state_prefetch_.begin());
    DoNoStatePrefetch();
    return;
  }

  manager->ManageNoStatePrefetch(
      std::move(handle),
      base::BindOnce(&PrefetchProxyTabHelper::OnPrerenderDone,
                     weak_factory_.GetWeakPtr(), url));
}

void PrefetchProxyTabHelper::OnPrerenderDone(const GURL& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // The completed NSP probably consumed a previously started spare renderer, so
  // kick off another one if needed.
  if (ShouldStartSpareRenderer()) {
    StartSpareRenderer();
  }

  // It is possible that this is run as a callback after a navigation has
  // already happened and |page_| is now a different instance than when the
  // prerender was started. In this case, just return.
  if (page_->urls_to_no_state_prefetch_.empty() ||
      url != page_->urls_to_no_state_prefetch_[0]) {
    return;
  }

  page_->no_state_prefetched_urls_.push_back(
      page_->urls_to_no_state_prefetch_[0]);

  for (auto& observer : observer_list_) {
    observer.OnNoStatePrefetchFinished();
  }

  page_->urls_to_no_state_prefetch_.erase(
      page_->urls_to_no_state_prefetch_.begin());

  DoNoStatePrefetch();
}

void PrefetchProxyTabHelper::StartSpareRenderer() {
  page_->number_of_spare_renderers_started_++;
  content::RenderProcessHost::WarmupSpareRenderProcessHost(profile_);
}

void PrefetchProxyTabHelper::PrefetchSpeculationCandidates(
    const std::vector<GURL>& private_prefetches_with_subresources,
    const std::vector<GURL>& private_prefetches,
    const GURL& source_document_url) {
  // Use navigation predictor by default.
  if (!PrefetchProxyUseSpeculationRules())
    return;

  // For IP-private prefetches, using the Google proxy needs to be restricted to
  // first party sites until we understand the benefit and determine interest
  // from other sites.
  if (!PrefetchProxyAllowAllDomains() &&
      !IsGoogleDomainUrl(source_document_url, google_util::ALLOW_SUBDOMAIN,
                         google_util::ALLOW_NON_STANDARD_PORTS)) {
    return;
  }

  std::vector<GURL> prefetches = private_prefetches;
  std::set<GURL> allowed_to_prefetch_subresources;
  for (auto url : private_prefetches_with_subresources) {
    prefetches.push_back(url);
    allowed_to_prefetch_subresources.insert(url);
  }

  PrefetchUrls(prefetches, allowed_to_prefetch_subresources);
}

void PrefetchProxyTabHelper::OnPredictionUpdated(
    const absl::optional<NavigationPredictorKeyedService::Prediction>
        prediction) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Use speculation rules API instead of navigation predictor.
  if (PrefetchProxyUseSpeculationRules())
    return;

  if (!prediction.has_value()) {
    return;
  }

  if (prediction->prediction_source() !=
      NavigationPredictorKeyedService::PredictionSource::
          kAnchorElementsParsedFromWebPage) {
    return;
  }

  if (prediction.value().web_contents() != web_contents()) {
    // We only care about predictions in this tab.
    return;
  }

  const absl::optional<GURL>& source_document_url =
      prediction->source_document_url();

  if (!source_document_url || source_document_url->is_empty())
    return;

  if (!google_util::IsGoogleSearchUrl(source_document_url.value())) {
    return;
  }

  // For the navigation predictor approach, we assume all predicted URLs are
  // eligible for NSP.
  std::set<GURL> allowed_to_prefetch_subresources(
      prediction.value().sorted_predicted_urls().begin(),
      prediction.value().sorted_predicted_urls().end());
  PrefetchUrls(prediction.value().sorted_predicted_urls(),
               allowed_to_prefetch_subresources);
}

void PrefetchProxyTabHelper::PrefetchUrls(
    const std::vector<GURL>& prefetch_targets,
    const std::set<GURL>& allowed_to_prefetch_subresources) {
  if (!PrefetchProxyIsEnabled()) {
    return;
  }

  if (!IsProfileEligible()) {
    return;
  }

  // This checks whether the user has enabled pre* actions in the settings UI.
  if (!chrome_browser_net::CanPreresolveAndPreconnectUI(profile_->GetPrefs())) {
    return;
  }

  if (!page_->prefetch_metrics_collector_) {
    page_->prefetch_metrics_collector_ =
        base::MakeRefCounted<PrefetchProxyPrefetchMetricsCollector>(
            page_->navigation_start_,
            web_contents()->GetMainFrame()->GetPageUkmSourceId());
  }

  // Remove duplicate prefetches, but allow |allowed_to_prefetch_subresources|
  // to be set for any upgraded prefetches.
  std::vector<GURL> new_targets;
  for (const auto& prefetch : prefetch_targets) {
    if (page_->original_prediction_ordering_.find(prefetch) ==
        page_->original_prediction_ordering_.end()) {
      new_targets.push_back(prefetch);
    }
  }

  // It's very likely we'll prefetch something at this point, so inform PLM to
  // start tracking metrics.
  InformPLMOfLikelyPrefetching(web_contents());

  page_->srp_metrics_->predicted_urls_count_ += new_targets.size();

  // It is possible, since it is not stipulated by the API contract, that the
  // navigation predictor will issue multiple predictions during a single page
  // load. Additional predictions should be treated as appending to the ordering
  // of previous predictions.
  size_t original_prediction_ordering_starting_size =
      page_->original_prediction_ordering_.size();

  page_->allowed_to_prefetch_subresources_.insert(
      allowed_to_prefetch_subresources.begin(),
      allowed_to_prefetch_subresources.end());

  for (size_t i = 0; i < new_targets.size(); ++i) {
    GURL url = new_targets[i];

    size_t url_index = original_prediction_ordering_starting_size + i;
    page_->original_prediction_ordering_.emplace(url, url_index);

    CheckEligibilityOfURL(
        profile_, url,
        base::BindOnce(&PrefetchProxyTabHelper::OnGotEligibilityResult,
                       weak_factory_.GetWeakPtr()));
  }
}

// static
content::ServiceWorkerContext* PrefetchProxyTabHelper::GetServiceWorkerContext(
    Profile* profile) {
  if (g_service_worker_context_for_test)
    return g_service_worker_context_for_test;
  return profile->GetDefaultStoragePartition()->GetServiceWorkerContext();
}

// static
std::pair<bool, absl::optional<PrefetchProxyPrefetchStatus>>
PrefetchProxyTabHelper::CheckEligibilityOfURLSansUserData(Profile* profile,
                                                          const GURL& url) {
  if (!IsProfileEligible(profile)) {
    return std::make_pair(false, absl::nullopt);
  }

  if (!PrefetchProxyUseSpeculationRules() &&
      google_util::IsGoogleAssociatedDomainUrl(url)) {
    return std::make_pair(
        false, PrefetchProxyPrefetchStatus::kPrefetchNotEligibleGoogleDomain);
  }

  if (url.HostIsIPAddress()) {
    return std::make_pair(
        false,
        PrefetchProxyPrefetchStatus::kPrefetchNotEligibleHostIsIPAddress);
  }

  if (!url.SchemeIs(url::kHttpsScheme)) {
    return std::make_pair(
        false,
        PrefetchProxyPrefetchStatus::kPrefetchNotEligibleSchemeIsNotHttps);
  }

  PrefetchProxyService* prefetch_proxy_service =
      PrefetchProxyServiceFactory::GetForProfile(profile);
  if (!prefetch_proxy_service) {
    return std::make_pair(false, absl::nullopt);
  }

  if (!prefetch_proxy_service->proxy_configurator()
           ->IsPrefetchProxyAvailable()) {
    return std::make_pair(
        false, PrefetchProxyPrefetchStatus::kPrefetchProxyNotAvailable);
  }

  return std::make_pair(true, absl::nullopt);
}

// static
void PrefetchProxyTabHelper::CheckEligibilityOfURL(
    Profile* profile,
    const GURL& url,
    OnEligibilityResultCallback result_callback) {
  auto no_user_data_check = CheckEligibilityOfURLSansUserData(profile, url);
  if (!no_user_data_check.first) {
    std::move(result_callback).Run(url, false, no_user_data_check.second);
    return;
  }

  content::StoragePartition* default_storage_partition =
      profile->GetDefaultStoragePartition();

  // Only the default storage partition is supported since that is the only
  // place where service workers are observed by
  // |PrefetchProxyServiceWorkersObserver|.
  if (default_storage_partition !=
      profile->GetStoragePartitionForUrl(url,
                                         /*can_create=*/false)) {
    std::move(result_callback)
        .Run(url, false,
             PrefetchProxyPrefetchStatus::
                 kPrefetchNotEligibleNonDefaultStoragePartition);
    return;
  }

  PrefetchProxyService* prefetch_proxy_service =
      PrefetchProxyServiceFactory::GetForProfile(profile);
  if (!prefetch_proxy_service) {
    std::move(result_callback).Run(url, false, absl::nullopt);
    return;
  }

  if (!prefetch_proxy_service->origin_decider()
           ->IsOriginOutsideRetryAfterWindow(url)) {
    std::move(result_callback)
        .Run(url, false,
             PrefetchProxyPrefetchStatus::kPrefetchIneligibleRetryAfter);
    return;
  }

  content::ServiceWorkerContext* service_worker_context_ =
      GetServiceWorkerContext(profile);

  // This service worker check assumes that the prefetch will only ever be
  // performed in a first-party context (main frame prefetch). At the moment
  // that is true but if it ever changes then the StorageKey will need to be
  // constructed with the top-level site to ensure correct partitioning.
  bool site_has_service_worker =
      service_worker_context_->MaybeHasRegistrationForStorageKey(
          blink::StorageKey(url::Origin::Create(url)));
  if (site_has_service_worker) {
    std::move(result_callback)
        .Run(url, false,
             PrefetchProxyPrefetchStatus::
                 kPrefetchNotEligibleUserHasServiceWorker);
    return;
  }

  net::CookieOptions options = net::CookieOptions::MakeAllInclusive();
  options.set_return_excluded_cookies();
  default_storage_partition->GetCookieManagerForBrowserProcess()->GetCookieList(
      url, options, net::CookiePartitionKeychain::Todo(),
      base::BindOnce(&OnGotCookieList, url, std::move(result_callback)));
}

void PrefetchProxyTabHelper::OnGotEligibilityResult(
    const GURL& url,
    bool eligible,
    absl::optional<PrefetchProxyPrefetchStatus> status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // It is possible that this callback is being run late. That is, after the
  // user has navigated away from the origin SRP. To detect this, check if the
  // url exists in the set of predicted urls. If it doesn't, do nothing.
  if (page_->original_prediction_ordering_.find(url) ==
      page_->original_prediction_ordering_.end()) {
    return;
  }

  if (!eligible) {
    if (status) {
      OnPrefetchStatusUpdate(url, *status);
      if (page_->prefetch_metrics_collector_) {
        page_->prefetch_metrics_collector_->OnMainframeResourceNotEligible(
            url, page_->original_prediction_ordering_.find(url)->second,
            *status);
      }

      // Consider whether to send a decoy request to mask any user state (i.e.:
      // cookies), and if so randomly decide whether to send a decoy request.
      if (ShouldConsiderDecoyRequestForStatus(*status) &&
          PrefetchProxySendDecoyRequestForIneligiblePrefetch()) {
        page_->decoy_urls_.emplace(url);
        page_->urls_to_prefetch_.push_back(url);
        OnPrefetchStatusUpdate(
            url, PrefetchProxyPrefetchStatus::kPrefetchIsPrivacyDecoy);
        Prefetch();
      }
    }

    return;
  }

  // TODO(robertogden): Consider adding redirect URLs to the front of the list.
  page_->urls_to_prefetch_.push_back(url);
  page_->srp_metrics_->prefetch_eligible_count_++;
  OnPrefetchStatusUpdate(url, PrefetchProxyPrefetchStatus::kPrefetchNotStarted);

  if (page_->original_prediction_ordering_.find(url) !=
      page_->original_prediction_ordering_.end()) {
    size_t original_prediction_index =
        page_->original_prediction_ordering_.find(url)->second;
    // Check that we won't go above the allowable size.
    if (original_prediction_index <
        sizeof(page_->srp_metrics_->ordered_eligible_pages_bitmask_) * 8) {
      page_->srp_metrics_->ordered_eligible_pages_bitmask_ |=
          1 << original_prediction_index;
    }

    if (!PrefetchProxyShouldPrefetchPosition(original_prediction_index)) {
      OnPrefetchStatusUpdate(
          url, PrefetchProxyPrefetchStatus::kPrefetchPositionIneligible);
      return;
    }
  }

  Prefetch();

  // Registers a cookie listener for this URL. If the cookies in the default
  // partition change after this point, then the prefetched resources should not
  // be served.
  page_->cookie_listeners_[url] = PrefetchProxyCookieListener::MakeAndRegister(
      url, profile_->GetDefaultStoragePartition()
               ->GetCookieManagerForBrowserProcess());

  for (auto& observer : observer_list_) {
    observer.OnNewEligiblePrefetchStarted();
  }
}

bool PrefetchProxyTabHelper::IsWaitingForAfterSRPCookiesCopy() const {
  switch (page_->cookie_copy_status_) {
    case CookieCopyStatus::kNoNavigation:
    case CookieCopyStatus::kCopyComplete:
      return false;
    case CookieCopyStatus::kWaitingForCopy:
      return true;
  }
}

void PrefetchProxyTabHelper::SetOnAfterSRPCookieCopyCompleteCallback(
    base::OnceClosure callback) {
  // We don't expect a callback unless there's something to wait on.
  DCHECK(IsWaitingForAfterSRPCookiesCopy());

  page_->on_after_srp_cookie_copy_complete_ = std::move(callback);
}

void PrefetchProxyTabHelper::CopyIsolatedCookiesOnAfterSRPClick(
    const GURL& url) {
  if (!page_->isolated_network_context_) {
    // Not set in unit tests.
    return;
  }

  // We don't want the cookie listener for this URL to get the changes from the
  // copy.
  page_->cookie_listeners_[url]->StopListening();

  page_->cookie_copy_status_ = CookieCopyStatus::kWaitingForCopy;

  if (!page_->isolated_cookie_manager_) {
    page_->isolated_network_context_->GetCookieManager(
        page_->isolated_cookie_manager_.BindNewPipeAndPassReceiver());
  }

  net::CookieOptions options = net::CookieOptions::MakeAllInclusive();
  page_->isolated_cookie_manager_->GetCookieList(
      url, options, net::CookiePartitionKeychain::Todo(),
      base::BindOnce(
          &PrefetchProxyTabHelper::OnGotIsolatedCookiesToCopyAfterSRPClick,
          weak_factory_.GetWeakPtr(), url));
}

void PrefetchProxyTabHelper::OnGotIsolatedCookiesToCopyAfterSRPClick(
    const GURL& url,
    const net::CookieAccessResultList& cookie_list,
    const net::CookieAccessResultList& excluded_cookies) {
  DCHECK(IsWaitingForAfterSRPCookiesCopy());

  UMA_HISTOGRAM_COUNTS_100("PrefetchProxy.Prefetch.Mainframe.CookiesToCopy",
                           cookie_list.size());

  if (cookie_list.empty()) {
    OnCopiedIsolatedCookiesAfterSRPClick();
    return;
  }

  // When |barrier| is run |cookie_list.size()| times, it will run
  // |OnCopiedIsolatedCookiesAfterSRPClick|.
  base::RepeatingClosure barrier = base::BarrierClosure(
      cookie_list.size(),
      base::BindOnce(
          &PrefetchProxyTabHelper::OnCopiedIsolatedCookiesAfterSRPClick,
          weak_factory_.GetWeakPtr()));

  content::StoragePartition* default_storage_partition =
      profile_->GetDefaultStoragePartition();
  net::CookieOptions options = net::CookieOptions::MakeAllInclusive();

  for (const net::CookieWithAccessResult& cookie : cookie_list) {
    default_storage_partition->GetCookieManagerForBrowserProcess()
        ->SetCanonicalCookie(cookie.cookie, url, options,
                             base::BindOnce(&CookieSetHelper, barrier));
  }
}

void PrefetchProxyTabHelper::OnCopiedIsolatedCookiesAfterSRPClick() {
  DCHECK(IsWaitingForAfterSRPCookiesCopy());

  page_->cookie_copy_status_ = CookieCopyStatus::kCopyComplete;
  if (page_->on_after_srp_cookie_copy_complete_) {
    std::move(page_->on_after_srp_cookie_copy_complete_).Run();
  }
}

network::mojom::URLLoaderFactory*
PrefetchProxyTabHelper::GetURLLoaderFactory() {
  if (!page_->isolated_url_loader_factory_) {
    CreateIsolatedURLLoaderFactory();
  }
  DCHECK(page_->isolated_url_loader_factory_);
  return page_->isolated_url_loader_factory_.get();
}

void PrefetchProxyTabHelper::CreateNewURLLoaderFactory(
    mojo::PendingReceiver<network::mojom::URLLoaderFactory> pending_receiver,
    absl::optional<net::IsolationInfo> isolation_info) {
  DCHECK(page_->isolated_network_context_);

  auto factory_params = network::mojom::URLLoaderFactoryParams::New();
  factory_params->process_id = network::mojom::kBrowserProcessId;
  factory_params->is_trusted = true;
  factory_params->is_corb_enabled = false;
  if (isolation_info) {
    factory_params->isolation_info = *isolation_info;
  }

  page_->isolated_network_context_->CreateURLLoaderFactory(
      std::move(pending_receiver), std::move(factory_params));
}

void PrefetchProxyTabHelper::CreateIsolatedURLLoaderFactory() {
  page_->isolated_network_context_.reset();
  page_->isolated_url_loader_factory_.reset();

  PrefetchProxyService* prefetch_proxy_service =
      PrefetchProxyServiceFactory::GetForProfile(profile_);

  auto context_params = network::mojom::NetworkContextParams::New();
  context_params->user_agent = content::GetReducedUserAgent(
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kUseMobileUserAgent),
      version_info::GetMajorVersionNumber());
  context_params->accept_language = net::HttpUtil::GenerateAcceptLanguageHeader(
      profile_->GetPrefs()->GetString(language::prefs::kAcceptLanguages));
  context_params->initial_custom_proxy_config =
      prefetch_proxy_service->proxy_configurator()->CreateCustomProxyConfig();
  context_params->custom_proxy_connection_observer_remote =
      prefetch_proxy_service->proxy_configurator()
          ->NewProxyConnectionObserverRemote();
  context_params->cert_verifier_params = content::GetCertVerifierParams(
      cert_verifier::mojom::CertVerifierCreationParams::New());
  context_params->cors_exempt_header_list = {
      content::kCorsExemptPurposeHeaderName};
  context_params->cookie_manager_params =
      network::mojom::CookieManagerParams::New();

  context_params->http_cache_enabled = true;
  DCHECK(!context_params->http_cache_path);

  // Also register a client config receiver so that updates to the set of proxy
  // hosts or proxy headers will be updated.
  mojo::Remote<network::mojom::CustomProxyConfigClient> config_client;
  context_params->custom_proxy_config_client_receiver =
      config_client.BindNewPipeAndPassReceiver();
  prefetch_proxy_service->proxy_configurator()->AddCustomProxyConfigClient(
      std::move(config_client), base::DoNothing());

  // Explicitly disallow network service features which could cause a privacy
  // leak.
  context_params->enable_certificate_reporting = false;
  context_params->enable_expect_ct_reporting = false;
  context_params->enable_domain_reliability = false;

  content::CreateNetworkContextInNetworkService(
      page_->isolated_network_context_.BindNewPipeAndPassReceiver(),
      std::move(context_params));

  // Configure a context client to ensure Web Reports and other privacy leak
  // surfaces won't be enabled.
  mojo::PendingRemote<network::mojom::NetworkContextClient> client_remote;
  mojo::MakeSelfOwnedReceiver(
      std::make_unique<PrefetchProxyNetworkContextClient>(),
      client_remote.InitWithNewPipeAndPassReceiver());
  page_->isolated_network_context_->SetClient(std::move(client_remote));

  mojo::PendingRemote<network::mojom::URLLoaderFactory> isolated_factory_remote;

  CreateNewURLLoaderFactory(
      isolated_factory_remote.InitWithNewPipeAndPassReceiver(), absl::nullopt);

  page_->isolated_url_loader_factory_ = network::SharedURLLoaderFactory::Create(
      std::make_unique<network::WrapperPendingSharedURLLoaderFactory>(
          std::move(isolated_factory_remote)));
}

bool PrefetchProxyTabHelper::HaveCookiesChanged(const GURL& url) const {
  auto cookie_listener_itr = page_->cookie_listeners_.find(url);
  if (cookie_listener_itr == page_->cookie_listeners_.cend())
    return false;
  return cookie_listener_itr->second->HaveCookiesChanged();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(PrefetchProxyTabHelper);
