// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_PUBLIC_CPP_QUICK_ANSWERS_QUICK_ANSWERS_STATE_H_
#define ASH_PUBLIC_CPP_QUICK_ANSWERS_QUICK_ANSWERS_STATE_H_

#include <memory>
#include <string>

#include "ash/components/quick_answers/public/cpp/quick_answers_prefs.h"
#include "ash/public/cpp/ash_public_export.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/timer/timer.h"

class PrefChangeRegistrar;
class PrefService;

namespace ash {

// The consent will appear up to a total of 6 times.
constexpr int kConsentImpressionCap = 6;
// The consent need to show for at least 1 second to be counted.
constexpr int kConsentImpressionMinimumDuration = 1;

// Consent result of the consent-view.
enum class ConsentResultType {
  // When user clicks on the "Allow" button.
  kAllow = 0,
  // When user clicks on the "No thanks" button.
  kNoThanks = 1,
  // When user dismisses or ignores the consent-view.
  kDismiss = 2
};

// A checked observer which receives Quick Answers state change.
class ASH_PUBLIC_EXPORT QuickAnswersStateObserver
    : public base::CheckedObserver {
 public:
  virtual void OnSettingsEnabled(bool enabled) {}
};

// A class that holds Quick Answers related prefs and states.
class ASH_PUBLIC_EXPORT QuickAnswersState {
 public:
  static QuickAnswersState* Get();

  QuickAnswersState();

  QuickAnswersState(const QuickAnswersState&) = delete;
  QuickAnswersState& operator=(const QuickAnswersState&) = delete;

  ~QuickAnswersState();

  void AddObserver(QuickAnswersStateObserver* observer);
  void RemoveObserver(QuickAnswersStateObserver* observer);

  void RegisterPrefChanges(PrefService* pref_service);

  void StartConsent();
  void OnConsentResult(ConsentResultType result);

  bool ShouldUseQuickAnswersTextAnnotator();

  bool settings_enabled() const { return settings_enabled_; }
  quick_answers::prefs::ConsentStatus consent_status() const {
    return consent_status_;
  }
  bool definition_enabled() const { return definition_enabled_; }
  bool translation_enabled() const { return translation_enabled_; }
  bool unit_conversion_enabled() const { return unit_conversion_enabled_; }
  bool is_eligible() const { return is_eligible_; }

  void set_eligibility_for_testing(bool is_eligible) {
    is_eligible_ = is_eligible;
  }
  void set_use_text_annotator_for_testing() {
    use_text_annotator_for_testing_ = true;
  }

 private:
  void InitializeObserver(QuickAnswersStateObserver* observer);

  // Called when the related preferences are obtained from the pref service.
  void UpdateSettingsEnabled();
  void UpdateConsentStatus();
  void UpdateDefinitionEnabled();
  void UpdateTranslationEnabled();
  void UpdateUnitConverstionEnabled();

  // Called when the feature eligibility might change.
  void UpdateEligibility();

  // Whether the Quick Answers is enabled in system settings.
  bool settings_enabled_ = false;

  // Status of the user's consent for the Quick Answers feature.
  quick_answers::prefs::ConsentStatus consent_status_ =
      quick_answers::prefs::ConsentStatus::kUnknown;

  // Whether the Quick Answers definition is enabled.
  bool definition_enabled_ = true;

  // Whether the Quick Answers translation is enabled.
  bool translation_enabled_ = true;

  // Whether the Quick Answers unit conversion is enabled.
  bool unit_conversion_enabled_ = true;

  // Whether the Quick Answers feature is eligible. The value is derived from a
  // number of other states.
  bool is_eligible_ = false;

  // Whether the pref values has been initialized.
  bool prefs_initialized_ = false;

  // Whether to use text annotator for testing.
  bool use_text_annotator_for_testing_ = false;

  // Time when the notice is shown.
  base::TimeTicks consent_start_time_;

  // Observes user profile prefs for the Assistant.
  std::unique_ptr<PrefChangeRegistrar> pref_change_registrar_;

  base::ObserverList<QuickAnswersStateObserver> observers_;
};

}  // namespace ash

#endif  // ASH_PUBLIC_CPP_QUICK_ANSWERS_QUICK_ANSWERS_STATE_H_
