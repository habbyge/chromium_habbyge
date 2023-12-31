// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SIGNIN_CORE_BROWSER_MIRROR_ACCOUNT_RECONCILOR_DELEGATE_H_
#define COMPONENTS_SIGNIN_CORE_BROWSER_MIRROR_ACCOUNT_RECONCILOR_DELEGATE_H_

#include <vector>

#include "components/signin/core/browser/account_reconcilor_delegate.h"
#include "components/signin/public/identity_manager/identity_manager.h"

namespace signin {

// AccountReconcilorDelegate specialized for Mirror.
class MirrorAccountReconcilorDelegate : public AccountReconcilorDelegate,
                                        public IdentityManager::Observer {
 public:
  explicit MirrorAccountReconcilorDelegate(IdentityManager* identity_manager);

  MirrorAccountReconcilorDelegate(const MirrorAccountReconcilorDelegate&) =
      delete;
  MirrorAccountReconcilorDelegate& operator=(
      const MirrorAccountReconcilorDelegate&) = delete;

  ~MirrorAccountReconcilorDelegate() override;

 protected:
  // AccountReconcilorDelegate:
  // TODO(sinhak): Make this private after deleting
  // |ChromeOSAccountReconcilorDelegate|.
  bool IsReconcileEnabled() const override;

  IdentityManager* GetIdentityManager() const { return identity_manager_; }

 private:
  // AccountReconcilorDelegate:
  gaia::GaiaSource GetGaiaApiSource() const override;
  bool ShouldAbortReconcileIfPrimaryHasError() const override;
  ConsentLevel GetConsentLevelForPrimaryAccount() const override;
  CoreAccountId GetFirstGaiaAccountForReconcile(
      const std::vector<CoreAccountId>& chrome_accounts,
      const std::vector<gaia::ListedAccount>& gaia_accounts,
      const CoreAccountId& primary_account,
      bool first_execution,
      bool will_logout) const override;
  std::vector<CoreAccountId> GetChromeAccountsForReconcile(
      const std::vector<CoreAccountId>& chrome_accounts,
      const CoreAccountId& primary_account,
      const std::vector<gaia::ListedAccount>& gaia_accounts,
      bool first_execution,
      bool primary_has_error,
      const gaia::MultiloginMode mode) const override;

  // IdentityManager::Observer:
  void OnPrimaryAccountChanged(const PrimaryAccountChangeEvent& event) override;

  void UpdateReconcilorStatus();

  IdentityManager* identity_manager_;
  bool reconcile_enabled_;
};

}  // namespace signin

#endif  // COMPONENTS_SIGNIN_CORE_BROWSER_MIRROR_ACCOUNT_RECONCILOR_DELEGATE_H_
