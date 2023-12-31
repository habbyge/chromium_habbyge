// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/input_method/assistive_suggester.h"

#include "ash/constants/ash_features.h"
#include "ash/constants/ash_pref_names.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ash/input_method/assistive_suggester_client_filter.h"
#include "chrome/browser/ash/input_method/assistive_suggester_switch.h"
#include "chrome/browser/ash/input_method/personal_info_suggester.h"
#include "chrome/browser/ui/ash/keyboard/chrome_keyboard_controller_client.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/ime/ash/ime_bridge.h"

namespace ash {
namespace input_method {
namespace {

using ::chromeos::ime::TextSuggestion;
using ::chromeos::ime::TextSuggestionMode;
using ::chromeos::ime::TextSuggestionType;

}  // namespace

const char kEmojiData[] = "happy,😀;😃;😄";

class AssistiveSuggesterTest : public testing::Test {
 protected:
  AssistiveSuggesterTest() { profile_ = std::make_unique<TestingProfile>(); }

  void SetUp() override {
    engine_ = std::make_unique<InputMethodEngine>();
    assistive_suggester_ = std::make_unique<AssistiveSuggester>(
        engine_.get(), profile_.get(),
        std::make_unique<AssistiveSuggesterClientFilter>());
    histogram_tester_.ExpectUniqueSample(
        "InputMethod.Assistive.UserPref.PersonalInfo", true, 1);
    histogram_tester_.ExpectUniqueSample("InputMethod.Assistive.UserPref.Emoji",
                                         true, 1);
    histogram_tester_.ExpectUniqueSample(
        "InputMethod.Assistive.UserPref.MultiWord", true, 1);
    ui::IMEBridge::Initialize();
  }

  content::BrowserTaskEnvironment task_environment_;
  std::unique_ptr<TestingProfile> profile_;
  std::unique_ptr<AssistiveSuggester> assistive_suggester_;
  std::unique_ptr<InputMethodEngine> engine_;
  base::HistogramTester histogram_tester_;
};

TEST_F(AssistiveSuggesterTest,
       EmojiSuggestionPrefFalseFeatureFlagTrue_UserPrefEnabledFalse) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures(
      /*enabled_features=*/{features::kEmojiSuggestAddition},
      /*disabled_features=*/{features::kAssistPersonalInfo,
                             features::kAssistMultiWord});
  profile_->GetPrefs()->SetBoolean(prefs::kEmojiSuggestionEnterpriseAllowed,
                                   true);
  profile_->GetPrefs()->SetBoolean(prefs::kEmojiSuggestionEnabled, false);

  EXPECT_FALSE(assistive_suggester_->IsAssistiveFeatureEnabled());
}

TEST_F(AssistiveSuggesterTest,
       EmojiSuggestionPrefFalseFeatureFlagTrue_EnterprisePrefEnabledFalse) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures(
      /*enabled_features=*/{features::kEmojiSuggestAddition},
      /*disabled_features=*/{features::kAssistPersonalInfo,
                             features::kAssistMultiWord});
  profile_->GetPrefs()->SetBoolean(prefs::kEmojiSuggestionEnterpriseAllowed,
                                   false);
  profile_->GetPrefs()->SetBoolean(prefs::kEmojiSuggestionEnabled, true);

  EXPECT_FALSE(assistive_suggester_->IsAssistiveFeatureEnabled());
}

TEST_F(AssistiveSuggesterTest,
       EmojiSuggestionPrefTrueFeatureFlagTrue_BothPrefsEnabledTrue) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures(
      /*enabled_features=*/{features::kEmojiSuggestAddition},
      /*disabled_features=*/{features::kAssistPersonalInfo,
                             features::kAssistMultiWord});
  profile_->GetPrefs()->SetBoolean(prefs::kEmojiSuggestionEnterpriseAllowed,
                                   true);
  profile_->GetPrefs()->SetBoolean(prefs::kEmojiSuggestionEnabled, true);

  EXPECT_TRUE(assistive_suggester_->IsAssistiveFeatureEnabled());
}

TEST_F(AssistiveSuggesterTest,
       EmojiSuggestionPrefTrueFeatureFlagTrue_BothPrefsEnabledFalse) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures(
      /*enabled_features=*/{features::kEmojiSuggestAddition},
      /*disabled_features=*/{features::kAssistPersonalInfo,
                             features::kAssistMultiWord});
  profile_->GetPrefs()->SetBoolean(prefs::kEmojiSuggestionEnterpriseAllowed,
                                   false);
  profile_->GetPrefs()->SetBoolean(prefs::kEmojiSuggestionEnabled, false);

  EXPECT_FALSE(assistive_suggester_->IsAssistiveFeatureEnabled());
}

