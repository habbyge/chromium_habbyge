// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/commerce/shopping_persisted_data_tab_helper.h"

#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "components/optimization_guide/core/optimization_metadata.h"
#import "ios/chrome/browser/application_context.h"
#import "ios/chrome/browser/browser_state/chrome_browser_state.h"
#import "ios/chrome/browser/commerce/price_alert_util.h"
#import "ios/chrome/browser/optimization_guide/optimization_guide_service.h"
#import "ios/chrome/browser/optimization_guide/optimization_guide_service_factory.h"
#import "ios/web/public/navigation/navigation_context.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {
const int kUnitsToMicros = 1000000;
const int kMinimumDropThresholdAbsolute = 2 * kUnitsToMicros;
const int kMinimumDropThresholdRelative = 10;
const int kMicrosToTwoDecimalPlaces = 10000;
const int kTwoDecimalPlacesMaximumThreshold = 10 * kUnitsToMicros;
const int kStaleThresholdHours = 1;
const base::TimeDelta kStaleDuration = base::Hours(kStaleThresholdHours);

// Returns true if a cached price drop has gone stale and should be
// re-fetched from OptimizationGuide.
BOOL IsPriceDropStale(base::Time price_drop_timestamp) {
  return base::Time::Now() - price_drop_timestamp > kStaleDuration;
}

}  // namespace

ShoppingPersistedDataTabHelper::~ShoppingPersistedDataTabHelper() {
  if (web_state_) {
    web_state_->RemoveObserver(this);
    web_state_ = nullptr;
  }
}

void ShoppingPersistedDataTabHelper::CreateForWebState(
    web::WebState* web_state) {
  if (FromWebState(web_state))
    return;
  web_state->SetUserData(
      UserDataKey(),
      base::WrapUnique(new ShoppingPersistedDataTabHelper(web_state)));
  OptimizationGuideService* optimization_guide_service =
      OptimizationGuideServiceFactory::GetForBrowserState(
          ChromeBrowserState::FromBrowserState(web_state->GetBrowserState()));
  if (!optimization_guide_service)
    return;

  optimization_guide_service->RegisterOptimizationTypes(
      {optimization_guide::proto::PRICE_TRACKING});
}

const ShoppingPersistedDataTabHelper::PriceDrop*
ShoppingPersistedDataTabHelper::GetPriceDrop() {
  if (!IsPriceAlertsEligible(web_state_->GetBrowserState()) ||
      !IsPriceAlertsEnabled())
    return nullptr;
  const GURL& url = web_state_->GetLastCommittedURL().is_valid()
                        ? web_state_->GetLastCommittedURL()
                        : web_state_->GetVisibleURL();
  if (!price_drop_ || price_drop_->url != url ||
      IsPriceDropStale(price_drop_->timestamp)) {
    ResetPriceDrop();
    OptimizationGuideService* optimization_guide_service =
        OptimizationGuideServiceFactory::GetForBrowserState(
            ChromeBrowserState::FromBrowserState(
                web_state_->GetBrowserState()));
    if (!optimization_guide_service)
      return nullptr;
    optimization_guide::OptimizationMetadata metadata;
    if (optimization_guide_service->CanApplyOptimization(
            url, optimization_guide::proto::PRICE_TRACKING, &metadata) !=
        optimization_guide::OptimizationGuideDecision::kTrue)
      return nullptr;
    ParseProto(url, metadata.ParsedMetadata<commerce::PriceTrackingData>());
  }
  if (price_drop_) {
    return price_drop_.get();
  }
  return nullptr;
}

ShoppingPersistedDataTabHelper::ShoppingPersistedDataTabHelper(
    web::WebState* web_state)
    : web_state_(web_state) {
  web_state_->AddObserver(this);
}

// static
BOOL ShoppingPersistedDataTabHelper::IsQualifyingPriceDrop(
    int64_t current_price_micros,
    int64_t previous_price_micros) {
  if (previous_price_micros - current_price_micros <
      kMinimumDropThresholdAbsolute) {
    return false;
  }
  if ((100 * current_price_micros) / previous_price_micros >
      (100 - kMinimumDropThresholdRelative)) {
    return false;
  }
  return true;
}

