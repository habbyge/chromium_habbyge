// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/strings/string_piece.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/values.h"
#include "build/build_config.h"
#include "components/autofill/core/browser/autofill_experiments.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/payments/credit_card_save_manager.h"
#include "components/autofill/core/browser/payments/local_card_migration_manager.h"
#include "components/autofill/core/browser/payments/payments_client.h"
#include "components/autofill/core/browser/test_personal_data_manager.h"
#include "components/autofill/core/common/autofill_clock.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "components/autofill/core/common/autofill_switches.h"
#include "components/signin/public/identity_manager/identity_test_environment.h"
#include "components/variations/net/variations_http_headers.h"
#include "components/variations/scoped_variations_ids_provider.h"
#include "components/variations/variations_associated_data.h"
#include "components/variations/variations_ids_provider.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace autofill {
namespace payments {
namespace {

int kAllDetectableValues =
    CreditCardSaveManager::DetectedValue::CVC |
    CreditCardSaveManager::DetectedValue::CARDHOLDER_NAME |
    CreditCardSaveManager::DetectedValue::ADDRESS_NAME |
    CreditCardSaveManager::DetectedValue::ADDRESS_LINE |
    CreditCardSaveManager::DetectedValue::LOCALITY |
    CreditCardSaveManager::DetectedValue::ADMINISTRATIVE_AREA |
    CreditCardSaveManager::DetectedValue::POSTAL_CODE |
    CreditCardSaveManager::DetectedValue::COUNTRY_CODE |
    CreditCardSaveManager::DetectedValue::HAS_GOOGLE_PAYMENTS_ACCOUNT;

struct CardUnmaskOptions {
  CardUnmaskOptions& with_fido() {
    use_fido = true;
    use_cvc = false;
    return *this;
  }

  CardUnmaskOptions& with_cvc(std::string c) {
    use_cvc = true;
    cvc = c;
    return *this;
  }

  CardUnmaskOptions& with_virtual_card() {
    virtual_card = true;
    return *this;
  }

  CardUnmaskOptions& with_virtual_card_risk_based() {
    with_virtual_card();
    use_cvc = false;
    return *this;
  }

  CardUnmaskOptions& with_virtual_card_risk_based_then_fido() {
    with_virtual_card();
    use_fido = true;
    use_cvc = false;
    set_context_token = true;
    return *this;
  }

  CardUnmaskOptions& with_virtual_card_risk_based_then_otp(std::string o) {
    with_virtual_card();
    use_otp = true;
    use_cvc = false;
    set_context_token = true;
    otp = o;
    return *this;
  }

  // By default, use cvc authentication.
  bool use_cvc = true;
  // If true, use FIDO authentication.
  bool use_fido = false;
  // If true, use otp authentication.
  bool use_otp = false;
  // If CVC authentication is chosen, default CVC value the user entered, to be
  // sent to Google Payments.
  std::string cvc = "123";
  // If OTP authentication is chosen, default OTP value the user entered.
  std::string otp = "654321";
  // If true, mock that the unmasking is for a virtual card.
  bool virtual_card = false;
  // If true, set context_token in the request.
  bool set_context_token = true;
};

}  // namespace

class PaymentsClientTest : public testing::Test {
 public:
  PaymentsClientTest() = default;

  PaymentsClientTest(const PaymentsClientTest&) = delete;
  PaymentsClientTest& operator=(const PaymentsClientTest&) = delete;

  ~PaymentsClientTest() override = default;

  void SetUp() override {
    // Silence the warning for mismatching sync and Payments servers.
    base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
        switches::kWalletServiceUseSandbox, "0");

    result_ = AutofillClient::PaymentsRpcResult::kNone;
    server_id_.clear();
    unmask_response_details_ = nullptr;
    legal_message_.reset();
    has_variations_header_ = false;

    factory()->SetInterceptor(base::BindLambdaForTesting(
        [&](const network::ResourceRequest& request) {
          intercepted_headers_ = request.headers;
          intercepted_body_ = network::GetUploadData(request);
          has_variations_header_ = variations::HasVariationsHeader(request);
        }));
    test_shared_loader_factory_ =
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &test_url_loader_factory_);
    client_ = std::make_unique<PaymentsClient>(
        test_shared_loader_factory_, identity_test_env_.identity_manager(),
        &test_personal_data_);
    test_personal_data_.SetAccountInfoForPayments(
        identity_test_env_.MakePrimaryAccountAvailable(
            "example@gmail.com", signin::ConsentLevel::kSync));
    scoped_feature_list_.InitAndEnableFeature(
        features::kAutofillEnableVirtualCardsRiskBasedAuthentication);
  }

  void TearDown() override { client_.reset(); }

  // Registers a field trial with the specified name and group and an associated
  // google web property variation id.
  void CreateFieldTrialWithId(const std::string& trial_name,
                              const std::string& group_name,
                              int variation_id) {
    variations::AssociateGoogleVariationID(
        variations::GOOGLE_WEB_PROPERTIES_ANY_CONTEXT, trial_name, group_name,
        static_cast<variations::VariationID>(variation_id));
    base::FieldTrialList::CreateFieldTrial(trial_name, group_name)->group();
  }

  void OnDidGetUnmaskDetails(
      AutofillClient::PaymentsRpcResult result,
      payments::PaymentsClient::UnmaskDetails& unmask_details) {
    result_ = result;
    unmask_details_ = &unmask_details;
  }

  void OnDidGetRealPan(AutofillClient::PaymentsRpcResult result,
                       PaymentsClient::UnmaskResponseDetails& response) {
    result_ = result;
    unmask_response_details_ = &response;
  }

  void OnDidGetOptChangeResult(
      AutofillClient::PaymentsRpcResult result,
      PaymentsClient::OptChangeResponseDetails& response) {
    result_ = result;
    opt_change_response_.user_is_opted_in = response.user_is_opted_in;
    opt_change_response_.fido_creation_options =
        std::move(response.fido_creation_options);
    opt_change_response_.fido_request_options =
        std::move(response.fido_request_options);
  }

  void OnDidGetUploadDetails(
      AutofillClient::PaymentsRpcResult result,
      const std::u16string& context_token,
      std::unique_ptr<base::Value> legal_message,
      std::vector<std::pair<int, int>> supported_card_bin_ranges) {
    result_ = result;
    legal_message_ = std::move(legal_message);
    supported_card_bin_ranges_ = supported_card_bin_ranges;
  }

  void OnDidUploadCard(AutofillClient::PaymentsRpcResult result,
                       const std::string& server_id) {
    result_ = result;
    server_id_ = server_id;
  }

#if !defined(OS_ANDROID) && !defined(OS_IOS)
  void OnDidMigrateLocalCards(
      AutofillClient::PaymentsRpcResult result,
      std::unique_ptr<std::unordered_map<std::string, std::string>>
          migration_save_results,
      const std::string& display_text) {
    result_ = result;
    migration_save_results_ = std::move(migration_save_results);
    display_text_ = display_text;
  }