TEST_F(AssistiveSuggesterTest,
       EnhancedEmojiSuggestDisabledWhenStandardEmojiDisabledAndPrefsDisabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures(
      /*enabled_features=*/{features::kAssistEmojiEnhanced},
      /*disabled_features=*/{features::kAssistPersonalInfo,
                             features::kAssistMultiWord,
                             features::kEmojiSuggestAddition});
  profile_->GetPrefs()->SetBoolean(prefs::kEmojiSuggestionEnterpriseAllowed,
                                   false);
  profile_->GetPrefs()->SetBoolean(prefs::kEmojiSuggestionEnabled, false);

  EXPECT_FALSE(assistive_suggester_->IsAssistiveFeatureEnabled());
}

TEST_F(AssistiveSuggesterTest,
       EnhancedEmojiSuggestDisabledWhenStandardEmojiDisabledAndPrefsEnabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures(
      /*enabled_features=*/{features::kAssistEmojiEnhanced},
      /*disabled_features=*/{features::kAssistPersonalInfo,
                             features::kAssistMultiWord,
                             features::kEmojiSuggestAddition});
  profile_->GetPrefs()->SetBoolean(prefs::kEmojiSuggestionEnterpriseAllowed,
                                   true);
  profile_->GetPrefs()->SetBoolean(prefs::kEmojiSuggestionEnabled, true);

  EXPECT_FALSE(assistive_suggester_->IsAssistiveFeatureEnabled());
}

TEST_F(AssistiveSuggesterTest,
       EnhancedEmojiSuggestEnabledWhenStandardEmojiEnabledAndPrefsEnabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures(
      /*enabled_features=*/{features::kAssistEmojiEnhanced,
                            features::kEmojiSuggestAddition},
      /*disabled_features=*/{features::kAssistPersonalInfo,
                             features::kAssistMultiWord});
  profile_->GetPrefs()->SetBoolean(prefs::kEmojiSuggestionEnterpriseAllowed,
                                   true);
  profile_->GetPrefs()->SetBoolean(prefs::kEmojiSuggestionEnabled, true);

  EXPECT_TRUE(assistive_suggester_->IsAssistiveFeatureEnabled());
}

TEST_F(
    AssistiveSuggesterTest,
    AssistPersonalInfoEnabledPrefFalseFeatureFlagTrue_AssitiveFeatureEnabledFalse) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures(
      /*enabled_features=*/{features::kAssistPersonalInfo},
      /*disabled_features=*/{features::kEmojiSuggestAddition,
                             features::kAssistMultiWord});
  profile_->GetPrefs()->SetBoolean(prefs::kAssistPersonalInfoEnabled, false);

  EXPECT_FALSE(assistive_suggester_->IsAssistiveFeatureEnabled());
}

TEST_F(
    AssistiveSuggesterTest,
    AssistPersonalInfoEnabledTrueFeatureFlagTrue_AssitiveFeatureEnabledTrue) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures(
      /*enabled_features=*/{features::kAssistPersonalInfo},
      /*disabled_features=*/{features::kEmojiSuggestAddition,
                             features::kAssistMultiWord});
  profile_->GetPrefs()->SetBoolean(prefs::kAssistPersonalInfoEnabled, true);

  EXPECT_TRUE(assistive_suggester_->IsAssistiveFeatureEnabled());
}

TEST_F(AssistiveSuggesterTest,
       MultiWordEnabledWhenFeatureFlagEnabledAndPrefEnabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures(
      /*enabled_features=*/{features::kAssistMultiWord},
      /*disabled_features=*/{features::kEmojiSuggestAddition,
                             features::kAssistPersonalInfo});
  profile_->GetPrefs()->SetBoolean(prefs::kAssistPredictiveWritingEnabled,
                                   true);

  EXPECT_TRUE(assistive_suggester_->IsAssistiveFeatureEnabled());
}

TEST_F(AssistiveSuggesterTest,
       MultiWordDisabledWhenFeatureFlagEnabledAndPrefDisabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures(
      /*enabled_features=*/{features::kAssistMultiWord},
      /*disabled_features=*/{features::kEmojiSuggestAddition,
                             features::kAssistPersonalInfo});
  profile_->GetPrefs()->SetBoolean(prefs::kAssistPredictiveWritingEnabled,
                                   false);

  EXPECT_FALSE(assistive_suggester_->IsAssistiveFeatureEnabled());
}

