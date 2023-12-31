// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/attribution_reporting/attribution_internals_handler_impl.h"

#include <utility>
#include <vector>

#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/containers/circular_deque.h"
#include "base/notreached.h"
#include "base/ranges/algorithm.h"
#include "base/time/time.h"
#include "content/browser/attribution_reporting/attribution_manager_impl.h"
#include "content/browser/attribution_reporting/attribution_report.h"
#include "content/browser/attribution_reporting/attribution_session_storage.h"
#include "content/browser/attribution_reporting/attribution_storage.h"
#include "content/browser/attribution_reporting/sent_report_info.h"
#include "content/browser/attribution_reporting/storable_source.h"
#include "content/browser/storage_partition_impl.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"

namespace content {

namespace {

using CreateReportStatus =
    ::content::AttributionStorage::CreateReportResult::Status;

mojom::SourceType SourceTypeToMojoType(StorableSource::SourceType input) {
  switch (input) {
    case StorableSource::SourceType::kNavigation:
      return mojom::SourceType::kNavigation;
    case StorableSource::SourceType::kEvent:
      return mojom::SourceType::kEvent;
  }
}

void ForwardSourcesToWebUI(
    mojom::AttributionInternalsHandler::GetActiveSourcesCallback
        web_ui_callback,
    std::vector<StorableSource> stored_sources) {
  std::vector<mojom::WebUIAttributionSourcePtr> web_ui_sources;
  web_ui_sources.reserve(stored_sources.size());

  for (const StorableSource& impression : stored_sources) {
    web_ui_sources.push_back(mojom::WebUIAttributionSource::New(
        impression.source_event_id(), impression.impression_origin(),
        impression.ConversionDestination().Serialize(),
        impression.reporting_origin(), impression.impression_time().ToJsTime(),
        impression.expiry_time().ToJsTime(),
        SourceTypeToMojoType(impression.source_type()), impression.priority(),
        impression.dedup_keys(),
        /*reportable=*/impression.attribution_logic() ==
            StorableSource::AttributionLogic::kTruthfully));
  }

  std::move(web_ui_callback).Run(std::move(web_ui_sources));
}

mojom::WebUIAttributionReportPtr WebUIAttributionReport(
    const AttributionReport& report,
    int http_response_code,
    mojom::WebUIAttributionReport::Status status) {
  return mojom::WebUIAttributionReport::New(
      report.impression.ConversionDestination().Serialize(), report.ReportURL(),
      /*trigger_time=*/report.conversion_time.ToJsTime(),
      /*report_time=*/report.report_time.ToJsTime(), report.priority,
      report.ReportBody(/*pretty_print=*/true),
      /*attributed_truthfully=*/report.impression.attribution_logic() ==
          StorableSource::AttributionLogic::kTruthfully,
      status, http_response_code);
}

void ForwardReportsToWebUI(
    mojom::AttributionInternalsHandler::GetReportsCallback web_ui_callback,
    std::vector<mojom::WebUIAttributionReportPtr> web_ui_reports,
    std::vector<AttributionReport> pending_reports) {
  web_ui_reports.reserve(web_ui_reports.capacity() + pending_reports.size());
  for (const AttributionReport& report : pending_reports) {
    web_ui_reports.push_back(WebUIAttributionReport(
        report, /*http_response_code=*/0,
        mojom::WebUIAttributionReport::Status::kPending));
  }

  base::ranges::sort(web_ui_reports, std::less<>(),
                     [](const auto& report) { return report->report_time; });
  std::move(web_ui_callback).Run(std::move(web_ui_reports));
}

}  // namespace

AttributionInternalsHandlerImpl::AttributionInternalsHandlerImpl(
    WebUI* web_ui,
    mojo::PendingReceiver<mojom::AttributionInternalsHandler> receiver)
    : web_ui_(web_ui),
      manager_provider_(std::make_unique<AttributionManagerProviderImpl>()),
      receiver_(this, std::move(receiver)) {}

AttributionInternalsHandlerImpl::~AttributionInternalsHandlerImpl() = default;

void AttributionInternalsHandlerImpl::IsAttributionReportingEnabled(
    mojom::AttributionInternalsHandler::IsAttributionReportingEnabledCallback
        callback) {
  content::WebContents* contents = web_ui_->GetWebContents();
  bool attribution_reporting_enabled =
      manager_provider_->GetManager(contents) &&
      GetContentClient()->browser()->IsConversionMeasurementOperationAllowed(
          contents->GetBrowserContext(),
          ContentBrowserClient::ConversionMeasurementOperation::kAny,
          /*impression_origin=*/nullptr, /*conversion_origin=*/nullptr,
          /*reporting_origin=*/nullptr);
  bool debug_mode = base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kConversionsDebugMode);
  std::move(callback).Run(attribution_reporting_enabled, debug_mode);
}

