// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/crosapi/cert_database_ash.h"

#include "base/bind.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/net/nss_service.h"
#include "chrome/browser/net/nss_service_factory.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chromeos/crosapi/mojom/cert_database.mojom.h"
#include "chromeos/login/login_state/login_state.h"
#include "chromeos/tpm/tpm_token_info_getter.h"
#include "components/account_id/account_id.h"
#include "components/user_manager/user.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "crypto/nss_util_internal.h"
#include "net/cert/nss_cert_database.h"

namespace {
using GotDbCallback =
    base::OnceCallback<void(unsigned long private_slot_id,
                            absl::optional<unsigned long> system_slot_id)>;

void GotCertDbOnIOThread(GotDbCallback ui_callback,
                         net::NSSCertDatabase* cert_db) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(cert_db);

  // Technically `PK11_GetSlotID` returns CK_SLOT_ID, but it is guaranteed by
  // PKCS #11 standard to be unsigned long and it is more convenient to use here
  // because it will be sent through mojo later.
  unsigned long private_slot_id =
      PK11_GetSlotID(cert_db->GetPrivateSlot().get());

  absl::optional<unsigned long> system_slot_id;
  crypto::ScopedPK11Slot system_slot = cert_db->GetSystemSlot();
  if (system_slot)
    system_slot_id = PK11_GetSlotID(system_slot.get());

  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(ui_callback), private_slot_id, system_slot_id));
}

void GetCertDbOnIOThread(GotDbCallback ui_callback,
                         NssCertDatabaseGetter database_getter) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  auto split_callback = base::SplitOnceCallback(
      base::BindOnce(&GotCertDbOnIOThread, std::move(ui_callback)));

  net::NSSCertDatabase* cert_db =
      std::move(database_getter).Run(std::move(split_callback.first));
  // If the NSS database was already available, |cert_db| is non-null and
  // |did_get_cert_db_callback| has not been called. Call it explicitly.
  if (cert_db)
    std::move(split_callback.second).Run(cert_db);
}

}  // namespace

namespace crosapi {

CertDatabaseAsh::CertDatabaseAsh() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(chromeos::LoginState::IsInitialized());
  chromeos::LoginState::Get()->AddObserver(this);
}

CertDatabaseAsh::~CertDatabaseAsh() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  chromeos::LoginState::Get()->RemoveObserver(this);
}

void CertDatabaseAsh::BindReceiver(
    mojo::PendingReceiver<mojom::CertDatabase> pending_receiver) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  receivers_.Add(this, std::move(pending_receiver));
}

void CertDatabaseAsh::GetCertDatabaseInfo(
    GetCertDatabaseInfoCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // TODO(crbug.com/1146430): For now Lacros-Chrome will initialize certificate
  // database only in session. Revisit later to decide what to do on the login
  // screen.
  if (!chromeos::LoginState::Get()->IsUserLoggedIn()) {
    LOG(ERROR) << "Not implemented";
    std::move(callback).Run(nullptr);
    return;
  }

  if (!is_cert_database_ready_.has_value()) {
    WaitForCertDatabaseReady(std::move(callback));
    return;
  }

  const user_manager::User* user =
      user_manager::UserManager::Get()->GetPrimaryUser();

  // If user is not available or the database was previously attempted to be
  // loaded, and failed, don't retry, just return an empty result that indicates
  // error.
  if (!user || !is_cert_database_ready_.value()) {
    std::move(callback).Run(nullptr);
    return;
  }

  // Otherwise, if the TPM was already loaded previously, let the
  // caller know.
  // TODO(crbug.com/1146430) For now Lacros-Chrome loads chaps and has access to
  // TPM operations only for affiliated users, because it gives access to
  // system token. Find a way to give unaffiliated users access only to user TPM
  // token.
  mojom::GetCertDatabaseInfoResultPtr result =
      mojom::GetCertDatabaseInfoResult::New();
  result->should_load_chaps = user->IsAffiliated();
  result->private_slot_id = private_slot_id_;
  result->enable_system_slot = system_slot_id_.has_value();
  result->system_slot_id =
      result->enable_system_slot ? system_slot_id_.value() : 0;

  // TODO(b/200784079): This is backwards compatibility code. It can be
  // removed in ChromeOS-M100.
  result->DEPRECATED_software_nss_db_path =
      crypto::GetSoftwareNSSDBPath(
          ProfileManager::GetPrimaryUserProfile()->GetPath())
          .value();

  std::move(callback).Run(std::move(result));
}

void CertDatabaseAsh::WaitForCertDatabaseReady(
    GetCertDatabaseInfoCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  Profile* profile = ProfileManager::GetPrimaryUserProfile();
  DCHECK(profile);

  auto got_db_callback =
      base::BindOnce(&CertDatabaseAsh::OnCertDatabaseReady,
                     weak_factory_.GetWeakPtr(), std::move(callback));

  content::GetIOThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(&GetCertDbOnIOThread, std::move(got_db_callback),
                     NssServiceFactory::GetForContext(profile)
                         ->CreateNSSCertDatabaseGetterForIOThread()));
}

void CertDatabaseAsh::OnCertDatabaseReady(
    GetCertDatabaseInfoCallback callback,
    unsigned long private_slot_id,
    absl::optional<unsigned long> system_slot_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  is_cert_database_ready_ = true;
  private_slot_id_ = private_slot_id;
  system_slot_id_ = system_slot_id;

  // Calling the initial method again. Since |is_tpm_token_ready_| is not empty
  // this time, it will return some result via mojo.
  GetCertDatabaseInfo(std::move(callback));
}

void CertDatabaseAsh::LoggedInStateChanged() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // Cached result is valid only within one session and should be reset on
  // sign out. Currently it is not necessary to reset it on sign in, but doesn't
  // hurt.
  is_cert_database_ready_.reset();
}

}  // namespace crosapi