#endif  // !defined(OS_ANDROID) && !defined(OS_IOS)

  void OnDidSelectChallengeOption(AutofillClient::PaymentsRpcResult result,
                                  const std::string& updated_context_token) {
    result_ = result;
    context_token_ = updated_context_token;
  }

 protected:
  base::test::ScopedFeatureList scoped_feature_list_;

  // Issue a GetUnmaskDetails request. This requires an OAuth token before
  // starting the request.
  void StartGettingUnmaskDetails() {
    client_->GetUnmaskDetails(
        base::BindOnce(&PaymentsClientTest::OnDidGetUnmaskDetails,
                       weak_ptr_factory_.GetWeakPtr()),
        "language-LOCALE");
  }

  // Issue an UnmaskCard request. This requires an OAuth token before starting
  // the request.
  void StartUnmasking(CardUnmaskOptions options) {
    PaymentsClient::UnmaskRequestDetails request_details;
    request_details.billing_customer_number = 111222333444;

    request_details.card = test::GetMaskedServerCard();
    request_details.risk_data = "some risk data";
    if (options.use_fido) {
      request_details.fido_assertion_info =
          base::Value(base::Value::Type::DICTIONARY);
    }
    if (options.use_cvc)
      request_details.user_response.cvc = base::ASCIIToUTF16(options.cvc);
    if (options.virtual_card) {
      request_details.card.set_record_type(CreditCard::VIRTUAL_CARD);
      request_details.last_committed_url_origin =
          GURL("https://www.example.com");
    }
    if (options.set_context_token)
      request_details.context_token = "fake context token";
    if (options.use_otp)
      request_details.otp = base::ASCIIToUTF16(options.otp);
    client_->UnmaskCard(request_details,
                        base::BindOnce(&PaymentsClientTest::OnDidGetRealPan,
                                       weak_ptr_factory_.GetWeakPtr()));
  }

  // If |opt_in| is set to true, then opts the user in to use FIDO
  // authentication for card unmasking. Otherwise opts the user out.
  void StartOptChangeRequest(
      PaymentsClient::OptChangeRequestDetails::Reason reason) {
    PaymentsClient::OptChangeRequestDetails request_details;
    request_details.reason = reason;
    client_->OptChange(
        request_details,
        base::BindOnce(&PaymentsClientTest::OnDidGetOptChangeResult,
                       weak_ptr_factory_.GetWeakPtr()));
  }

  // Issue a GetUploadDetails request.
  void StartGettingUploadDetails(
      PaymentsClient::UploadCardSource upload_card_source =
          PaymentsClient::UploadCardSource::UNKNOWN_UPLOAD_CARD_SOURCE) {
    client_->GetUploadDetails(
        BuildTestProfiles(), kAllDetectableValues, std::vector<const char*>(),
        "language-LOCALE",
        base::BindOnce(&PaymentsClientTest::OnDidGetUploadDetails,
                       weak_ptr_factory_.GetWeakPtr()),
        /*billable_service_number=*/12345, upload_card_source);
  }

  // Issue an UploadCard request. This requires an OAuth token before starting
  // the request.
  void StartUploading(bool include_cvc, bool include_nickname = false) {
    PaymentsClient::UploadRequestDetails request_details;
    request_details.billing_customer_number = 111222333444;
    request_details.card = test::GetCreditCard();
    if (include_cvc)
      request_details.cvc = u"123";
    if (include_nickname) {
      upstream_nickname_ = u"grocery";
      request_details.card.SetNickname(upstream_nickname_);
    }
    request_details.context_token = u"context token";
    request_details.risk_data = "some risk data";
    request_details.app_locale = "language-LOCALE";
    request_details.profiles = BuildTestProfiles();
    client_->UploadCard(request_details,
                        base::BindOnce(&PaymentsClientTest::OnDidUploadCard,
                                       weak_ptr_factory_.GetWeakPtr()));
  }

#if !defined(OS_ANDROID) && !defined(OS_IOS)
  void StartMigrating(bool has_cardholder_name,
                      bool set_nickname_for_first_card = false) {
    PaymentsClient::MigrationRequestDetails request_details;
    request_details.context_token = u"context token";
    request_details.risk_data = "some risk data";
    request_details.app_locale = "language-LOCALE";

    migratable_credit_cards_.clear();
    CreditCard card1 = test::GetCreditCard();
    if (set_nickname_for_first_card)
      card1.SetNickname(u"grocery");
    CreditCard card2 = test::GetCreditCard2();
    if (!has_cardholder_name) {
      card1.SetRawInfo(CREDIT_CARD_NAME_FULL, u"");
      card2.SetRawInfo(CREDIT_CARD_NAME_FULL, u"");
    }
    migratable_credit_cards_.push_back(MigratableCreditCard(card1));
    migratable_credit_cards_.push_back(MigratableCreditCard(card2));
    client_->MigrateCards(
        request_details, migratable_credit_cards_,
        base::BindOnce(&PaymentsClientTest::OnDidMigrateLocalCards,
                       weak_ptr_factory_.GetWeakPtr()));
  }
#endif  // !defined(OS_ANDROID) && !defined(OS_IOS)

  void StartSelectingChallengeOption(
      CardUnmaskChallengeOptionType challenge_type =
          CardUnmaskChallengeOptionType::kSmsOtp,
      std::string challenge_id = "arbitrary id") {
    PaymentsClient::SelectChallengeOptionRequestDetails request_details;
    request_details.billing_customer_number = 555666777888;
    request_details.context_token = "fake context token";

    CardUnmaskChallengeOption selected_challenge_option;
    selected_challenge_option.type = challenge_type;
    selected_challenge_option.id = challenge_id;
    selected_challenge_option.challenge_info = u"(***)-***-5678";
    request_details.selected_challenge_option = selected_challenge_option;

    client_->SelectChallengeOption(
        request_details,
        base::BindOnce(&PaymentsClientTest::OnDidSelectChallengeOption,
                       weak_ptr_factory_.GetWeakPtr()));
  }

  network::TestURLLoaderFactory* factory() { return &test_url_loader_factory_; }

  const std::string& GetUploadData() { return intercepted_body_; }

  bool HasVariationsHeader() { return has_variations_header_; }

  // Issues access token in response to any access token request. This will
  // start the Payments Request which requires the authentication.
  void IssueOAuthToken() {
    identity_test_env_.WaitForAccessTokenRequestIfNecessaryAndRespondWithToken(
        "totally_real_token", AutofillClock::Now() + base::Days(10));

    // Verify the auth header.
    std::string auth_header_value;
    EXPECT_TRUE(intercepted_headers_.GetHeader(
        net::HttpRequestHeaders::kAuthorization, &auth_header_value))
        << intercepted_headers_.ToString();
    EXPECT_EQ("Bearer totally_real_token", auth_header_value);
  }

  void ReturnResponse(net::HttpStatusCode response_code,
                      const std::string& response_body) {
    client_->OnSimpleLoaderCompleteInternal(response_code, response_body);
  }

  void assertCvcIncludedInRequest(std::string cvc) {
    EXPECT_TRUE(!GetUploadData().empty());
    // Verify that the encrypted_cvc and s7e_13_cvc parameters were both
    // included in the request.
    EXPECT_TRUE(GetUploadData().find("encrypted_cvc") != std::string::npos);
    EXPECT_TRUE(GetUploadData().find("__param:s7e_13_cvc") !=
                std::string::npos);
    EXPECT_TRUE(GetUploadData().find("&s7e_13_cvc=" + cvc) !=
                std::string::npos);
  }

  void assertOtpIncludedInRequest(std::string otp) {
    EXPECT_TRUE(!GetUploadData().empty());
    // Verify that the otp and s7e_263_otp parameters were both included in the
    // request.
    EXPECT_TRUE(GetUploadData().find("otp") != std::string::npos);
    EXPECT_TRUE(GetUploadData().find("__param:s7e_263_otp") !=
                std::string::npos);
    EXPECT_TRUE(GetUploadData().find("&s7e_263_otp=" + otp) !=
                std::string::npos);
  }

  void assertCvcNotIncludedInRequest() {
    EXPECT_TRUE(!GetUploadData().empty());
    // Verify that the encrypted_cvc and s7e_13_cvc parameters were NOT included
    // in the request.
    EXPECT_TRUE(GetUploadData().find("encrypted_cvc") == std::string::npos);
    EXPECT_TRUE(GetUploadData().find("__param:s7e_13_cvc") ==
                std::string::npos);
    EXPECT_TRUE(GetUploadData().find("&s7e_13_cvc=") == std::string::npos);
  }

  void assertOtpNotIncludedInRequest() {
    EXPECT_TRUE(!GetUploadData().empty());
    // Verify that the otp and s7e_263_otp parameters were NOT included in the
    // request.
    EXPECT_TRUE(GetUploadData().find("otp") == std::string::npos);
    EXPECT_TRUE(GetUploadData().find("__param:s7e_263_otp") ==
                std::string::npos);
    EXPECT_TRUE(GetUploadData().find("&s7e_263_otp=") == std::string::npos);
  }

  void assertIncludedInRequest(std::string field_name_or_value) {
    EXPECT_TRUE(GetUploadData().find(field_name_or_value) != std::string::npos);
  }

  void assertNotIncludedInRequest(std::string field_name_or_value) {
    EXPECT_TRUE(GetUploadData().find(field_name_or_value) == std::string::npos);
  }

  AutofillClient::PaymentsRpcResult result_ =
      AutofillClient::PaymentsRpcResult::kNone;
  payments::PaymentsClient::UnmaskDetails* unmask_details_;

  // Server ID of a saved card via credit card upload save.
  std::string server_id_;
  // The OptChangeResponseDetails retrieved from an OptChangeRequest.
  PaymentsClient::OptChangeResponseDetails opt_change_response_;
  // The UnmaskResponseDetails retrieved from an UnmaskRequest.  Includes PAN.
  PaymentsClient::UnmaskResponseDetails* unmask_response_details_ = nullptr;
  // The legal message returned from a GetDetails upload save preflight call.
  std::unique_ptr<base::Value> legal_message_;
  // A list of card BIN ranges supported by Google Payments, returned from a
  // GetDetails upload save preflight call.
  std::vector<std::pair<int, int>> supported_card_bin_ranges_;
  // The nickname name in the UploadRequest that was supposed to be saved.
  std::u16string upstream_nickname_;
  // The opaque token used to chain consecutive payments requests together.
  std::string context_token_;

