// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/policy/core/browser/signin/user_cloud_signin_restriction_policy_fetcher.h"

#include <set>

#include "base/command_line.h"
#include "base/strings/stringprintf.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/common/cloud/cloud_policy_client_registration_helper.h"
#include "components/policy/core/common/policy_switches.h"
#include "components/policy/proto/secure_connect.pb.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "google_apis/gaia/core_account_id.h"
#include "google_apis/gaia/gaia_constants.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace policy {

namespace {

const char kAuthorizationHeaderFormat[] = "Bearer %s";
const char kProtobufferContentType[] = "application/x-protobuf";
const char kSecureConnectApiGetManagedAccountsSigninRestrictionsUrl[] =
    "https://secureconnect-pa.googleapis.com/"
    "v1:getManagedAccountsSigninRestriction";

std::unique_ptr<network::SimpleURLLoader> CreateUrlLoader(
    const GURL& url,
    const std::string& access_token,
    net::NetworkTrafficAnnotationTag annotation) {
  auto resource_request = std::make_unique<network::ResourceRequest>();

  resource_request->url = url;
  resource_request->method = net::HttpRequestHeaders::kGetMethod;
  resource_request->load_flags = net::LOAD_DISABLE_CACHE;
  resource_request->headers.SetHeader(
      net::HttpRequestHeaders::kAuthorization,
      base::StringPrintf(kAuthorizationHeaderFormat, access_token.c_str()));
  resource_request->headers.SetHeader(net::HttpRequestHeaders::kContentType,
                                      kProtobufferContentType);
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  auto url_loader =
      network::SimpleURLLoader::Create(std::move(resource_request), annotation);
  return url_loader;
}

}  // namespace