TEST_F(AssistiveSuggesterTest,
       MultiWordDisabledWhenFeatureFlagDisabledAndPrefDisabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures(
      /*enabled_features=*/{},
      /*disabled_features=*/{features::kEmojiSuggestAddition,
                             features::kAssistPersonalInfo,
                             features::kAssistMultiWord});
  profile_->GetPrefs()->SetBoolean(prefs::kAssistPredictiveWritingEnabled,
                                   false);

  EXPECT_FALSE(assistive_suggester_->IsAssistiveFeatureEnabled());
}

TEST_F(AssistiveSuggesterTest, MultiWordEnabledWhenFeatureFlagAndDepsEnabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures(
      /*enabled_features=*/{features::kAssistMultiWord},
      /*disabled_features=*/{features::kEmojiSuggestAddition,
                             features::kAssistPersonalInfo});

  EXPECT_TRUE(assistive_suggester_->IsAssistiveFeatureEnabled());
}

class FakeSuggesterSwitch : public AssistiveSuggesterSwitch {
 public:
  explicit FakeSuggesterSwitch(EnabledSuggestions enabled_suggestions)
      : enabled_suggestions_(enabled_suggestions) {}
  ~FakeSuggesterSwitch() override = default;

  // AssistiveSuggesterDelegate overrides
  bool IsEmojiSuggestionAllowed() override {
    return enabled_suggestions_.emoji_suggestions;
  }

  bool IsMultiWordSuggestionAllowed() override {
    return enabled_suggestions_.multi_word_suggestions;
  }

  bool IsPersonalInfoSuggestionAllowed() override {
    return enabled_suggestions_.personal_info_suggestions;
  }

  void GetEnabledSuggestions(GetEnabledSuggestionsCallback callback) override {
    std::move(callback).Run(enabled_suggestions_);
  }

 private:
  EnabledSuggestions enabled_suggestions_;
};

class AssistiveSuggesterMultiWordTest : public testing::Test {
 protected:
  AssistiveSuggesterMultiWordTest() {
    profile_ = std::make_unique<TestingProfile>();
  }

  void SetUp() override {
    engine_ = std::make_unique<InputMethodEngine>();
    assistive_suggester_ = std::make_unique<AssistiveSuggester>(
        engine_.get(), profile_.get(),
        std::make_unique<FakeSuggesterSwitch>(
            FakeSuggesterSwitch::EnabledSuggestions{
                .multi_word_suggestions = true,
            }));

    feature_list_.InitWithFeatures(
        /*enabled_features=*/{features::kAssistMultiWord},
        /*disabled_features=*/{});
    profile_->GetPrefs()->SetBoolean(prefs::kAssistPredictiveWritingEnabled,
                                     true);

    ui::IMEBridge::Initialize();
  }

  content::BrowserTaskEnvironment task_environment_;
  base::test::ScopedFeatureList feature_list_;
  std::unique_ptr<TestingProfile> profile_;
  std::unique_ptr<AssistiveSuggester> assistive_suggester_;
  std::unique_ptr<InputMethodEngine> engine_;
  base::HistogramTester histogram_tester_;
};

TEST_F(AssistiveSuggesterMultiWordTest,
       MatchMetricNotRecordedWhenZeroSuggestions) {
  assistive_suggester_->OnExternalSuggestionsUpdated({});

  histogram_tester_.ExpectTotalCount("InputMethod.Assistive.Match", 0);
}

TEST_F(AssistiveSuggesterMultiWordTest,
       MatchMetricRecordedWhenOneOrMoreSuggestions) {
  std::vector<TextSuggestion> suggestions = {
      TextSuggestion{.mode = TextSuggestionMode::kPrediction,
                     .type = TextSuggestionType::kMultiWord,
                     .text = "hello there"}};

  assistive_suggester_->OnFocus(5);
  assistive_suggester_->OnSurroundingTextChanged(u"", 0, 0);
  assistive_suggester_->OnExternalSuggestionsUpdated(suggestions);

  histogram_tester_.ExpectTotalCount("InputMethod.Assistive.Match", 1);
  histogram_tester_.ExpectUniqueSample("InputMethod.Assistive.Match",
                                       AssistiveType::kMultiWordPrediction, 1);
}

