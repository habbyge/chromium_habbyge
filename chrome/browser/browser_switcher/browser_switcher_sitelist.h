// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_BROWSER_SWITCHER_BROWSER_SWITCHER_SITELIST_H_
#define CHROME_BROWSER_BROWSER_SWITCHER_BROWSER_SWITCHER_SITELIST_H_

#include "chrome/browser/browser_switcher/browser_switcher_prefs.h"
#include "components/prefs/pref_change_registrar.h"

class GURL;

namespace browser_switcher {

class BrowserSwitcherPrefs;
class ParsedXml;

// Return a Rule object for |original_rule|, with the right type depending on
// |parsing_mode|.
//
// Returns nullptr if |original_rule| is not a valid rule.
std::unique_ptr<Rule> CanonicalizeRule(base::StringPiece original_rule,
                                       ParsingMode parsing_mode);

enum Action {
  kStay = 0,
  kGo = 1,
};

enum Reason {
  // BrowserSwitcher is globally disabled.
  kDisabled = 0,
  // Protocol is not HTTP, HTTPS or FILE.
  kProtocol = 1,
  // A sitelist rule (either positive or negative) matched.
  kSitelist = 2,
  // A greylist rule matched.
  kGreylist = 3,
  // No rule matched, so default to STAY.
  kDefault = 4,
};

struct Decision {
  Decision(Action, Reason, const Rule* matching_rule);

  Decision();
  Decision(Decision&);
  Decision(Decision&&);

  bool operator==(const Decision&) const;

  Action action;
  Reason reason;
  // If reason is kSitelist or kGreylist, this is the rule that caused the
  // decision.
  const Rule* matching_rule;
};

// Interface that decides whether a navigation should trigger a browser
// switch.
class BrowserSwitcherSitelist {
 public:
  virtual ~BrowserSwitcherSitelist();

  // Returns true if the given URL should be open in an alternative browser.
  bool ShouldSwitch(const GURL& url) const;

  // Same as ShouldSwitch(), but returns a struct instead of a bool, also
  // containing the reason why this decision was made.
  virtual Decision GetDecision(const GURL& url) const = 0;

  // Set the Internet Explorer Enterprise Mode sitelist to use, in addition to
  // Chrome's sitelist/greylist policies. Consumes the object, and performs no
  // copy.
  virtual void SetIeemSitelist(ParsedXml&& sitelist) = 0;

  // Set the XML sitelist file to use, in addition to Chrome's sitelist/greylist
  // policies. This XML file is in the same format as an IEEM sitelist.
  // Consumes the object, and performs no copy.
  virtual void SetExternalSitelist(ParsedXml&& sitelist) = 0;

  // Set the XML sitelist file to use, in addition to Chrome's sitelist/greylist
  // policies. This XML file is in the same format as an IEEM sitelist.
  // Consumes the object, and performs no copy.
  virtual void SetExternalGreylist(ParsedXml&& sitelist) = 0;

  virtual const RuleSet* GetIeemSitelist() const = 0;
  virtual const RuleSet* GetExternalSitelist() const = 0;
};

// Manages the sitelist configured by the administrator for
// BrowserSwitcher. Decides whether a navigation should trigger a browser
// switch.
class BrowserSwitcherSitelistImpl : public BrowserSwitcherSitelist {
 public:
  explicit BrowserSwitcherSitelistImpl(BrowserSwitcherPrefs* prefs);
  ~BrowserSwitcherSitelistImpl() override;

  BrowserSwitcherSitelistImpl(const BrowserSwitcherSitelistImpl&) = delete;
  BrowserSwitcherSitelistImpl& operator=(const BrowserSwitcherSitelistImpl&) =
      delete;

  // BrowserSwitcherSitelist
  Decision GetDecision(const GURL& url) const override;
  void SetIeemSitelist(ParsedXml&& sitelist) override;
  void SetExternalSitelist(ParsedXml&& sitelist) override;
  void SetExternalGreylist(ParsedXml&& greylist) override;
  const RuleSet* GetIeemSitelist() const override;
  const RuleSet* GetExternalSitelist() const override;

 private:
  // Returns true if there are any rules configured.
  bool IsActive() const;

  Decision GetDecisionImpl(const GURL& url) const;

  // Stores the rules from |src| in |dst|, by first calling CanonicalizeRule()
  // on them.
  void StoreRules(std::vector<std::unique_ptr<Rule>>* dst,
                  const std::vector<std::string>& src);

  // CanonicalizeRule() has different output based on ParsingMode, so we need to
  // re-canonicalize them if the parsing mode changes.
  void OnPrefsChanged(BrowserSwitcherPrefs* prefs,
                      const std::vector<std::string>& changed_prefs);

  RuleSet ieem_sitelist_;
  RuleSet external_sitelist_;

  // Original values used for canonicalization.
  std::vector<std::string> original_ieem_sitelist_;
  std::vector<std::string> original_external_sitelist_;
  std::vector<std::string> original_external_greylist_;

  base::CallbackListSubscription prefs_changed_subscription_;

  BrowserSwitcherPrefs* const prefs_;
};

}  // namespace browser_switcher

#endif  // CHROME_BROWSER_BROWSER_SWITCHER_BROWSER_SWITCHER_SITELIST_H_