UserCloudSigninRestrictionPolicyFetcher::
    UserCloudSigninRestrictionPolicyFetcher(
        policy::BrowserPolicyConnector* browser_policy_connector,
        scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : browser_policy_connector_(browser_policy_connector),
      url_loader_factory_(url_loader_factory) {}

UserCloudSigninRestrictionPolicyFetcher::
    ~UserCloudSigninRestrictionPolicyFetcher() = default;

void UserCloudSigninRestrictionPolicyFetcher::
    GetManagedAccountsSigninRestriction(
        signin::IdentityManager* identity_manager,
        const CoreAccountId& account_id,
        base::OnceCallback<void(const std::string&)> callback) {
  // base::Unretained is safe here because the callback is called in the
  // lifecycle of `this`.
  FetchAccessToken(
      identity_manager, account_id,
      base::BindOnce(&UserCloudSigninRestrictionPolicyFetcher::
                         GetManagedAccountsSigninRestrictionInternal,
                     base::Unretained(this), std::move(callback)));
}

void UserCloudSigninRestrictionPolicyFetcher::FetchAccessToken(
    signin::IdentityManager* identity_manager,
    const CoreAccountId& account_id,
    base::OnceCallback<void(const std::string&)> callback) {
  DCHECK(callback);
  DCHECK(!account_id.empty());
  DCHECK(identity_manager->HasAccountWithRefreshToken(account_id));
  DCHECK(!access_token_fetcher_);

  // base::Unretained is safe here because `access_token_fetcher_` is owned by
  // `this`.
  access_token_fetcher_ = identity_manager->CreateAccessTokenFetcherForAccount(
      account_id, /*oauth_consumer_name=*/"cloud_policy", /*scopes=*/
      {GaiaConstants::kGoogleUserInfoProfile},
      base::BindOnce(
          &UserCloudSigninRestrictionPolicyFetcher::OnFetchAccessTokenResult,
          base::Unretained(this), std::move(callback)),
      signin::AccessTokenFetcher::Mode::kImmediate);
}

void UserCloudSigninRestrictionPolicyFetcher::OnFetchAccessTokenResult(
    base::OnceCallback<void(const std::string&)> callback,
    GoogleServiceAuthError error,
    signin::AccessTokenInfo token_info) {
  std::move(callback).Run(
      error.state() == GoogleServiceAuthError::NONE ? token_info.token : "");
}

void UserCloudSigninRestrictionPolicyFetcher::
    GetManagedAccountsSigninRestrictionInternal(
        base::OnceCallback<void(const std::string&)> callback,
        const std::string& access_token) {
  net::NetworkTrafficAnnotationTag annotation =
      net::DefineNetworkTrafficAnnotation(
          "managed_acccount_signin_restrictions_secure_connect", R"(
    semantics {
      sender: "SecureConnect Service"
      description:
        "A request to the SecureConnect API to retrieve the value of the "
        "ManagedAccountsSigninRestriction policy for the signed in user."
      trigger:
        "After a user signs into a managed account and if they have not "
        "explicitely accepted to use a managed profile and no value for the "
        "profile separation is not enforced by any machine level policy."
      data:
        "Gaia access token."
      destination: GOOGLE_OWNED_SERVICE
    }
    policy {
      cookies_allowed: NO
      chrome_policy {
        SigninInterceptionEnabled {
          SigninInterceptionEnabled: false
        }
      }
    })");

  // Each url loader can only be used for one request.
  url_loader_ =
      CreateUrlLoader(GetSecureConnectApiGetAccountSigninRestrictionUrl(),
                      access_token, annotation);

  // base::Unretained is safe here because `url_loader_` is owned by `this`.
  url_loader_->DownloadToString(
      url_loader_factory_for_testing_ == nullptr
          ? url_loader_factory_.get()
          : url_loader_factory_for_testing_,
      base::BindOnce(&UserCloudSigninRestrictionPolicyFetcher::
                         OnManagedAccountsSigninRestrictionResult,
                     base::Unretained(this), std::move(callback)),
      1024 * 1024 /* 1 MiB */);
}

void UserCloudSigninRestrictionPolicyFetcher::
    OnManagedAccountsSigninRestrictionResult(
        base::OnceCallback<void(const std::string&)> callback,
        std::unique_ptr<std::string> response_body) {
  std::string restriction;
  std::unique_ptr<network::SimpleURLLoader> url_loader = std::move(url_loader_);

  // TODO (crbug/1261474): Add metrics for the failure rate.
  GoogleServiceAuthError error = GoogleServiceAuthError::AuthErrorNone();
  if (url_loader->NetError() != net::OK) {
    if (url_loader->ResponseInfo() && url_loader->ResponseInfo()->headers) {
      int response_code = url_loader->ResponseInfo()->headers->response_code();
      LOG(WARNING)
          << "ManagedAccountsSigninRestriction request failed with HTTP code: "
          << response_code;
    } else {
      error =
          GoogleServiceAuthError::FromConnectionError(url_loader->NetError());
      LOG(WARNING)
          << "ManagedAccountsSigninRestriction request failed with error: "
          << url_loader->NetError();
    }
  }

  if (error.state() == GoogleServiceAuthError::NONE) {
    enterprise_management::GetManagedAccountsSigninRestrictionResponse response;
    if (response_body && response.ParseFromString(*response_body) &&
        (!response.has_has_error() || !response.has_error())) {
      restriction = response.policy_value();
    } else {
      LOG(WARNING) << "Failed to ManagedAccountsSigninRestriction response";
    }
  }

  std::move(callback).Run(std::move(restriction));
}

GURL UserCloudSigninRestrictionPolicyFetcher::
    GetSecureConnectApiGetAccountSigninRestrictionUrl() const {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  std::string url =
      command_line->HasSwitch(policy::switches::kSecureConnectApiUrl) &&
              browser_policy_connector_ &&
              browser_policy_connector_->IsCommandLineSwitchSupported()
          ? command_line->GetSwitchValueASCII(
                policy::switches::kSecureConnectApiUrl)
          : kSecureConnectApiGetManagedAccountsSigninRestrictionsUrl;
  return GURL(url);
}

}  //  namespace policy