TEST_F(AssistiveSuggesterMultiWordTest,
       MatchMetricNotRecordedWhenMultiWordFlagDisabled) {
  feature_list_.Reset();
  feature_list_.InitWithFeatures(
      /*enabled_features=*/{},
      /*disabled_features=*/{features::kAssistMultiWord});
  std::vector<TextSuggestion> suggestions = {
      TextSuggestion{.mode = TextSuggestionMode::kPrediction,
                     .type = TextSuggestionType::kMultiWord,
                     .text = "hello there"}};

  assistive_suggester_->OnFocus(5);
  assistive_suggester_->OnSurroundingTextChanged(u"", 0, 0);
  assistive_suggester_->OnExternalSuggestionsUpdated(suggestions);

  histogram_tester_.ExpectTotalCount("InputMethod.Assistive.Match", 0);
}

TEST_F(AssistiveSuggesterMultiWordTest,
       DisableMetricNotRecordedWhenNoSuggestionAndMultiWordBlocked) {
  assistive_suggester_ = std::make_unique<AssistiveSuggester>(
      engine_.get(), profile_.get(),
      std::make_unique<FakeSuggesterSwitch>(
          FakeSuggesterSwitch::EnabledSuggestions{}));

  assistive_suggester_->OnFocus(5);
  assistive_suggester_->OnSurroundingTextChanged(u"", 0, 0);
  assistive_suggester_->OnExternalSuggestionsUpdated({});

  histogram_tester_.ExpectTotalCount("InputMethod.Assistive.Disabled.MultiWord",
                                     0);
}

TEST_F(AssistiveSuggesterMultiWordTest,
       DisableMetricRecordedWhenGivenSuggestionAndMultiWordBlocked) {
  assistive_suggester_ = std::make_unique<AssistiveSuggester>(
      engine_.get(), profile_.get(),
      std::make_unique<FakeSuggesterSwitch>(
          FakeSuggesterSwitch::EnabledSuggestions{}));
  std::vector<TextSuggestion> suggestions = {
      TextSuggestion{.mode = TextSuggestionMode::kPrediction,
                     .type = TextSuggestionType::kMultiWord,
                     .text = "hello there"}};

  assistive_suggester_->OnFocus(5);
  assistive_suggester_->OnSurroundingTextChanged(u"", 0, 0);
  assistive_suggester_->OnExternalSuggestionsUpdated(suggestions);

  histogram_tester_.ExpectTotalCount("InputMethod.Assistive.Disabled.MultiWord",
                                     1);
  histogram_tester_.ExpectUniqueSample(
      "InputMethod.Assistive.Disabled.MultiWord",
      DisabledReason::kUrlOrAppNotAllowed, 1);
}

TEST_F(AssistiveSuggesterMultiWordTest,
       CoverageMetricNotRecordedWhenNoSuggestionGiven) {
  assistive_suggester_->OnFocus(5);
  assistive_suggester_->OnSurroundingTextChanged(u"", 0, 0);
  assistive_suggester_->OnExternalSuggestionsUpdated({});

  histogram_tester_.ExpectTotalCount("InputMethod.Assistive.Coverage", 0);
}

TEST_F(AssistiveSuggesterMultiWordTest,
       CoverageMetricRecordedWhenSuggestionShown) {
  std::vector<TextSuggestion> suggestions = {
      TextSuggestion{.mode = TextSuggestionMode::kPrediction,
                     .type = TextSuggestionType::kMultiWord,
                     .text = "hello there"}};

  assistive_suggester_->OnFocus(5);
  assistive_suggester_->OnExternalSuggestionsUpdated(suggestions);

  histogram_tester_.ExpectTotalCount("InputMethod.Assistive.Coverage", 1);
  histogram_tester_.ExpectUniqueSample("InputMethod.Assistive.Coverage",
                                       AssistiveType::kMultiWordPrediction, 1);
}

TEST_F(AssistiveSuggesterMultiWordTest,
       CoverageMetricRecordedOnceWhenSuggestionShownAndTracked) {
  std::vector<TextSuggestion> suggestions = {
      TextSuggestion{.mode = TextSuggestionMode::kPrediction,
                     .type = TextSuggestionType::kMultiWord,
                     .text = "hello there"}};

  assistive_suggester_->OnFocus(5);
  assistive_suggester_->OnSurroundingTextChanged(u"", 0, 0);
  assistive_suggester_->OnExternalSuggestionsUpdated(suggestions);
  assistive_suggester_->OnSurroundingTextChanged(u"h", 1, 1);
  assistive_suggester_->OnExternalSuggestionsUpdated(suggestions);
  assistive_suggester_->OnSurroundingTextChanged(u"he", 2, 2);
  assistive_suggester_->OnExternalSuggestionsUpdated(suggestions);
  assistive_suggester_->OnSurroundingTextChanged(u"hel", 3, 3);
  assistive_suggester_->OnExternalSuggestionsUpdated(suggestions);

  histogram_tester_.ExpectTotalCount("InputMethod.Assistive.Coverage", 1);
  histogram_tester_.ExpectUniqueSample("InputMethod.Assistive.Coverage",
                                       AssistiveType::kMultiWordPrediction, 1);
}

