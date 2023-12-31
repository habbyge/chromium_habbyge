// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>
#include <vector>

#include "ash/public/cpp/shelf_model.h"
#include "base/callback.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/test/bind.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/ash/apps/apk_web_app_installer.h"
#include "chrome/browser/ash/apps/apk_web_app_service.h"
#include "chrome/browser/ash/arc/arc_util.h"
#include "chrome/browser/ash/arc/session/arc_session_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/ash/shelf/chrome_shelf_controller.h"
#include "chrome/browser/ui/ash/shelf/chrome_shelf_controller_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/web_applications/test/web_app_browsertest_util.h"
#include "chrome/browser/web_applications/app_registrar_observer.h"
#include "chrome/browser/web_applications/test/web_app_icon_test_utils.h"
#include "chrome/browser/web_applications/test/web_app_install_test_utils.h"
#include "chrome/browser/web_applications/test/web_app_test_utils.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/browser/web_applications/web_app_constants.h"
#include "chrome/browser/web_applications/web_app_icon_generator.h"
#include "chrome/browser/web_applications/web_app_id.h"
#include "chrome/browser/web_applications/web_app_install_finalizer.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/arc/test/arc_util_test_support.h"
#include "components/arc/test/connection_holder_util.h"
#include "components/arc/test/fake_app_instance.h"
#include "components/webapps/browser/installable/installable_metrics.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

const char kPackageName[] = "com.google.maps";
const char kAppTitle[] = "Google Maps";
const char kAppUrl[] = "https://www.google.com/maps/";
const char kAppScope[] = "https://www.google.com/";
constexpr char kLastAppId[] = "last_app_id";
const char kAppActivity[] = "test.app.activity";
const char kAppActivity1[] = "test.app1.activity";
const char kPackageName1[] = "com.test.app";

arc::mojom::RawIconPngDataPtr GetFakeIconBytes() {
  auto fake_app_instance =
      std::make_unique<arc::FakeAppInstance>(/*app_host=*/nullptr);
  return fake_app_instance->GenerateIconResponse(128, /*app_icon=*/true);
}

std::unique_ptr<WebApplicationInfo> CreateWebApplicationInfo(const GURL& url) {
  auto web_application_info = std::make_unique<WebApplicationInfo>();
  web_application_info->start_url = url;
  web_application_info->title = u"App Title";
  web_application_info->theme_color = SK_ColorBLUE;
  web_application_info->scope = url.Resolve("scope");
  web_application_info->display_mode = web_app::DisplayMode::kBrowser;
  web_application_info->user_display_mode = web_app::DisplayMode::kStandalone;

  const std::vector<SquareSizePx> sizes_px{web_app::icon_size::k256,
                                           web_app::icon_size::k512};
  const std::vector<SkColor> colors{SK_ColorRED, SK_ColorYELLOW};
  web_app::AddIconsToWebApplicationInfo(web_application_info.get(), url,
                                        {{IconPurpose::ANY, sizes_px, colors}});

  return web_application_info;
}

void ExpectInitialIconInfosFromWebApplicationInfo(
    const std::vector<apps::IconInfo>& icon_infos,
    const GURL& url) {
  EXPECT_EQ(2u, icon_infos.size());

  EXPECT_EQ(url.Resolve("icon-256.png"), icon_infos[0].url);
  EXPECT_EQ(256, icon_infos[0].square_size_px);
  EXPECT_EQ(apps::IconInfo::Purpose::kAny, icon_infos[0].purpose);

  EXPECT_EQ(url.Resolve("icon-512.png"), icon_infos[1].url);
  EXPECT_EQ(512, icon_infos[1].square_size_px);
  EXPECT_EQ(apps::IconInfo::Purpose::kAny, icon_infos[1].purpose);
}

