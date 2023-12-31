// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/policy/core/common/features.h"

namespace policy {

namespace features {

const base::Feature kPolicyBlocklistThrottleRequiresPoliciesLoaded{
    "PolicyBlocklistThrottleRequiresPoliciesLoaded",
    base::FEATURE_DISABLED_BY_DEFAULT};

const base::FeatureParam<base::TimeDelta>
    kPolicyBlocklistThrottlePolicyLoadTimeout{
        &kPolicyBlocklistThrottleRequiresPoliciesLoaded,
        "PolicyBlocklistThrottlePolicyLoadTimeout", base::Seconds(20)};

const base::Feature kUploadBrowserDeviceIdentifier{
    "UploadBrowserDeviceIdentifier", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kLoginEventReporting{"LoginEventReporting",
                                         base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kPasswordBreachEventReporting{
    "PasswordBreachEventReporting", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kChromeManagementPageAndroid{
    "ChromeManagementPageAndroid", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kEnableUserCloudSigninRestrictionPolicyFetcher{
    "UserCloudSigninRestrictionPolicyFetcher",
    base::FEATURE_DISABLED_BY_DEFAULT};

}  // namespace features

}  // namespace policy