#if !defined(OS_ANDROID) && !defined(OS_IOS)
  // Credit cards to be upload saved during a local credit card migration call.
  std::vector<MigratableCreditCard> migratable_credit_cards_;
  // A mapping of results from a local credit card migration call.
  std::unique_ptr<std::unordered_map<std::string, std::string>>
      migration_save_results_;
  // A tip message to be displayed during local card migration.
  std::string display_text_;
#endif  // !defined(OS_ANDROID) && !defined(OS_IOS)

  base::test::TaskEnvironment task_environment_;
  variations::ScopedVariationsIdsProvider scoped_variations_ids_provider_{
      variations::VariationsIdsProvider::Mode::kUseSignedInState};
  network::TestURLLoaderFactory test_url_loader_factory_;
  scoped_refptr<network::SharedURLLoaderFactory> test_shared_loader_factory_;
  TestPersonalDataManager test_personal_data_;
  std::unique_ptr<PaymentsClient> client_;
  signin::IdentityTestEnvironment identity_test_env_;

  net::HttpRequestHeaders intercepted_headers_;
  bool has_variations_header_;
  std::string intercepted_body_;
  base::WeakPtrFactory<PaymentsClientTest> weak_ptr_factory_{this};

 private:
  std::vector<AutofillProfile> BuildTestProfiles() {
    std::vector<AutofillProfile> profiles;
    profiles.push_back(BuildProfile("John", "Smith", "1234 Main St.", "Miami",
                                    "FL", "32006", "212-555-0162"));
    profiles.push_back(BuildProfile("Pat", "Jones", "432 Oak Lane", "Lincoln",
                                    "OH", "43005", "(834)555-0090"));
    return profiles;
  }

  AutofillProfile BuildProfile(base::StringPiece first_name,
                               base::StringPiece last_name,
                               base::StringPiece address_line,
                               base::StringPiece city,
                               base::StringPiece state,
                               base::StringPiece zip,
                               base::StringPiece phone_number) {
    AutofillProfile profile;

    profile.SetInfo(NAME_FIRST, base::ASCIIToUTF16(first_name), "en-US");
    profile.SetInfo(NAME_LAST, base::ASCIIToUTF16(last_name), "en-US");
    profile.SetInfo(ADDRESS_HOME_LINE1, base::ASCIIToUTF16(address_line),
                    "en-US");
    profile.SetInfo(ADDRESS_HOME_CITY, base::ASCIIToUTF16(city), "en-US");
    profile.SetInfo(ADDRESS_HOME_STATE, base::ASCIIToUTF16(state), "en-US");
    profile.SetInfo(ADDRESS_HOME_ZIP, base::ASCIIToUTF16(zip), "en-US");
    profile.SetInfo(PHONE_HOME_WHOLE_NUMBER, base::ASCIIToUTF16(phone_number),
                    "en-US");
    profile.FinalizeAfterImport();
    return profile;
  }
};

TEST_F(PaymentsClientTest, GetUnmaskDetailsSuccess) {
  StartGettingUnmaskDetails();
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"offer_fido_opt_in\": \"false\", "
                 "\"authentication_method\": \"CVC\" }");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ(false, unmask_details_->offer_fido_opt_in);
  EXPECT_EQ(AutofillClient::UnmaskAuthMethod::kCvc,
            unmask_details_->unmask_auth_method);
}

TEST_F(PaymentsClientTest, GetUnmaskDetailsIncludesChromeUserContext) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndDisableFeature(
      features::kAutofillEnableAccountWalletStorage);

  StartGettingUnmaskDetails();
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK, "{}");

  // ChromeUserContext was set.
  EXPECT_TRUE(GetUploadData().find("chrome_user_context") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("full_sync_enabled") != std::string::npos);
}

TEST_F(PaymentsClientTest, OAuthError) {
  StartUnmasking(CardUnmaskOptions());
  identity_test_env_.WaitForAccessTokenRequestIfNecessaryAndRespondWithError(
      GoogleServiceAuthError(GoogleServiceAuthError::SERVICE_UNAVAILABLE));
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kPermanentFailure, result_);
  EXPECT_TRUE(unmask_response_details_->real_pan.empty());
}

TEST_F(PaymentsClientTest,
       UnmaskRequestIncludesBillingCustomerNumberInRequest) {
  StartUnmasking(CardUnmaskOptions());
  IssueOAuthToken();

  // Verify that the billing customer number is included in the request.
  EXPECT_TRUE(
      GetUploadData().find("%22external_customer_id%22:%22111222333444%22") !=
      std::string::npos);
}

TEST_F(PaymentsClientTest, UnmaskSuccessViaCVC) {
  StartUnmasking(CardUnmaskOptions().with_cvc("111"));
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK, "{ \"pan\": \"1234\" }");

  assertCvcIncludedInRequest("111");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ("1234", unmask_response_details_->real_pan);
}

TEST_F(PaymentsClientTest, UnmaskSuccessViaFIDO) {
  StartUnmasking(CardUnmaskOptions().with_fido());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK, "{ \"pan\": \"1234\" }");

  assertCvcNotIncludedInRequest();
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ("1234", unmask_response_details_->real_pan);
}

TEST_F(PaymentsClientTest, UnmaskSuccessViaCVCWithCreationOptions) {
  StartUnmasking(CardUnmaskOptions().with_cvc("111"));
  IssueOAuthToken();
  ReturnResponse(
      net::HTTP_OK,
      "{ \"pan\": \"1234\", \"dcvv\": \"321\", \"fido_creation_options\": "
      "{\"relying_party_id\": \"google.com\"}}");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ("1234", unmask_response_details_->real_pan);
  EXPECT_EQ("321", unmask_response_details_->dcvv);
  EXPECT_EQ("google.com",
            *unmask_response_details_->fido_creation_options->FindStringKey(
                "relying_party_id"));
}

TEST_F(PaymentsClientTest, UnmaskSuccessAccountFromSyncTest) {
  StartUnmasking(CardUnmaskOptions());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK, "{ \"pan\": \"1234\" }");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ("1234", unmask_response_details_->real_pan);
}

TEST_F(PaymentsClientTest, UnmaskSuccessWithVirtualCardCvcAuth) {
  StartUnmasking(CardUnmaskOptions().with_virtual_card().with_cvc("222"));
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"pan\": \"4111111111111111\", \"dcvv\": \"999\",  "
                 "\"expiration\": { \"month\":12, \"year\":2099 } }");

  assertCvcIncludedInRequest("222");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ("4111111111111111", unmask_response_details_->real_pan);
  EXPECT_EQ("999", unmask_response_details_->dcvv);
  EXPECT_EQ("12", unmask_response_details_->expiration_month);
  EXPECT_EQ("2099", unmask_response_details_->expiration_year);
}