// static
std::u16string ShoppingPersistedDataTabHelper::FormatPrice(
    payments::CurrencyFormatter* currency_formatter,
    long price_micros) {
  currency_formatter->SetMaxFractionalDigits(
      price_micros >= kTwoDecimalPlacesMaximumThreshold ? 0 : 2);
  long twoDecimalPlaces = price_micros / kMicrosToTwoDecimalPlaces;
  std::u16string result = currency_formatter->Format(base::StringPrintf(
      "%s.%s", base::NumberToString(twoDecimalPlaces / 100).c_str(),
      base::NumberToString(twoDecimalPlaces % 100).c_str()));
  return result;
}

void ShoppingPersistedDataTabHelper::DidFinishNavigation(
    web::WebState* web_state,
    web::NavigationContext* navigation_context) {
  if (!IsPriceAlertsEligible(web_state->GetBrowserState()) ||
      !IsPriceAlertsEnabled())
    return;
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!navigation_context->GetUrl().SchemeIsHTTPOrHTTPS())
    return;

  ResetPriceDrop();
  OptimizationGuideService* optimization_guide_service =
      OptimizationGuideServiceFactory::GetForBrowserState(
          ChromeBrowserState::FromBrowserState(web_state->GetBrowserState()));
  if (!optimization_guide_service)
    return;

  optimization_guide_service->CanApplyOptimizationAsync(
      navigation_context, optimization_guide::proto::PRICE_TRACKING,
      base::BindOnce(
          &ShoppingPersistedDataTabHelper::OnOptimizationGuideResultReceived,
          weak_factory_.GetWeakPtr(), navigation_context->GetUrl()));
}

void ShoppingPersistedDataTabHelper::WebStateDestroyed(
    web::WebState* web_state) {
  web_state->RemoveObserver(this);
  web_state_ = nullptr;
}

void ShoppingPersistedDataTabHelper::OnOptimizationGuideResultReceived(
    const GURL& url,
    optimization_guide::OptimizationGuideDecision decision,
    const optimization_guide::OptimizationMetadata& metadata) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (decision != optimization_guide::OptimizationGuideDecision::kTrue)
    return;

  ParseProto(url, metadata.ParsedMetadata<commerce::PriceTrackingData>());
}

payments::CurrencyFormatter*
ShoppingPersistedDataTabHelper::GetCurrencyFormatter(
    const std::string& currency_code,
    const std::string& locale_name) {
  // Create a currency formatter for |currency_code|, or if already created
  // return the cached version.
  std::pair<std::map<std::string, payments::CurrencyFormatter>::iterator, bool>
      emplace_result = currency_formatter_map_.emplace(
          std::piecewise_construct, std::forward_as_tuple(currency_code),
          std::forward_as_tuple(currency_code, locale_name));
  return &(emplace_result.first->second);
}

void ShoppingPersistedDataTabHelper::ParseProto(
    const GURL& url,
    const absl::optional<commerce::PriceTrackingData>& price_metadata) {
  if (!price_metadata || !price_metadata->has_product_update())
    return;

  const auto& product_update = price_metadata->product_update();
  if (!product_update.has_old_price() || !product_update.has_new_price())
    return;

  if (product_update.old_price().currency_code() !=
      product_update.new_price().currency_code())
    return;

  if (!IsQualifyingPriceDrop(product_update.new_price().amount_micros(),
                             product_update.old_price().amount_micros())) {
    return;
  }

  // TODO(crbug.com/1254900) Filter out non-qualifying price drops (< 10% or
  // < 2 units).
  payments::CurrencyFormatter* currencyFormatter =
      GetCurrencyFormatter(product_update.old_price().currency_code(),
                           GetApplicationContext()->GetApplicationLocale());
  price_drop_ = std::make_unique<PriceDrop>();
  price_drop_->current_price = base::SysUTF16ToNSString(FormatPrice(
      currencyFormatter, product_update.new_price().amount_micros()));
  price_drop_->previous_price = base::SysUTF16ToNSString(FormatPrice(
      currencyFormatter, product_update.old_price().amount_micros()));
  price_drop_->url = url;
  price_drop_->timestamp = base::Time::Now();
}

void ShoppingPersistedDataTabHelper::ResetPriceDrop() {
  price_drop_ = nullptr;
}

WEB_STATE_USER_DATA_KEY_IMPL(ShoppingPersistedDataTabHelper)