void AttributionInternalsHandlerImpl::GetActiveSources(
    mojom::AttributionInternalsHandler::GetActiveSourcesCallback callback) {
  if (AttributionManager* manager =
          manager_provider_->GetManager(web_ui_->GetWebContents())) {
    manager->GetActiveSourcesForWebUI(
        base::BindOnce(&ForwardSourcesToWebUI, std::move(callback)));
  } else {
    std::move(callback).Run({});
  }
}

void AttributionInternalsHandlerImpl::GetReports(
    mojom::AttributionInternalsHandler::GetReportsCallback callback) {
  if (AttributionManager* manager =
          manager_provider_->GetManager(web_ui_->GetWebContents())) {
    const AttributionSessionStorage& session_storage =
        manager->GetSessionStorage();

    const base::circular_deque<SentReportInfo>& sent_reports =
        session_storage.GetSentReports();
    const auto& dropped_reports = session_storage.GetDroppedReports();

    std::vector<mojom::WebUIAttributionReportPtr> session_cached_reports;
    session_cached_reports.reserve(sent_reports.size() +
                                   dropped_reports.size());

    for (const SentReportInfo& info : sent_reports) {
      session_cached_reports.push_back(
          WebUIAttributionReport(info.report, info.http_response_code,
                                 mojom::WebUIAttributionReport::Status::kSent));
    }

    for (const AttributionStorage::CreateReportResult& result :
         dropped_reports) {
      mojom::WebUIAttributionReport::Status status;
      switch (result.status()) {
        case CreateReportStatus::kSuccessDroppedLowerPriority:
        case CreateReportStatus::kPriorityTooLow:
          status =
              mojom::WebUIAttributionReport::Status::kDroppedDueToLowPriority;
          break;
        case CreateReportStatus::kDroppedForNoise:
          status = mojom::WebUIAttributionReport::Status::kDroppedForNoise;
          break;
        default:
          NOTREACHED();
          continue;
      }

      session_cached_reports.push_back(WebUIAttributionReport(
          *result.dropped_report(), /*http_response_code=*/0, status));
    }

    manager->GetPendingReportsForWebUI(
        base::BindOnce(&ForwardReportsToWebUI, std::move(callback),
                       std::move(session_cached_reports)));
  } else {
    std::move(callback).Run({});
  }
}

void AttributionInternalsHandlerImpl::SendPendingReports(
    mojom::AttributionInternalsHandler::SendPendingReportsCallback callback) {
  if (AttributionManager* manager =
          manager_provider_->GetManager(web_ui_->GetWebContents())) {
    manager->SendReportsForWebUI(std::move(callback));
  } else {
    std::move(callback).Run();
  }
}

void AttributionInternalsHandlerImpl::ClearStorage(
    mojom::AttributionInternalsHandler::ClearStorageCallback callback) {
  if (AttributionManager* manager =
          manager_provider_->GetManager(web_ui_->GetWebContents())) {
    manager->ClearData(base::Time::Min(), base::Time::Max(),
                       base::NullCallback(), std::move(callback));
  } else {
    std::move(callback).Run();
  }
}

void AttributionInternalsHandlerImpl::SetAttributionManagerProviderForTesting(
    std::unique_ptr<AttributionManager::Provider> manager_provider) {
  manager_provider_ = std::move(manager_provider);
}

}  // namespace content