void ExpectInitialManifestFieldsFromWebApplicationInfo(
    const web_app::WebAppIconManager& icon_manager,
    const web_app::WebApp* web_app,
    const GURL& url) {
  // Manifest fields:
  EXPECT_EQ(web_app->name(), "App Title");
  EXPECT_EQ(web_app->start_url(), url);
  EXPECT_EQ(web_app->scope().spec(), url.Resolve("scope"));
  EXPECT_EQ(web_app->display_mode(), web_app::DisplayMode::kBrowser);

  ASSERT_TRUE(web_app->theme_color().has_value());
  EXPECT_EQ(SK_ColorBLUE, web_app->theme_color().value());

  ASSERT_TRUE(web_app->sync_fallback_data().theme_color.has_value());
  EXPECT_EQ(SK_ColorBLUE, web_app->sync_fallback_data().theme_color.value());

  EXPECT_EQ("App Title", web_app->sync_fallback_data().name);
  EXPECT_EQ(url.Resolve("scope"), web_app->sync_fallback_data().scope);
  {
    SCOPED_TRACE("web_app->manifest_icons()");
    ExpectInitialIconInfosFromWebApplicationInfo(web_app->manifest_icons(),
                                                 url);
  }
  {
    SCOPED_TRACE("web_app->sync_fallback_data().icon_infos");
    ExpectInitialIconInfosFromWebApplicationInfo(
        web_app->sync_fallback_data().icon_infos, url);
  }

  // Manifest Resources:
  EXPECT_EQ(web_app::IconManagerReadAppIconPixel(
                icon_manager, web_app->app_id(), /*size=*/256),
            SK_ColorRED);

  EXPECT_EQ(web_app::IconManagerReadAppIconPixel(
                icon_manager, web_app->app_id(), /*size=*/512),
            SK_ColorYELLOW);

  // User preferences:
  EXPECT_EQ(web_app->user_display_mode(), web_app::DisplayMode::kStandalone);
}

}  // namespace

namespace ash {

class ApkWebAppInstallerBrowserTest : public InProcessBrowserTest,
                                      public web_app::AppRegistrarObserver,
                                      public ArcAppListPrefs::Observer {
 public:
  ApkWebAppInstallerBrowserTest() = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    arc::SetArcAvailableCommandLineForTesting(command_line);
  }

  void SetUpInProcessBrowserTestFixture() override {
    arc::ArcSessionManager::SetUiEnabledForTesting(false);
  }

  void EnableArc() {
    arc::SetArcPlayStoreEnabledForProfile(browser()->profile(), true);

    arc_app_list_prefs_ = ArcAppListPrefs::Get(browser()->profile());
    DCHECK(arc_app_list_prefs_);

    base::RunLoop run_loop;
    arc_app_list_prefs_->SetDefaultAppsReadyCallback(run_loop.QuitClosure());
    run_loop.Run();

    app_instance_ = std::make_unique<arc::FakeAppInstance>(arc_app_list_prefs_);
    arc_app_list_prefs_->app_connection_holder()->SetInstance(
        app_instance_.get());
    WaitForInstanceReady(arc_app_list_prefs_->app_connection_holder());
  }

  void DisableArc() {
    arc_app_list_prefs_->app_connection_holder()->CloseInstance(
        app_instance_.get());
    app_instance_.reset();
    arc::ArcSessionManager::Get()->Shutdown();
    arc_app_list_prefs_ = nullptr;
  }

  void SetUpWebApps() {
    provider_ = web_app::WebAppProvider::GetForTest(browser()->profile());
    DCHECK(provider_);
    observation_.Observe(&provider_->registrar());
  }

  void TearDownWebApps() {
    provider_ = nullptr;
    observation_.Reset();
  }

  void SetUpOnMainThread() override {
    EnableArc();
    SetUpWebApps();
  }

  void TearDownOnMainThread() override {
    DisableArc();
    TearDownWebApps();
  }

  arc::mojom::ArcPackageInfoPtr GetWebAppPackage(
      const std::string& package_name,
      const std::string& app_title) {
    auto package = GetArcAppPackage(package_name, app_title);
    package->web_app_info = GetWebAppInfo(app_title);

    return package;
  }

  arc::mojom::ArcPackageInfoPtr GetArcAppPackage(
      const std::string& package_name,
      const std::string& app_title) {
    auto package = arc::mojom::ArcPackageInfo::New();
    package->package_name = package_name;
    package->package_version = 1;
    package->last_backup_android_id = 1;
    package->last_backup_time = 1;
    package->sync = true;
    package->system = false;

    return package;
  }

  arc::mojom::WebAppInfoPtr GetWebAppInfo(const std::string& app_title) {
    return arc::mojom::WebAppInfo::New(app_title, kAppUrl, kAppScope, 100000);
  }