TEST_F(AssistiveSuggesterMultiWordTest,
       CoverageMetricRecordedForEverySuggestionShown) {
  std::vector<TextSuggestion> first_suggestions = {
      TextSuggestion{.mode = TextSuggestionMode::kPrediction,
                     .type = TextSuggestionType::kMultiWord,
                     .text = "hello there"}};
  std::vector<TextSuggestion> second_suggestions = {
      TextSuggestion{.mode = TextSuggestionMode::kPrediction,
                     .type = TextSuggestionType::kMultiWord,
                     .text = "was"}};

  assistive_suggester_->OnFocus(5);
  assistive_suggester_->OnSurroundingTextChanged(u"", 0, 0);
  assistive_suggester_->OnExternalSuggestionsUpdated(first_suggestions);
  assistive_suggester_->OnSurroundingTextChanged(u"h", 1, 1);
  assistive_suggester_->OnExternalSuggestionsUpdated(first_suggestions);
  assistive_suggester_->OnSurroundingTextChanged(u"he", 2, 2);
  assistive_suggester_->OnExternalSuggestionsUpdated(first_suggestions);
  assistive_suggester_->OnSurroundingTextChanged(u"he ", 3, 3);
  assistive_suggester_->OnExternalSuggestionsUpdated(second_suggestions);

  histogram_tester_.ExpectTotalCount("InputMethod.Assistive.Coverage", 2);
  histogram_tester_.ExpectUniqueSample("InputMethod.Assistive.Coverage",
                                       AssistiveType::kMultiWordPrediction, 2);
}

class AssistiveSuggesterEmojiTest : public testing::Test {
 protected:
  AssistiveSuggesterEmojiTest() {
    profile_ = std::make_unique<TestingProfile>();
  }

  void SetUp() override {
    engine_ = std::make_unique<InputMethodEngine>();
    assistive_suggester_ = std::make_unique<AssistiveSuggester>(
        engine_.get(), profile_.get(),
        std::make_unique<FakeSuggesterSwitch>(
            FakeSuggesterSwitch::EnabledSuggestions{
                .emoji_suggestions = true,
            }));
    assistive_suggester_->get_emoji_suggester_for_testing()
        ->LoadEmojiMapForTesting(kEmojiData);

    // Needed to ensure globals accessed by EmojiSuggester are available.
    chrome_keyboard_controller_client_ =
        ChromeKeyboardControllerClient::CreateForTest();
    chrome_keyboard_controller_client_->set_keyboard_visible_for_test(false);

    profile_->GetPrefs()->SetBoolean(prefs::kEmojiSuggestionEnterpriseAllowed,
                                     true);
    profile_->GetPrefs()->SetBoolean(prefs::kEmojiSuggestionEnabled, true);

    ui::IMEBridge::Initialize();
  }

  content::BrowserTaskEnvironment task_environment_;
  base::test::ScopedFeatureList feature_list_;
  std::unique_ptr<TestingProfile> profile_;
  std::unique_ptr<AssistiveSuggester> assistive_suggester_;
  std::unique_ptr<InputMethodEngine> engine_;
  base::HistogramTester histogram_tester_;

  // Needs to outlive the emoji_suggester under test.
  std::unique_ptr<ChromeKeyboardControllerClient>
      chrome_keyboard_controller_client_;
};

TEST_F(AssistiveSuggesterEmojiTest,
       ShouldReturnPrefixBasedEmojiSuggestionsWhenStandardFlagEnabledOnly) {
  feature_list_.InitWithFeatures(
      /*enabled_features=*/{features::kEmojiSuggestAddition},
      /*disabled_features=*/{});

  assistive_suggester_->OnFocus(5);

  EXPECT_TRUE(assistive_suggester_->OnSurroundingTextChanged(u"happy ", 6, 6));
}

TEST_F(AssistiveSuggesterEmojiTest,
       ShouldNotReturnPrefixBasedEmojiSuggestionsWhenBothEmojiFlagsAreEnabled) {
  feature_list_.InitWithFeatures(
      /*enabled_features=*/{features::kEmojiSuggestAddition,
                            features::kAssistEmojiEnhanced},
      /*disabled_features=*/{});

  assistive_suggester_->OnFocus(5);

  EXPECT_FALSE(assistive_suggester_->OnSurroundingTextChanged(u"happy ", 6, 6));
}

}  // namespace input_method
}  // namespace ash
