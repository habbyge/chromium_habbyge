// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/chromeos_buildflags.h"
#include "chrome/browser/sync/test/integration/apps_helper.h"
#include "chrome/browser/sync/test/integration/web_apps_sync_test_base.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/browser/web_applications/web_app_helpers.h"
#include "chrome/browser/web_applications/web_app_proto_utils.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "components/sync/base/model_type.h"
#include "components/sync/base/user_selectable_type.h"
#include "components/sync/driver/sync_service_impl.h"
#include "components/sync/driver/sync_user_settings.h"
#include "components/sync/protocol/app_specifics.pb.h"
#include "components/sync/protocol/entity_specifics.pb.h"
#include "components/sync/protocol/extension_specifics.pb.h"
#include "components/sync/test/fake_server/fake_server_verifier.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/constants/ash_features.h"
#include "chrome/browser/sync/test/integration/sync_consent_optional_sync_test.h"
#endif

using syncer::UserSelectableType;
using syncer::UserSelectableTypeSet;

namespace web_app {
namespace {

// Default time (creation and last modified) used when creating entities.
const int64_t kDefaultTime = 1234L;

// Default version used when creating extension entities.
const char kVersion[] = "1.0.0.1";

#if BUILDFLAG(IS_CHROMEOS_ASH)

// These tests test the new Web Apps system with next generation sync.
//
// Chrome OS syncs Web apps as a browser type, so it shouldn't be affected by
// the OS sync feature.
class SingleClientWebAppsOsSyncTest : public SyncConsentOptionalSyncTest {
 public:
  SingleClientWebAppsOsSyncTest()
      : SyncConsentOptionalSyncTest(SINGLE_CLIENT) {}
  ~SingleClientWebAppsOsSyncTest() override = default;
};

IN_PROC_BROWSER_TEST_F(SingleClientWebAppsOsSyncTest,
                       DisablingOsSyncFeatureKeepsWebAppsEnabled) {
  ASSERT_TRUE(chromeos::features::IsSyncConsentOptionalEnabled());
  ASSERT_TRUE(SetupSync());
  syncer::SyncServiceImpl* service = GetSyncService(0);
  syncer::SyncUserSettings* settings = service->GetUserSettings();

  EXPECT_TRUE(settings->IsOsSyncFeatureEnabled());
  EXPECT_TRUE(service->GetActiveDataTypes().Has(syncer::WEB_APPS));

  settings->SetOsSyncFeatureEnabled(false);
  EXPECT_FALSE(settings->IsOsSyncFeatureEnabled());
  // WEB_APPS is a browser type, so they shouldn't be affected by the OS sync.
  EXPECT_TRUE(service->GetActiveDataTypes().Has(syncer::WEB_APPS));
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

class SingleClientWebAppsSyncTest : public WebAppsSyncTestBase {
 public:
  SingleClientWebAppsSyncTest() : WebAppsSyncTestBase(SINGLE_CLIENT) {}
  ~SingleClientWebAppsSyncTest() override = default;

  bool SetupClients() override {
    if (!SyncTest::SetupClients()) {
      return false;
    }

    for (Profile* profile : GetAllProfiles()) {
      auto* web_app_provider = WebAppProvider::GetForTest(profile);
      base::RunLoop loop;
      web_app_provider->on_registry_ready().Post(FROM_HERE, loop.QuitClosure());
      loop.Run();
    }
    return true;
  }

  void AwaitWebAppQuiescence() {
    ASSERT_TRUE(AwaitQuiescence());
    apps_helper::AwaitWebAppQuiescence(GetAllProfiles());
  }

  void InjectWebAppEntityToFakeServer(
      const std::string& app_id,
      const GURL& url,
      absl::optional<std::string> manifest_id = absl::nullopt) {
    WebApp app(app_id);
    app.SetName(app_id);
    app.SetStartUrl(url);
    app.SetUserDisplayMode(DisplayMode::kBrowser);
    app.SetManifestId(manifest_id);

    WebApp::SyncFallbackData sync_fallback_data;
    sync_fallback_data.name = app_id;
    app.SetSyncFallbackData(std::move(sync_fallback_data));

    sync_pb::EntitySpecifics entity_specifics;

    *(entity_specifics.mutable_web_app()) = WebAppToSyncProto(app);

    fake_server_->InjectEntity(
        syncer::PersistentUniqueClientEntity::CreateFromSpecificsForTesting(
            /*non_unique_name=*/"", app_id, entity_specifics, kDefaultTime,
            kDefaultTime));
  }

  // TODO(crbug.com/1065748): remove this function and any tests.
  void InjectBookmarkAppEntityToFakeServer(const std::string& app_id,
                                           const std::string& url) {
    sync_pb::EntitySpecifics entity;
    sync_pb::AppSpecifics* app_specifics = entity.mutable_app();

    sync_pb::ExtensionSpecifics* extension_specifics =
        app_specifics->mutable_extension();
    // Required fields for a valid ExtensionSpecifics
    extension_specifics->set_id(app_id);
    extension_specifics->set_update_url(url);
    extension_specifics->set_version(kVersion);
    fake_server_->InjectEntity(
        syncer::PersistentUniqueClientEntity::CreateFromSpecificsForTesting(
            /*non_unique_name=*/"", app_id, entity, kDefaultTime,
            kDefaultTime));
  }
};

IN_PROC_BROWSER_TEST_F(SingleClientWebAppsSyncTest,
                       DisablingSelectedTypeDisablesModelType) {
  ASSERT_TRUE(SetupSync());
  syncer::SyncServiceImpl* service = GetSyncService(0);
  syncer::SyncUserSettings* settings = service->GetUserSettings();
  ASSERT_TRUE(settings->GetSelectedTypes().Has(UserSelectableType::kApps));
  EXPECT_TRUE(service->GetActiveDataTypes().Has(syncer::WEB_APPS));

  settings->SetSelectedTypes(false, UserSelectableTypeSet());
  ASSERT_FALSE(settings->GetSelectedTypes().Has(UserSelectableType::kApps));
  EXPECT_FALSE(service->GetActiveDataTypes().Has(syncer::WEB_APPS));
}

IN_PROC_BROWSER_TEST_F(SingleClientWebAppsSyncTest,
                       AppWithValidIdSyncInstalled) {
  GURL url("https://example.com/");
  const std::string app_id = GenerateAppId(/*manifest_id=*/absl::nullopt, url);
  InjectWebAppEntityToFakeServer(app_id, url);
  ASSERT_TRUE(SetupSync());
  AwaitWebAppQuiescence();

  auto& web_app_registrar =
      WebAppProvider::GetForTest(GetProfile(0))->registrar();
  EXPECT_TRUE(web_app_registrar.IsInstalled(app_id));
}

IN_PROC_BROWSER_TEST_F(SingleClientWebAppsSyncTest,
                       PRE_BookmarkAppNotSyncInstalled) {
  std::string url = "https://example.com/";
  const std::string app_id =
      GenerateAppId(/*manifest_id=*/absl::nullopt, GURL(url));
  InjectBookmarkAppEntityToFakeServer(app_id, url);
  ASSERT_TRUE(SetupSync());
  AwaitWebAppQuiescence();
  auto& web_app_registrar =
      WebAppProvider::GetForTest(GetProfile(0))->registrar();

  EXPECT_EQ(web_app_registrar.GetAppById(app_id), nullptr);
}

// Make sure bookmark app is not installed by BMO migration on
// re-initialization.
IN_PROC_BROWSER_TEST_F(SingleClientWebAppsSyncTest,
                       BookmarkAppNotSyncInstalled) {
  std::string url = "https://example.com/";
  const std::string app_id =
      GenerateAppId(/*manifest_id=*/absl::nullopt, GURL(url));
  ASSERT_TRUE(SetupSync());
  AwaitWebAppQuiescence();
  auto& web_app_registrar =
      WebAppProvider::GetForTest(GetProfile(0))->registrar();

  EXPECT_FALSE(web_app_registrar.IsInstalled(app_id));
}

// Web app install should not commit APPS sync entity.
IN_PROC_BROWSER_TEST_F(SingleClientWebAppsSyncTest,
                       AppInstallDoNotSyncBookmarkApp) {
  ASSERT_TRUE(SetupSync());
  WebApplicationInfo info;
  std::string name = "Test name";
  info.title = base::UTF8ToUTF16(name);
  info.description = u"Test description";
  info.start_url = GURL("http://www.chromium.org/path");
  info.scope = GURL("http://www.chromium.org/");
  AppId app_id = apps_helper::InstallWebApp(GetProfile(0), info);
  ASSERT_TRUE(SetupSync());

  fake_server::FakeServerVerifier fake_server_verifier(fake_server_.get());
  EXPECT_TRUE(fake_server_verifier.VerifyEntityCountByTypeAndName(
      1, syncer::WEB_APPS, name));
  EXPECT_TRUE(fake_server_verifier.VerifyEntityCountByTypeAndName(
      0, syncer::APPS, name));
}

IN_PROC_BROWSER_TEST_F(SingleClientWebAppsSyncTest,
                       AppWithMalformedIdNotSyncInstalled) {
  const std::string app_id = "invalid_id";
  GURL url("https://example.com/");
  InjectWebAppEntityToFakeServer(app_id, url);
  ASSERT_TRUE(SetupSync());
  AwaitWebAppQuiescence();

  auto& web_app_registrar =
      WebAppProvider::GetForTest(GetProfile(0))->registrar();

  EXPECT_FALSE(web_app_registrar.IsInstalled(app_id));
}

IN_PROC_BROWSER_TEST_F(SingleClientWebAppsSyncTest,
                       AppWithIdSpecifiedSyncInstalled) {
  const absl::optional<std::string> manifest_id("explicit_id");
  GURL url("https://example.com/start");
  const std::string app_id = GenerateAppId(manifest_id, url);

  InjectWebAppEntityToFakeServer(app_id, url, manifest_id);
  ASSERT_TRUE(SetupSync());
  AwaitWebAppQuiescence();

  auto& web_app_registrar =
      WebAppProvider::GetForTest(GetProfile(0))->registrar();

  EXPECT_TRUE(web_app_registrar.IsInstalled(app_id));

  WebApplicationInfo info;
  std::string name = "Test name";
  info.title = base::UTF8ToUTF16(app_id);
  info.description = u"Test description";
  info.start_url = url;
  info.scope = url;
  info.manifest_id = manifest_id;
  const AppId installed_app_id =
      apps_helper::InstallWebApp(GetProfile(0), info);

  const std::string expected_app_id = GenerateAppId(
      /*manifest_id=*/absl::nullopt, GURL("https://example.com/explicit_id"));
  EXPECT_EQ(expected_app_id, installed_app_id);
}

IN_PROC_BROWSER_TEST_F(SingleClientWebAppsSyncTest,
                       AppWithIdSpecifiedAsEmptyStringSyncInstalled) {
  const absl::optional<std::string> manifest_id("");
  GURL url("https://example.com/start");
  const std::string app_id = GenerateAppId(manifest_id, url);

  InjectWebAppEntityToFakeServer(app_id, url, manifest_id);
  ASSERT_TRUE(SetupSync());
  AwaitWebAppQuiescence();

  auto& web_app_registrar =
      WebAppProvider::GetForTest(GetProfile(0))->registrar();

  EXPECT_TRUE(web_app_registrar.IsInstalled(app_id));

  WebApplicationInfo info;
  std::string name = "Test name";
  info.title = base::UTF8ToUTF16(app_id);
  info.description = u"Test description";
  info.start_url = url;
  info.scope = url;
  info.manifest_id = manifest_id;
  const AppId installed_app_id =
      apps_helper::InstallWebApp(GetProfile(0), info);

  const std::string expected_app_id = GenerateAppId(
      /*manifest_id=*/absl::nullopt, GURL("https://example.com/"));
  EXPECT_EQ(expected_app_id, installed_app_id);
}
}  // namespace
}  // namespace web_app