  ApkWebAppService* apk_web_app_service() {
    return ApkWebAppService::Get(browser()->profile());
  }

  const web_app::WebAppIconManager& icon_manager() {
    return web_app::WebAppProvider::GetForTest(browser()->profile())
        ->icon_manager();
  }

  // Sets a callback to be called whenever an app is completely uninstalled and
  // removed from the Registrar.
  void set_app_uninstalled_callback(
      base::RepeatingCallback<void(const web_app::AppId&)> callback) {
    app_uninstalled_callback_ = callback;
  }

  // web_app::AppRegistrarObserver overrides.
  void OnWebAppInstalled(const web_app::AppId& web_app_id) override {
    installed_web_app_id_ = web_app_id;
    installed_web_app_name_ =
        provider_->registrar().GetAppShortName(web_app_id);
  }

  void OnWebAppWillBeUninstalled(const web_app::AppId& web_app_id) override {
    uninstalled_web_app_id_ = web_app_id;
  }

  void OnWebAppUninstalled(const web_app::AppId& app_id) override {
    if (app_uninstalled_callback_) {
      app_uninstalled_callback_.Run(app_id);
    }
  }

  // ArcAppListPrefs::Observer:
  void OnPackageRemoved(const std::string& package_name,
                        bool uninstalled) override {
    EXPECT_TRUE(uninstalled);
    removed_package_ = package_name;
  }

  void Reset() {
    removed_package_.clear();
    installed_web_app_id_.clear();
    installed_web_app_name_.clear();
    uninstalled_web_app_id_.clear();
  }

 protected:
  base::ScopedObservation<web_app::WebAppRegistrar,
                          web_app::AppRegistrarObserver>
      observation_{this};
  ArcAppListPrefs* arc_app_list_prefs_ = nullptr;
  web_app::WebAppProvider* provider_ = nullptr;
  std::unique_ptr<arc::FakeAppInstance> app_instance_;
  base::RepeatingCallback<void(const web_app::AppId&)>
      app_uninstalled_callback_;

  std::string removed_package_;
  web_app::AppId installed_web_app_id_;
  std::string installed_web_app_name_;
  web_app::AppId uninstalled_web_app_id_;
};

class ApkWebAppInstallerDelayedArcStartBrowserTest
    : public ApkWebAppInstallerBrowserTest {
  // Don't start ARC.
  void SetUpOnMainThread() override { SetUpWebApps(); }

  // Don't tear down ARC.
  void TearDownOnMainThread() override { TearDownWebApps(); }
};

class ApkWebAppInstallerWithShelfControllerBrowserTest
    : public ApkWebAppInstallerBrowserTest {
 public:
  // ApkWebAppInstallerBrowserTest
  void SetUpOnMainThread() override {
    EnableArc();
    SetUpWebApps();
    shelf_controller_ = ChromeShelfController::instance();
    ASSERT_TRUE(shelf_controller_);
  }

  // ApkWebAppInstallerBrowserTest
  void TearDownOnMainThread() override {
    DisableArc();
    TearDownWebApps();
  }

 protected:
  ChromeShelfController* shelf_controller_;
};

// Test the full installation and uninstallation flow.
IN_PROC_BROWSER_TEST_F(ApkWebAppInstallerBrowserTest, InstallAndUninstall) {
  ApkWebAppService* service = apk_web_app_service();
  service->SetArcAppListPrefsForTesting(arc_app_list_prefs_);

  web_app::AppId app_id;
  {
    base::RunLoop run_loop;
    service->SetWebAppInstalledCallbackForTesting(base::BindLambdaForTesting(
        [&](const std::string& package_name, const web_app::AppId& web_app_id) {
          EXPECT_EQ(kAppTitle, installed_web_app_name_);
          EXPECT_EQ(web_app_id, installed_web_app_id_);
          EXPECT_EQ(kPackageName, package_name);
          app_id = web_app_id;
          run_loop.Quit();
        }));

    app_instance_->SendPackageAdded(GetWebAppPackage(kPackageName, kAppTitle));
    run_loop.Run();
  }

  // Now send an uninstallation call from ARC, which should uninstall the
  // installed web app.
  {
    base::RunLoop run_loop;
    service->SetWebAppUninstalledCallbackForTesting(base::BindLambdaForTesting(
        [&](const std::string& package_name, const web_app::AppId& web_app_id) {
          EXPECT_FALSE(uninstalled_web_app_id_.empty());
          EXPECT_EQ(app_id, uninstalled_web_app_id_);
          // No UninstallPackage happened.
          EXPECT_EQ("", package_name);
          run_loop.Quit();
        }));

    app_instance_->SendPackageUninstalled(kPackageName);
    run_loop.Run();
  }
}