TEST_F(PaymentsClientTest, UnmaskSuccessWithVirtualCardFidoAuth) {
  StartUnmasking(CardUnmaskOptions().with_virtual_card().with_fido());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"pan\": \"4111111111111111\", \"dcvv\": \"999\",  "
                 "\"expiration\": { \"month\":12, \"year\":2099 } }");

  assertCvcNotIncludedInRequest();
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ("4111111111111111", unmask_response_details_->real_pan);
  EXPECT_EQ("999", unmask_response_details_->dcvv);
  EXPECT_EQ("12", unmask_response_details_->expiration_month);
  EXPECT_EQ("2099", unmask_response_details_->expiration_year);
}

TEST_F(PaymentsClientTest, VirtualCardRiskBasedGreenPathResponse) {
  StartUnmasking(CardUnmaskOptions().with_virtual_card_risk_based());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"pan\": \"4111111111111111\", \"dcvv\": \"999\",  "
                 "\"expiration\": { \"month\":12, \"year\":2099 } }");

  // Verify that Cvc/FIDO/OTP are not included in the request.
  assertCvcNotIncludedInRequest();
  assertOtpNotIncludedInRequest();
  EXPECT_TRUE(GetUploadData().find("fido_assertion_info") == std::string::npos);
  // Only merchant_domain is included.
  EXPECT_TRUE(GetUploadData().find("merchant_domain") != std::string::npos);

  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ("4111111111111111", unmask_response_details_->real_pan);
  EXPECT_EQ("999", unmask_response_details_->dcvv);
  EXPECT_EQ("12", unmask_response_details_->expiration_month);
  EXPECT_EQ("2099", unmask_response_details_->expiration_year);
  EXPECT_TRUE(unmask_response_details_->card_unmask_challenge_options.empty());
}

TEST_F(PaymentsClientTest, VirtualCardRiskBasedRedPathResponse_Error) {
  StartUnmasking(CardUnmaskOptions().with_virtual_card_risk_based());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"error\": { \"code\": \"NON-INTERNAL\", "
                 "\"api_error_reason\": \"virtual_card_permanent_error\"} }");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kVcnRetrievalPermanentFailure,
            result_);
}

TEST_F(PaymentsClientTest,
       VirtualCardRiskBasedRedPathResponse_NoOptionProvided) {
  StartUnmasking(CardUnmaskOptions().with_virtual_card_risk_based());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK, "{ \"context_token\": \"fake_context_token\" }");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kPermanentFailure, result_);
}

TEST_F(PaymentsClientTest, VirtualCardRiskBasedYellowPathResponse) {
  StartUnmasking(CardUnmaskOptions().with_virtual_card_risk_based());
  IssueOAuthToken();
  ReturnResponse(
      net::HTTP_OK,
      "{ \"fido_request_options\": { \"challenge\": \"fake_fido_challenge\" }, "
      "\"context_token\": \"fake_context_token\", \"idv_challenge_options\": "
      "[{ \"sms_otp_challenge_option\": { \"challenge_id\": "
      "\"fake_challenge_id_1\", \"masked_phone_number\": \"(***)-***-1234\" } "
      "}, { \"sms_otp_challenge_option\": { \"challenge_id\": "
      "\"fake_challenge_id_2\", \"masked_phone_number\": \"(***)-***-5678\" } "
      "}] }");

  // Ensure that it's not treated as failure when no pan is returned.
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ("fake_context_token", unmask_response_details_->context_token);
  // Verify the FIDO request challenge is correctly parsed.
  EXPECT_EQ("fake_fido_challenge",
            *unmask_response_details_->fido_request_options->FindStringKey(
                "challenge"));
  // Verify the two idv challenge options are both sms challenge and fields can
  // be correctly parsed.
  EXPECT_EQ(2u, unmask_response_details_->card_unmask_challenge_options.size());
  const CardUnmaskChallengeOption& challenge_option_1 =
      unmask_response_details_->card_unmask_challenge_options[0];
  EXPECT_EQ(CardUnmaskChallengeOptionType::kSmsOtp, challenge_option_1.type);
  EXPECT_EQ("fake_challenge_id_1", challenge_option_1.id);
  EXPECT_EQ(u"(***)-***-1234", challenge_option_1.challenge_info);
  const CardUnmaskChallengeOption& challenge_option_2 =
      unmask_response_details_->card_unmask_challenge_options[1];
  EXPECT_EQ(CardUnmaskChallengeOptionType::kSmsOtp, challenge_option_2.type);
  EXPECT_EQ("fake_challenge_id_2", challenge_option_2.id);
  EXPECT_EQ(u"(***)-***-5678", challenge_option_2.challenge_info);
}

TEST_F(PaymentsClientTest,
       VirtualCardRiskBasedYellowPathResponseWithUnknownType) {
  StartUnmasking(CardUnmaskOptions().with_virtual_card_risk_based());
  IssueOAuthToken();
  ReturnResponse(
      net::HTTP_OK,
      "{ \"fido_request_options\": { \"challenge\": \"fake_fido_challenge\" }, "
      "\"context_token\": \"fake_context_token\", \"idv_challenge_options\": "
      "[{ \"sms_otp_challenge_option\": { \"challenge_id\": "
      "\"fake_challenge_id_1\", \"masked_phone_number\": \"(***)-***-1234\" } "
      "}, { \"unknown_new_challenge_option\": { \"challenge_id\": "
      "\"fake_challenge_id_2\" } }] }");

  // Ensure that it's not treated as failure when no pan is returned.
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ("fake_context_token", unmask_response_details_->context_token);
  // Verify the FIDO request challenge is correctly parsed.
  EXPECT_EQ("fake_fido_challenge",
            *unmask_response_details_->fido_request_options->FindStringKey(
                "challenge"));
  // Verify that the unknow new challenge option type won't break the parsing.
  // We ignore the unknown new type, and only return the supported challenge
  // option.
  EXPECT_EQ(1u, unmask_response_details_->card_unmask_challenge_options.size());
  const CardUnmaskChallengeOption& sms_challenge_option =
      unmask_response_details_->card_unmask_challenge_options[0];
  EXPECT_EQ(CardUnmaskChallengeOptionType::kSmsOtp, sms_challenge_option.type);
  EXPECT_EQ("fake_challenge_id_1", sms_challenge_option.id);
  EXPECT_EQ(u"(***)-***-1234", sms_challenge_option.challenge_info);
}

TEST_F(PaymentsClientTest, VirtualCardRiskBasedThenFido) {
  StartUnmasking(CardUnmaskOptions().with_virtual_card_risk_based_then_fido());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"pan\": \"4111111111111111\", \"dcvv\": \"999\",  "
                 "\"expiration\": { \"month\":12, \"year\":2099 } }");

  // Verify that Cvc/OTP are not included in the request.
  assertCvcNotIncludedInRequest();
  assertOtpNotIncludedInRequest();
  // Verify the fido assertion and context token is included.
  EXPECT_TRUE(GetUploadData().find("fido_assertion_info") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("context_token") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("merchant_domain") != std::string::npos);

  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ("4111111111111111", unmask_response_details_->real_pan);
  EXPECT_EQ("999", unmask_response_details_->dcvv);
  EXPECT_EQ("12", unmask_response_details_->expiration_month);
  EXPECT_EQ("2099", unmask_response_details_->expiration_year);
}

TEST_F(PaymentsClientTest, VirtualCardRiskBasedThenOtpSuccess) {
  const std::string otp = "111111";
  StartUnmasking(
      CardUnmaskOptions().with_virtual_card_risk_based_then_otp(otp));
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"pan\": \"4111111111111111\", \"dcvv\": \"999\",  "
                 "\"expiration\": { \"month\":12, \"year\":2099 } }");

  assertOtpIncludedInRequest(otp);
  // Verify that Cvc/FIDO are not included in the request.
  assertCvcNotIncludedInRequest();
  EXPECT_TRUE(GetUploadData().find("fido_assertion_info") == std::string::npos);
  // Verify the context token is also included.
  EXPECT_TRUE(GetUploadData().find("context_token") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("merchant_domain") != std::string::npos);

  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ("4111111111111111", unmask_response_details_->real_pan);
  EXPECT_EQ("999", unmask_response_details_->dcvv);
  EXPECT_EQ("12", unmask_response_details_->expiration_month);
  EXPECT_EQ("2099", unmask_response_details_->expiration_year);
}

TEST_F(PaymentsClientTest, ExpiredOtp) {
  const std::string otp = "222222";
  StartUnmasking(
      CardUnmaskOptions().with_virtual_card_risk_based_then_otp(otp));
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"context_token\": \"fake_context_token\", "
                 "\"flow_status\": \"FLOW_STATUS_EXPIRED_OTP\" }");

  assertOtpIncludedInRequest(otp);
  // Verify the context token is also included.
  EXPECT_TRUE(GetUploadData().find("context_token") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("merchant_domain") != std::string::npos);

  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ("FLOW_STATUS_EXPIRED_OTP", unmask_response_details_->flow_status);
}

TEST_F(PaymentsClientTest, IncorrectOtp) {
  const std::string otp = "333333";
  StartUnmasking(
      CardUnmaskOptions().with_virtual_card_risk_based_then_otp(otp));
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"context_token\": \"fake_context_token\", "
                 "\"flow_status\": \"FLOW_STATUS_INCORRECT_OTP\" }");

  assertOtpIncludedInRequest(otp);
  // Verify the context token is also included.
  EXPECT_TRUE(GetUploadData().find("context_token") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("merchant_domain") != std::string::npos);

  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ("FLOW_STATUS_INCORRECT_OTP", unmask_response_details_->flow_status);
}

TEST_F(PaymentsClientTest, UnmaskIncludesChromeUserContext) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndDisableFeature(
      features::kAutofillEnableAccountWalletStorage);

  StartUnmasking(CardUnmaskOptions());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK, "{}");

  // ChromeUserContext was set.
  EXPECT_TRUE(GetUploadData().find("chrome_user_context") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("full_sync_enabled") != std::string::npos);
}

TEST_F(PaymentsClientTest,
       UnmaskIncludesChromeUserContextIfWalletStorageFlagEnabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(
      features::kAutofillEnableAccountWalletStorage);

  StartUnmasking(CardUnmaskOptions());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK, "{}");

  // ChromeUserContext was set.
  EXPECT_TRUE(GetUploadData().find("chrome_user_context") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("full_sync_enabled") != std::string::npos);
}

TEST_F(PaymentsClientTest, UnmaskIncludesMerchantDomain) {
  StartUnmasking(CardUnmaskOptions().with_virtual_card());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK, "{}");

  // last_committed_url_origin was set.
  EXPECT_TRUE(GetUploadData().find("merchant_domain") != std::string::npos);
}

TEST_F(PaymentsClientTest, OptInSuccess) {
  StartOptChangeRequest(
      PaymentsClient::OptChangeRequestDetails::ENABLE_FIDO_AUTH);
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"fido_authentication_info\": { \"user_status\": "
                 "\"FIDO_AUTH_ENABLED\"}}");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_TRUE(opt_change_response_.user_is_opted_in.value());
}

TEST_F(PaymentsClientTest, OptInServerUnresponsive) {
  StartOptChangeRequest(
      PaymentsClient::OptChangeRequestDetails::ENABLE_FIDO_AUTH);
  IssueOAuthToken();
  ReturnResponse(net::HTTP_REQUEST_TIMEOUT, "");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kNetworkError, result_);
  EXPECT_FALSE(opt_change_response_.user_is_opted_in.has_value());
}

TEST_F(PaymentsClientTest, OptOutSuccess) {
  StartOptChangeRequest(
      PaymentsClient::OptChangeRequestDetails::DISABLE_FIDO_AUTH);
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"fido_authentication_info\": { \"user_status\": "
                 "\"FIDO_AUTH_DISABLED\"}}");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_FALSE(opt_change_response_.user_is_opted_in.value());
}

TEST_F(PaymentsClientTest, EnrollAttemptReturnsCreationOptions) {
  StartOptChangeRequest(
      PaymentsClient::OptChangeRequestDetails::ENABLE_FIDO_AUTH);
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"fido_authentication_info\": { \"user_status\": "
                 "\"FIDO_AUTH_DISABLED\","
                 "\"fido_creation_options\": {"
                 "\"relying_party_id\": \"google.com\"}}}");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_FALSE(opt_change_response_.user_is_opted_in.value());
  EXPECT_EQ("google.com",
            *opt_change_response_.fido_creation_options->FindStringKey(
                "relying_party_id"));
}

TEST_F(PaymentsClientTest, GetDetailsSuccess) {
  StartGettingUploadDetails();
  ReturnResponse(
      net::HTTP_OK,
      "{ \"context_token\": \"some_token\", \"legal_message\": {} }");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_NE(nullptr, legal_message_.get());
}

TEST_F(PaymentsClientTest, GetDetailsRemovesNonLocationData) {
  StartGettingUploadDetails();

  // Verify that the recipient name field and test names appear nowhere in the
  // upload data.
  EXPECT_TRUE(GetUploadData().find(PaymentsClient::kRecipientName) ==
              std::string::npos);
  EXPECT_TRUE(GetUploadData().find("John") == std::string::npos);
  EXPECT_TRUE(GetUploadData().find("Smith") == std::string::npos);
  EXPECT_TRUE(GetUploadData().find("Pat") == std::string::npos);
  EXPECT_TRUE(GetUploadData().find("Jones") == std::string::npos);

  // Verify that the phone number field and test numbers appear nowhere in the
  // upload data.
  EXPECT_TRUE(GetUploadData().find(PaymentsClient::kPhoneNumber) ==
              std::string::npos);
  EXPECT_TRUE(GetUploadData().find("212") == std::string::npos);
  EXPECT_TRUE(GetUploadData().find("555") == std::string::npos);
  EXPECT_TRUE(GetUploadData().find("0162") == std::string::npos);
  EXPECT_TRUE(GetUploadData().find("834") == std::string::npos);
  EXPECT_TRUE(GetUploadData().find("0090") == std::string::npos);
}

TEST_F(PaymentsClientTest, GetDetailsIncludesDetectedValuesInRequest) {
  StartGettingUploadDetails();

  // Verify that the detected values were included in the request.
  std::string detected_values_string =
      "\"detected_values\":" + std::to_string(kAllDetectableValues);
  EXPECT_TRUE(GetUploadData().find(detected_values_string) !=
              std::string::npos);
}

TEST_F(PaymentsClientTest, GetDetailsIncludesChromeUserContext) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndDisableFeature(
      features::kAutofillEnableAccountWalletStorage);

  StartGettingUploadDetails();

  // ChromeUserContext was set.
  EXPECT_TRUE(GetUploadData().find("chrome_user_context") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("full_sync_enabled") != std::string::npos);
}

TEST_F(PaymentsClientTest,
       GetDetailsIncludesChromeUserContextIfWalletStorageFlagEnabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(
      features::kAutofillEnableAccountWalletStorage);

  StartGettingUploadDetails();

  // ChromeUserContext was set.
  EXPECT_TRUE(GetUploadData().find("chrome_user_context") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("full_sync_enabled") != std::string::npos);
}

TEST_F(PaymentsClientTest,
       GetDetailsIncludesUpstreamCheckoutFlowUploadCardSourceInRequest) {
  StartGettingUploadDetails(
      PaymentsClient::UploadCardSource::UPSTREAM_CHECKOUT_FLOW);

  // Verify that the correct upload card source was included in the request.
  EXPECT_TRUE(GetUploadData().find("UPSTREAM_CHECKOUT_FLOW") !=
              std::string::npos);
}