// Test installation via PackageListRefreshed.
IN_PROC_BROWSER_TEST_F(ApkWebAppInstallerBrowserTest, PackageListRefreshed) {
  ApkWebAppService* service = apk_web_app_service();
  service->SetArcAppListPrefsForTesting(arc_app_list_prefs_);

  std::vector<arc::mojom::ArcPackageInfoPtr> packages;
  packages.push_back(GetWebAppPackage(kPackageName, kAppTitle));

  base::RunLoop run_loop;
  service->SetWebAppInstalledCallbackForTesting(base::BindLambdaForTesting(
      [&](const std::string& package_name, const web_app::AppId& web_app_id) {
        EXPECT_EQ(kAppTitle, installed_web_app_name_);
        EXPECT_EQ(web_app_id, installed_web_app_id_);
        run_loop.Quit();
      }));

  app_instance_->SendRefreshPackageList(std::move(packages));
  run_loop.Run();
}

// Test uninstallation when ARC isn't running.
IN_PROC_BROWSER_TEST_F(ApkWebAppInstallerDelayedArcStartBrowserTest,
                       DelayedUninstall) {
  ApkWebAppService* service = apk_web_app_service();

  {
    base::RunLoop run_loop;
    service->SetWebAppInstalledCallbackForTesting(base::BindLambdaForTesting(
        [&](const std::string& package_name, const web_app::AppId& web_app_id) {
          EXPECT_EQ(kAppTitle, installed_web_app_name_);
          EXPECT_EQ(web_app_id, installed_web_app_id_);
          EXPECT_EQ(kPackageName, package_name);
          run_loop.Quit();
        }));

    // Install an app from the raw data as if ARC had installed it.
    service->OnDidGetWebAppIcon(kPackageName, GetWebAppInfo(kAppTitle),
                                GetFakeIconBytes());
    run_loop.Run();
  }

  // Uninstall the app on the web apps side. ARC uninstallation should be
  // queued.
  {
    base::RunLoop run_loop;
    provider_->install_finalizer().UninstallExternalWebApp(
        installed_web_app_id_, webapps::WebappUninstallSource::kArc,
        base::BindLambdaForTesting([&](bool uninstalled) {
          EXPECT_TRUE(uninstalled);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  // Start up ARC and set the package to be installed.
  EnableArc();
  app_instance_->SendPackageAdded(GetWebAppPackage(kPackageName, kAppTitle));

  // Trigger a package refresh, which should call to ARC to remove the package.
  arc_app_list_prefs_->AddObserver(this);
  service->SetArcAppListPrefsForTesting(arc_app_list_prefs_);
  std::vector<arc::mojom::ArcPackageInfoPtr> packages;
  packages.push_back(GetWebAppPackage(kPackageName, kAppTitle));
  app_instance_->SendRefreshPackageList(std::move(packages));

  EXPECT_EQ(kPackageName, removed_package_);

  arc_app_list_prefs_->RemoveObserver(this);
  DisableArc();
}

// Test an upgrade that becomes a web app and then stops being a web app.
IN_PROC_BROWSER_TEST_F(ApkWebAppInstallerBrowserTest,
                       UpgradeToWebAppAndToArcApp) {
  ApkWebAppService* service = apk_web_app_service();
  service->SetArcAppListPrefsForTesting(arc_app_list_prefs_);
  app_instance_->SendPackageAdded(GetArcAppPackage(kPackageName, kAppTitle));

  EXPECT_TRUE(installed_web_app_id_.empty());
  EXPECT_TRUE(uninstalled_web_app_id_.empty());

  // Send a second package added call from ARC, upgrading the package to a web
  // app.
  {
    base::RunLoop run_loop;
    service->SetWebAppInstalledCallbackForTesting(base::BindLambdaForTesting(
        [&](const std::string& package_name, const web_app::AppId& web_app_id) {
          EXPECT_TRUE(uninstalled_web_app_id_.empty());
          EXPECT_EQ(kAppTitle, installed_web_app_name_);
          run_loop.Quit();
        }));

    app_instance_->SendPackageAdded(GetWebAppPackage(kPackageName, kAppTitle));
    run_loop.Run();
  }

  // Send an package added call from ARC, upgrading the package to not be a
  // web app. The web app should be uninstalled.
  {
    base::RunLoop run_loop;
    service->SetWebAppUninstalledCallbackForTesting(base::BindLambdaForTesting(
        [&](const std::string& package_name, const web_app::AppId& web_app_id) {
          EXPECT_EQ(uninstalled_web_app_id_, installed_web_app_id_);
          run_loop.Quit();
        }));
    app_instance_->SendPackageAdded(GetArcAppPackage(kPackageName, kAppTitle));
    run_loop.Run();
  }

  Reset();
  EXPECT_TRUE(installed_web_app_id_.empty());
  EXPECT_TRUE(installed_web_app_name_.empty());

  // Upgrade the package to a web app again and make sure it is installed again.
  {
    base::RunLoop run_loop;
    service->SetWebAppInstalledCallbackForTesting(base::BindLambdaForTesting(
        [&](const std::string& package_name, const web_app::AppId& web_app_id) {
          EXPECT_FALSE(installed_web_app_id_.empty());
          EXPECT_EQ(kAppTitle, installed_web_app_name_);
          run_loop.Quit();
        }));

    app_instance_->SendPackageAdded(GetWebAppPackage(kPackageName, kAppTitle));
    run_loop.Run();
  }
}

// Test that when an ARC-installed Web App is uninstalled and then reinstalled
// as a regular web app, it is not treated as ARC-installed.
IN_PROC_BROWSER_TEST_F(ApkWebAppInstallerBrowserTest,
                       UninstallAndReinstallAsWebApp) {
  ApkWebAppService* service = apk_web_app_service();
  service->SetArcAppListPrefsForTesting(arc_app_list_prefs_);

  // Install the Web App from ARC.
  web_app::AppId app_id;
  {
    base::RunLoop run_loop;
    service->SetWebAppInstalledCallbackForTesting(base::BindLambdaForTesting(
        [&](const std::string& package_name, const web_app::AppId& web_app_id) {
          EXPECT_EQ(web_app_id, installed_web_app_id_);
          app_id = web_app_id;
          run_loop.Quit();
        }));

    app_instance_->SendPackageAdded(GetWebAppPackage(kPackageName, kAppTitle));
    run_loop.Run();
  }

  ASSERT_TRUE(service->IsWebAppInstalledFromArc(app_id));

  // Uninstall the Web App from ARC.
  {
    base::RunLoop run_loop;
    // Wait until the app is completely uninstalled.
    set_app_uninstalled_callback(
        base::BindLambdaForTesting([&](const web_app::AppId& web_app_id) {
          EXPECT_EQ(app_id, web_app_id);
          run_loop.Quit();
        }));

    app_instance_->SendPackageUninstalled(kPackageName);
    run_loop.Run();
  }

  ASSERT_FALSE(service->IsWebAppInstalledFromArc(app_id));

  // Reinstall the Web App through the Browser.
  web_app::AppId non_arc_app_id = web_app::test::InstallDummyWebApp(
      browser()->profile(), kAppTitle, GURL(kAppUrl));
  ASSERT_EQ(app_id, non_arc_app_id);
  ASSERT_FALSE(service->IsWebAppInstalledFromArc(app_id));
}

IN_PROC_BROWSER_TEST_F(ApkWebAppInstallerWithShelfControllerBrowserTest,
                       CheckPinStateAfterUpdate) {
  ApkWebAppService* service = apk_web_app_service();
  service->SetArcAppListPrefsForTesting(arc_app_list_prefs_);
  app_instance_->SendPackageAdded(GetArcAppPackage(kPackageName, kAppTitle));
  const std::string arc_app_id =
      ArcAppListPrefs::GetAppId(kPackageName, kAppActivity);

  /// Create an app and add to the package.
  arc::mojom::AppInfo app;
  app.name = kAppTitle;
  app.package_name = kPackageName;
  app.activity = kAppActivity;
  app.sticky = true;
  app_instance_->SendPackageAppListRefreshed(kPackageName, {app});

  EXPECT_TRUE(installed_web_app_id_.empty());
  EXPECT_TRUE(uninstalled_web_app_id_.empty());
  EXPECT_FALSE(shelf_controller_->IsAppPinned(arc_app_id));

  // Pin the app to the shelf.
  PinAppWithIDToShelf(arc_app_id);
  EXPECT_TRUE(shelf_controller_->IsAppPinned(arc_app_id));

  int pin_index = shelf_controller_->PinnedItemIndexByAppID(arc_app_id);

  arc_app_list_prefs_->SetPackagePrefs(kPackageName, kLastAppId,
                                       base::Value(arc_app_id));

  std::string keep_web_app_id;
  // Update ARC app to web app and check that the pinned app has
  // been updated.
  {
    base::RunLoop run_loop;
    service->SetWebAppInstalledCallbackForTesting(base::BindLambdaForTesting(
        [&](const std::string& package_name, const web_app::AppId& web_app_id) {
          // Web apps update the shelf asynchronously, so flush the App
          // Service's mojo calls to ensure that happens.
          auto* proxy =
              apps::AppServiceProxyFactory::GetForProfile(browser()->profile());
          proxy->FlushMojoCallsForTesting();
          keep_web_app_id = web_app_id;
          EXPECT_FALSE(installed_web_app_id_.empty());
          EXPECT_FALSE(shelf_controller_->IsAppPinned(arc_app_id));
          EXPECT_TRUE(shelf_controller_->IsAppPinned(keep_web_app_id));
          int new_index =
              shelf_controller_->PinnedItemIndexByAppID(keep_web_app_id);
          EXPECT_EQ(pin_index, new_index);
          run_loop.Quit();
        }));

    app_instance_->SendPackageAdded(GetWebAppPackage(kPackageName, kAppTitle));
    run_loop.Run();
  }

  // Move the pin location of the app.
  app_instance_->SendPackageAdded(GetArcAppPackage(kPackageName1, kAppTitle));
  const std::string arc_app_id1 =
      ArcAppListPrefs::GetAppId(kPackageName1, kAppActivity1);
  shelf_controller_->PinAppAtIndex(arc_app_id1, pin_index);
  EXPECT_EQ(pin_index, shelf_controller_->PinnedItemIndexByAppID(arc_app_id1));

  // The app that was previously pinned will be shifted one to the right.
  pin_index += 1;
  EXPECT_EQ(pin_index,
            shelf_controller_->PinnedItemIndexByAppID(keep_web_app_id));

  // Update to ARC app and check the pinned app has updated.
  {
    base::RunLoop run_loop;
    service->SetWebAppUninstalledCallbackForTesting(base::BindLambdaForTesting(
        [&](const std::string& package_name, const web_app::AppId& web_app_id) {
          EXPECT_FALSE(uninstalled_web_app_id_.empty());
          EXPECT_FALSE(shelf_controller_->IsAppPinned(web_app_id));
          EXPECT_TRUE(shelf_controller_->IsAppPinned(arc_app_id));
          int new_index = shelf_controller_->PinnedItemIndexByAppID(arc_app_id);
          EXPECT_EQ(pin_index, new_index);
          EXPECT_FALSE(shelf_controller_->IsAppPinned(keep_web_app_id));
          run_loop.Quit();
        }));
    app_instance_->SendPackageAdded(GetArcAppPackage(kPackageName, kAppTitle));
    run_loop.Run();
  }
}

// Test that when a regular synced Web App is installed first and then same ARC
// Web App is installed we don't overwrite manifest fields obtained from full
// online install (especially sync fallback data).
IN_PROC_BROWSER_TEST_F(ApkWebAppInstallerBrowserTest,
                       InstallRegularWebAppFirstThenInstallFromArc) {
  ApkWebAppService* service = apk_web_app_service();
  service->SetArcAppListPrefsForTesting(arc_app_list_prefs_);

  // Install the Web App as if the user installs it.
  std::unique_ptr<WebApplicationInfo> web_application_info =
      CreateWebApplicationInfo(GURL(kAppUrl));

  web_app::AppId app_id = web_app::test::InstallWebApp(
      browser()->profile(), std::move(web_application_info),
      /*overwrite_existing_manifest_fields=*/true,
      webapps::WebappInstallSource::SYNC);

  ASSERT_FALSE(service->IsWebAppInstalledFromArc(app_id));

  const web_app::WebApp* web_app = provider_->registrar().GetAppById(app_id);
  ASSERT_TRUE(web_app);

  EXPECT_TRUE(web_app->IsSynced());
  EXPECT_FALSE(web_app->IsWebAppStoreInstalledApp());

  {
    SCOPED_TRACE("Expect initial manifest fields.");
    ExpectInitialManifestFieldsFromWebApplicationInfo(icon_manager(), web_app,
                                                      GURL(kAppUrl));
  }

  // Install the Web App from ARC.
  {
    base::RunLoop run_loop;
    service->SetWebAppInstalledCallbackForTesting(
        base::BindLambdaForTesting([&](const std::string& package_name,
                                       const web_app::AppId& installed_app_id) {
          EXPECT_EQ(app_id, installed_app_id);
          run_loop.Quit();
        }));

    app_instance_->SendPackageAdded(GetWebAppPackage(kPackageName, kAppTitle));
    run_loop.Run();
  }

  ASSERT_TRUE(service->IsWebAppInstalledFromArc(app_id));

  EXPECT_EQ(web_app, provider_->registrar().GetAppById(app_id));

  EXPECT_TRUE(web_app->IsSynced());
  EXPECT_TRUE(web_app->IsWebAppStoreInstalledApp());

  {
    SCOPED_TRACE("Expect same manifest fields, no overwrites.");
    ExpectInitialManifestFieldsFromWebApplicationInfo(icon_manager(), web_app,
                                                      GURL(kAppUrl));
  }
}

// Test that when ARC Web App is installed first and then same regular synced
// Web App is installed we overwrite the apk manifest fields with fields
// obtained from full online install (especially sync fallback data).
IN_PROC_BROWSER_TEST_F(ApkWebAppInstallerBrowserTest,
                       InstallFromArcFirstThenRegularWebApp) {
  ApkWebAppService* service = apk_web_app_service();
  service->SetArcAppListPrefsForTesting(arc_app_list_prefs_);

  web_app::AppId app_id;

  // Install the Web App from ARC.
  {
    base::RunLoop run_loop;
    service->SetWebAppInstalledCallbackForTesting(base::BindLambdaForTesting(
        [&](const std::string& package_name, const web_app::AppId& apk_app_id) {
          app_id = apk_app_id;
          run_loop.Quit();
        }));

    app_instance_->SendPackageAdded(GetWebAppPackage(kPackageName, kAppTitle));
    run_loop.Run();
  }

  ASSERT_TRUE(service->IsWebAppInstalledFromArc(app_id));

  const web_app::WebApp* web_app = provider_->registrar().GetAppById(app_id);
  ASSERT_TRUE(web_app);

  EXPECT_TRUE(web_app->IsWebAppStoreInstalledApp());
  EXPECT_FALSE(web_app->IsSynced());

  // Install the Web App as if the user installs it.
  std::unique_ptr<WebApplicationInfo> web_application_info =
      CreateWebApplicationInfo(GURL(kAppUrl));

  web_app::AppId web_app_id = web_app::test::InstallWebApp(
      browser()->profile(), std::move(web_application_info),
      /*overwrite_existing_manifest_fields=*/true,
      webapps::WebappInstallSource::SYNC);
  ASSERT_EQ(app_id, web_app_id);

  EXPECT_TRUE(service->IsWebAppInstalledFromArc(app_id));

  EXPECT_EQ(web_app, provider_->registrar().GetAppById(app_id));

  EXPECT_TRUE(web_app->IsWebAppStoreInstalledApp());
  EXPECT_TRUE(web_app->IsSynced());

  {
    SCOPED_TRACE(
        "Expect online manifest fields, the offline fields from ARC have been "
        "overwritten.");
    ExpectInitialManifestFieldsFromWebApplicationInfo(icon_manager(), web_app,
                                                      GURL(kAppUrl));
  }
}

}  // namespace ash