TEST_F(PaymentsClientTest,
       GetDetailsIncludesUpstreamSettingsPageUploadCardSourceInRequest) {
  StartGettingUploadDetails(
      PaymentsClient::UploadCardSource::UPSTREAM_SETTINGS_PAGE);

  // Verify that the correct upload card source was included in the request.
  EXPECT_TRUE(GetUploadData().find("UPSTREAM_SETTINGS_PAGE") !=
              std::string::npos);
}

TEST_F(PaymentsClientTest,
       GetDetailsIncludesUpstreamCardOcrUploadCardSourceInRequest) {
  StartGettingUploadDetails(
      PaymentsClient::UploadCardSource::UPSTREAM_CARD_OCR);

  // Verify that the correct upload card source was included in the request.
  EXPECT_TRUE(GetUploadData().find("UPSTREAM_CARD_OCR") != std::string::npos);
}

TEST_F(
    PaymentsClientTest,
    GetDetailsIncludesLocalCardMigrationCheckoutFlowUploadCardSourceInRequest) {
  StartGettingUploadDetails(
      PaymentsClient::UploadCardSource::LOCAL_CARD_MIGRATION_CHECKOUT_FLOW);

  // Verify that the correct upload card source was included in the request.
  EXPECT_TRUE(GetUploadData().find("LOCAL_CARD_MIGRATION_CHECKOUT_FLOW") !=
              std::string::npos);
}

TEST_F(
    PaymentsClientTest,
    GetDetailsIncludesLocalCardMigrationSettingsPageUploadCardSourceInRequest) {
  StartGettingUploadDetails(
      PaymentsClient::UploadCardSource::LOCAL_CARD_MIGRATION_SETTINGS_PAGE);

  // Verify that the correct upload card source was included in the request.
  EXPECT_TRUE(GetUploadData().find("LOCAL_CARD_MIGRATION_SETTINGS_PAGE") !=
              std::string::npos);
}

TEST_F(PaymentsClientTest, GetDetailsIncludesUnknownUploadCardSourceInRequest) {
  StartGettingUploadDetails();

  // Verify that the absence of an upload card source results in UNKNOWN.
  EXPECT_TRUE(GetUploadData().find("UNKNOWN_UPLOAD_CARD_SOURCE") !=
              std::string::npos);
}

TEST_F(PaymentsClientTest, GetUploadDetailsVariationsTest) {
  // Register a trial and variation id, so that there is data in variations
  // headers.
  CreateFieldTrialWithId("AutofillTest", "Group", 369);
  StartGettingUploadDetails();

  // Note that experiment information is stored in X-Client-Data.
  EXPECT_TRUE(HasVariationsHeader());
}

TEST_F(PaymentsClientTest, GetDetailsIncludeBillableServiceNumber) {
  StartGettingUploadDetails();

  // Verify that billable service number was included in the request.
  EXPECT_TRUE(GetUploadData().find("\"billable_service\":12345") !=
              std::string::npos);
}

TEST_F(PaymentsClientTest, GetDetailsFollowedByUploadSuccess) {
  StartGettingUploadDetails();
  ReturnResponse(
      net::HTTP_OK,
      "{ \"context_token\": \"some_token\", \"legal_message\": {} }");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);

  result_ = AutofillClient::PaymentsRpcResult::kNone;

  StartUploading(/*include_cvc=*/true);
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK, "{}");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
}

TEST_F(PaymentsClientTest, GetDetailsMissingContextToken) {
  StartGettingUploadDetails();
  ReturnResponse(net::HTTP_OK, "{ \"legal_message\": {} }");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kPermanentFailure, result_);
}

TEST_F(PaymentsClientTest, GetDetailsMissingLegalMessage) {
  StartGettingUploadDetails();
  ReturnResponse(net::HTTP_OK, "{ \"context_token\": \"some_token\" }");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kPermanentFailure, result_);
  EXPECT_EQ(nullptr, legal_message_.get());
}

TEST_F(PaymentsClientTest, SupportedCardBinRangesParsesCorrectly) {
  StartGettingUploadDetails();
  ReturnResponse(
      net::HTTP_OK,
      "{"
      "  \"context_token\" : \"some_token\","
      "  \"legal_message\" : {},"
      "  \"supported_card_bin_ranges_string\" : \"1234,300000-555555,765\""
      "}");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  // Check that |supported_card_bin_ranges_| has the two entries specified in
  // ReturnResponse(~) above.
  ASSERT_EQ(3U, supported_card_bin_ranges_.size());
  EXPECT_EQ(1234, supported_card_bin_ranges_[0].first);
  EXPECT_EQ(1234, supported_card_bin_ranges_[0].second);
  EXPECT_EQ(300000, supported_card_bin_ranges_[1].first);
  EXPECT_EQ(555555, supported_card_bin_ranges_[1].second);
  EXPECT_EQ(765, supported_card_bin_ranges_[2].first);
  EXPECT_EQ(765, supported_card_bin_ranges_[2].second);
}

TEST_F(PaymentsClientTest, GetUploadAccountFromSyncTest) {
  // Set up a different account.
  const AccountInfo& secondary_account_info =
      identity_test_env_.MakeAccountAvailable("secondary@gmail.com");
  test_personal_data_.SetAccountInfoForPayments(secondary_account_info);

  StartUploading(/*include_cvc=*/true);
  ReturnResponse(net::HTTP_OK, "{}");

  // Issue a token for the secondary account.
  identity_test_env_.WaitForAccessTokenRequestIfNecessaryAndRespondWithToken(
      secondary_account_info.account_id, "secondary_account_token",
      AutofillClock::Now() + base::Days(10));

  // Verify the auth header.
  std::string auth_header_value;
  EXPECT_TRUE(intercepted_headers_.GetHeader(
      net::HttpRequestHeaders::kAuthorization, &auth_header_value))
      << intercepted_headers_.ToString();
  EXPECT_EQ("Bearer secondary_account_token", auth_header_value);
}

TEST_F(PaymentsClientTest, UploadCardVariationsTest) {
  // Register a trial and variation id, so that there is data in variations
  // headers.
  CreateFieldTrialWithId("AutofillTest", "Group", 369);
  StartUploading(/*include_cvc=*/true);
  IssueOAuthToken();

  // Note that experiment information is stored in X-Client-Data.
  EXPECT_TRUE(HasVariationsHeader());
}

TEST_F(PaymentsClientTest, UnmaskCardVariationsTest) {
  // Register a trial and variation id, so that there is data in variations
  // headers.
  CreateFieldTrialWithId("AutofillTest", "Group", 369);
  StartUnmasking(CardUnmaskOptions());
  IssueOAuthToken();

  // Note that experiment information is stored in X-Client-Data.
  EXPECT_TRUE(HasVariationsHeader());
}

TEST_F(PaymentsClientTest, UploadSuccessWithoutServerId) {
  StartUploading(/*include_cvc=*/true);
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK, "{}");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_TRUE(server_id_.empty());
}

TEST_F(PaymentsClientTest, UploadSuccessWithServerId) {
  StartUploading(/*include_cvc=*/true);
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK, "{ \"credit_card_id\": \"InstrumentData:1\" }");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ("InstrumentData:1", server_id_);
}

TEST_F(PaymentsClientTest, UploadIncludesNonLocationData) {
  StartUploading(/*include_cvc=*/true);
  IssueOAuthToken();

  // Verify that the recipient name field and test names do appear in the upload
  // data.
  EXPECT_TRUE(GetUploadData().find(PaymentsClient::kRecipientName) !=
              std::string::npos);
  EXPECT_TRUE(GetUploadData().find("John") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("Smith") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("Pat") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("Jones") != std::string::npos);

  // Verify that the phone number field and test numbers do appear in the upload
  // data.
  EXPECT_TRUE(GetUploadData().find(PaymentsClient::kPhoneNumber) !=
              std::string::npos);
  EXPECT_TRUE(GetUploadData().find("212") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("555") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("0162") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("834") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("0090") != std::string::npos);
}

TEST_F(PaymentsClientTest,
       UploadRequestIncludesBillingCustomerNumberInRequest) {
  StartUploading(/*include_cvc=*/true);
  IssueOAuthToken();

  // Verify that the billing customer number is included in the request.
  EXPECT_TRUE(
      GetUploadData().find("%22external_customer_id%22:%22111222333444%22") !=
      std::string::npos);
}

TEST_F(PaymentsClientTest, UploadIncludesCvcInRequestIfProvided) {
  StartUploading(/*include_cvc=*/true);
  IssueOAuthToken();

  // Verify that the encrypted_cvc and s7e_13_cvc parameters were included in
  // the request.
  EXPECT_TRUE(GetUploadData().find("encrypted_cvc") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("__param:s7e_13_cvc") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("&s7e_13_cvc=") != std::string::npos);
}

TEST_F(PaymentsClientTest, UploadIncludesChromeUserContext) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndDisableFeature(
      features::kAutofillEnableAccountWalletStorage);

  StartUploading(/*include_cvc=*/true);
  IssueOAuthToken();

  // ChromeUserContext was set.
  EXPECT_TRUE(GetUploadData().find("chrome_user_context") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("full_sync_enabled") != std::string::npos);
}

TEST_F(PaymentsClientTest,
       UploadIncludesChromeUserContextIfWalletStorageFlagEnabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(
      features::kAutofillEnableAccountWalletStorage);

  StartUploading(/*include_cvc=*/true);
  IssueOAuthToken();

  // ChromeUserContext was set.
  EXPECT_TRUE(GetUploadData().find("chrome_user_context") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("full_sync_enabled") != std::string::npos);
}

TEST_F(PaymentsClientTest, UploadDoesNotIncludeCvcInRequestIfNotProvided) {
  StartUploading(/*include_cvc=*/false);
  IssueOAuthToken();

  EXPECT_TRUE(!GetUploadData().empty());
  // Verify that the encrypted_cvc and s7e_13_cvc parameters were not included
  // in the request.
  EXPECT_TRUE(GetUploadData().find("encrypted_cvc") == std::string::npos);
  EXPECT_TRUE(GetUploadData().find("__param:s7e_13_cvc") == std::string::npos);
  EXPECT_TRUE(GetUploadData().find("&s7e_13_cvc=") == std::string::npos);
}

TEST_F(PaymentsClientTest, UploadIncludesCardNickname) {
  StartUploading(/*include_cvc=*/true, /*include_nickname=*/true);
  IssueOAuthToken();

  // Card nickname was set.
  EXPECT_TRUE(GetUploadData().find("nickname") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find(base::UTF16ToUTF8(upstream_nickname_)) !=
              std::string::npos);
}

TEST_F(PaymentsClientTest, UploadDoesNotIncludeCardNicknameEmptyNickname) {
  StartUploading(/*include_cvc=*/true, /*include_nickname=*/false);
  IssueOAuthToken();

  // Card nickname was not set.
  EXPECT_FALSE(GetUploadData().find("nickname") != std::string::npos);
}

TEST_F(PaymentsClientTest, UnmaskMissingPan) {
  StartUnmasking(CardUnmaskOptions());
  ReturnResponse(net::HTTP_OK, "{}");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kPermanentFailure, result_);
}

TEST_F(PaymentsClientTest, UnmaskRetryFailure) {
  StartUnmasking(CardUnmaskOptions());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK, "{ \"error\": { \"code\": \"INTERNAL\" } }");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kTryAgainFailure, result_);
  EXPECT_EQ("", unmask_response_details_->real_pan);
}

TEST_F(PaymentsClientTest, UnmaskPermanentFailure) {
  StartUnmasking(CardUnmaskOptions());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"error\": { \"code\": \"ANYTHING_ELSE\" } }");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kPermanentFailure, result_);
  EXPECT_EQ("", unmask_response_details_->real_pan);
}

TEST_F(PaymentsClientTest, UnmaskMalformedResponse) {
  StartUnmasking(CardUnmaskOptions());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK, "{ \"error_code\": \"WRONG_JSON_FORMAT\" }");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kPermanentFailure, result_);
  EXPECT_EQ("", unmask_response_details_->real_pan);
}

TEST_F(PaymentsClientTest, ReauthNeeded) {
  {
    StartUnmasking(CardUnmaskOptions());
    IssueOAuthToken();
    ReturnResponse(net::HTTP_UNAUTHORIZED, "");
    // No response yet.
    EXPECT_EQ(AutofillClient::PaymentsRpcResult::kNone, result_);
    EXPECT_EQ(nullptr, unmask_response_details_);

    // Second HTTP_UNAUTHORIZED causes permanent failure.
    IssueOAuthToken();
    ReturnResponse(net::HTTP_UNAUTHORIZED, "");
    EXPECT_EQ(AutofillClient::PaymentsRpcResult::kPermanentFailure, result_);
    EXPECT_EQ("", unmask_response_details_->real_pan);
  }

  result_ = AutofillClient::PaymentsRpcResult::kNone;
  unmask_response_details_ = nullptr;

  {
    StartUnmasking(CardUnmaskOptions());
    // NOTE: Don't issue an access token here: the issuing of an access token
    // first waits for the access token request to be received, but here there
    // should be no access token request because PaymentsClient should reuse the
    // access token from the previous request.
    ReturnResponse(net::HTTP_UNAUTHORIZED, "");
    // No response yet.
    EXPECT_EQ(AutofillClient::PaymentsRpcResult::kNone, result_);
    EXPECT_EQ(nullptr, unmask_response_details_);

    // HTTP_OK after first HTTP_UNAUTHORIZED results in success.
    IssueOAuthToken();
    ReturnResponse(net::HTTP_OK, "{ \"pan\": \"1234\" }");
    EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
    EXPECT_EQ("1234", unmask_response_details_->real_pan);
  }
}

TEST_F(PaymentsClientTest, NetworkError) {
  StartUnmasking(CardUnmaskOptions());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_REQUEST_TIMEOUT, std::string());
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kNetworkError, result_);
  EXPECT_EQ("", unmask_response_details_->real_pan);
}

TEST_F(PaymentsClientTest, OtherError) {
  StartUnmasking(CardUnmaskOptions());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_FORBIDDEN, std::string());
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kPermanentFailure, result_);
  EXPECT_EQ("", unmask_response_details_->real_pan);
}

TEST_F(PaymentsClientTest, VcnRetrievalTryAgainFailure) {
  StartUnmasking(CardUnmaskOptions().with_virtual_card());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"error\": { \"code\": \"ANYTHING_ELSE\", "
                 "\"api_error_reason\": \"virtual_card_temporary_error\" } }");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kVcnRetrievalTryAgainFailure,
            result_);
}

TEST_F(PaymentsClientTest, VcnRetrievalPermanentFailure) {
  StartUnmasking(CardUnmaskOptions().with_virtual_card());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"error\": { \"code\": \"ANYTHING_ELSE\", "
                 "\"api_error_reason\": \"virtual_card_permanent_error\"} }");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kVcnRetrievalPermanentFailure,
            result_);
}

TEST_F(PaymentsClientTest, UnmaskPermanentFailureWhenVcnMissingExpiration) {
  StartUnmasking(CardUnmaskOptions().with_virtual_card());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"pan\": \"4111111111111111\", \"dcvv\": \"999\" }");

  EXPECT_EQ("4111111111111111", unmask_response_details_->real_pan);
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kPermanentFailure, result_);
}

TEST_F(PaymentsClientTest, UnmaskPermanentFailureWhenVcnMissingCvv) {
  StartUnmasking(CardUnmaskOptions().with_virtual_card());
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"pan\": \"4111111111111111\", \"expiration\": { "
                 "\"month\":12, \"year\":2099 } }");

  EXPECT_EQ("4111111111111111", unmask_response_details_->real_pan);
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kPermanentFailure, result_);
}

// Tests for the local card migration flow. Desktop only.
#if !defined(OS_ANDROID) && !defined(OS_IOS)
TEST_F(PaymentsClientTest, GetDetailsFollowedByMigrationSuccess) {
  StartGettingUploadDetails();
  ReturnResponse(
      net::HTTP_OK,
      "{ \"context_token\": \"some_token\", \"legal_message\": {} }");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);

  result_ = AutofillClient::PaymentsRpcResult::kNone;

  StartMigrating(/*has_cardholder_name=*/true);
  IssueOAuthToken();
  ReturnResponse(
      net::HTTP_OK,
      "{\"save_result\":[],\"value_prop_display_text\":\"display text\"}");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
}

TEST_F(PaymentsClientTest, MigrateCardsVariationsTest) {
  // Register a trial and variation id, so that there is data in variations
  // headers.
  CreateFieldTrialWithId("AutofillTest", "Group", 369);
  StartMigrating(/*has_cardholder_name=*/true);
  IssueOAuthToken();

  // Note that experiment information is stored in X-Client-Data.
  EXPECT_TRUE(HasVariationsHeader());
}

TEST_F(PaymentsClientTest, MigrationRequestIncludesUniqueId) {
  StartMigrating(/*has_cardholder_name=*/true);
  IssueOAuthToken();

  // Verify that the unique id was included in the request.
  EXPECT_TRUE(GetUploadData().find("unique_id") != std::string::npos);
  EXPECT_TRUE(
      GetUploadData().find(migratable_credit_cards_[0].credit_card().guid()) !=
      std::string::npos);
  EXPECT_TRUE(
      GetUploadData().find(migratable_credit_cards_[1].credit_card().guid()) !=
      std::string::npos);
}

TEST_F(PaymentsClientTest, MigrationRequestIncludesEncryptedPan) {
  StartMigrating(/*has_cardholder_name=*/true);
  IssueOAuthToken();

  // Verify that the encrypted_pan and s7e_1_pan parameters were included
  // in the request.
  EXPECT_TRUE(GetUploadData().find("encrypted_pan") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("__param:s7e_1_pan0") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("&s7e_1_pan0=4111111111111111") !=
              std::string::npos);
  EXPECT_TRUE(GetUploadData().find("__param:s7e_1_pan1") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("&s7e_1_pan1=378282246310005") !=
              std::string::npos);
}

TEST_F(PaymentsClientTest, MigrationRequestIncludesCardholderNameWhenItExists) {
  StartMigrating(/*has_cardholder_name=*/true);
  IssueOAuthToken();

  EXPECT_TRUE(!GetUploadData().empty());
  // Verify that the cardholder name is sent if it exists.
  EXPECT_TRUE(GetUploadData().find("cardholder_name") != std::string::npos);
}

TEST_F(PaymentsClientTest,
       MigrationRequestExcludesCardholderNameWhenItDoesNotExist) {
  StartMigrating(/*has_cardholder_name=*/false);
  IssueOAuthToken();

  EXPECT_TRUE(!GetUploadData().empty());
  // Verify that the cardholder name is not sent if it doesn't exist.
  EXPECT_TRUE(GetUploadData().find("cardholder_name") == std::string::npos);
}

TEST_F(PaymentsClientTest, MigrationRequestIncludesChromeUserContext) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndDisableFeature(
      features::kAutofillEnableAccountWalletStorage);

  StartMigrating(/*has_cardholder_name=*/true);
  IssueOAuthToken();

  // ChromeUserContext was set.
  EXPECT_TRUE(GetUploadData().find("chrome_user_context") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("full_sync_enabled") != std::string::npos);
}

TEST_F(PaymentsClientTest,
       MigrationRequestIncludesChromeUserContextIfWalletStorageFlagEnabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(
      features::kAutofillEnableAccountWalletStorage);

  StartMigrating(/*has_cardholder_name=*/true);
  IssueOAuthToken();

  // ChromeUserContext was set.
  EXPECT_TRUE(GetUploadData().find("chrome_user_context") != std::string::npos);
  EXPECT_TRUE(GetUploadData().find("full_sync_enabled") != std::string::npos);
}

TEST_F(PaymentsClientTest, MigrationRequestIncludesCardNickname) {
  StartMigrating(/*has_cardholder_name=*/true,
                 /*set_nickname_to_first_card=*/true);
  IssueOAuthToken();

  // Nickname was set for the first card.
  std::size_t pos = GetUploadData().find("nickname");
  EXPECT_TRUE(pos != std::string::npos);
  EXPECT_TRUE(GetUploadData().find(base::UTF16ToUTF8(
                  migratable_credit_cards_[0].credit_card().nickname())) !=
              std::string::npos);

  // Nickname was not set for the second card.
  EXPECT_FALSE(GetUploadData().find("nickname", pos + 1) != std::string::npos);
}

TEST_F(PaymentsClientTest, MigrationSuccessWithSaveResult) {
  StartMigrating(/*has_cardholder_name=*/true);
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{\"save_result\":[{\"unique_id\":\"0\",\"status\":"
                 "\"SUCCESS\"},{\"unique_id\":\"1\",\"status\":\"TEMPORARY_"
                 "FAILURE\"}],\"value_prop_display_text\":\"display text\"}");

  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_TRUE(migration_save_results_.get());
  EXPECT_TRUE(migration_save_results_->find("0") !=
              migration_save_results_->end());
  EXPECT_TRUE(migration_save_results_->at("0") == "SUCCESS");
  EXPECT_TRUE(migration_save_results_->find("1") !=
              migration_save_results_->end());
  EXPECT_TRUE(migration_save_results_->at("1") == "TEMPORARY_FAILURE");
}

TEST_F(PaymentsClientTest, MigrationMissingSaveResult) {
  StartMigrating(/*has_cardholder_name=*/true);
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{\"value_prop_display_text\":\"display text\"}");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kPermanentFailure, result_);
  EXPECT_EQ(nullptr, migration_save_results_.get());
}

TEST_F(PaymentsClientTest, MigrationSuccessWithDisplayText) {
  StartMigrating(/*has_cardholder_name=*/true);
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{\"save_result\":[{\"unique_id\":\"0\",\"status\":"
                 "\"SUCCESS\"}],\"value_prop_display_text\":\"display text\"}");
  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ("display text", display_text_);
}
#endif

TEST_F(PaymentsClientTest, SelectChallengeOptionWithSmsOtpMethod) {
  StartSelectingChallengeOption(CardUnmaskChallengeOptionType::kSmsOtp,
                                "arbitrary id for sms otp");
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK, "{ \"context_token\": \"new context token\" }");

  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  assertIncludedInRequest("context_token");
  assertIncludedInRequest("selected_idv_challenge_option");
  assertIncludedInRequest("sms_otp_challenge_option");
  // We should only set the challenge id. No need to send the masked phone
  // number.
  assertIncludedInRequest("challenge_id");
  assertIncludedInRequest("arbitrary id for sms otp");
  assertNotIncludedInRequest("masked_phone_number");
}

TEST_F(PaymentsClientTest, SelectChallengeOptionSuccess) {
  StartSelectingChallengeOption();
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK, "{ \"context_token\": \"new context token\" }");

  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kSuccess, result_);
  EXPECT_EQ("new context token", context_token_);
}

TEST_F(PaymentsClientTest, SelectChallengeOptionTemporaryFailure) {
  StartSelectingChallengeOption();
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"error\": { \"code\": \"ANYTHING_ELSE\", "
                 "\"api_error_reason\": \"virtual_card_temporary_error\"} }");

  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kVcnRetrievalTryAgainFailure,
            result_);
}

TEST_F(PaymentsClientTest, SelectChallengeOptionVcnFlowPermanentFailure) {
  StartSelectingChallengeOption();
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK,
                 "{ \"error\": { \"code\": \"ANYTHING_ELSE\", "
                 "\"api_error_reason\": \"virtual_card_permanent_error\"} }");

  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kVcnRetrievalPermanentFailure,
            result_);
}

TEST_F(PaymentsClientTest, SelectChallengeOptionResponseMissingContextToken) {
  StartSelectingChallengeOption();
  IssueOAuthToken();
  ReturnResponse(net::HTTP_OK, "{}");

  EXPECT_EQ(AutofillClient::PaymentsRpcResult::kPermanentFailure, result_);
}

}  // namespace payments
}  // namespace autofill
