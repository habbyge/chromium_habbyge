// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/web_apps/web_app_integration_test_driver.h"

#include <ostream>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/containers/flat_map.h"
#include "base/files/file_util.h"
#include "base/notreached.h"
#include "base/strings/pattern.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/test/bind.h"
#include "base/values.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/banners/test_app_banner_manager_desktop.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/intent_picker_tab_helper.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "chrome/browser/ui/views/location_bar/custom_tab_bar_view.h"
#include "chrome/browser/ui/views/page_action/page_action_icon_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/browser/ui/web_applications/test/web_app_browsertest_util.h"
#include "chrome/browser/ui/web_applications/web_app_dialog_utils.h"
#include "chrome/browser/ui/web_applications/web_app_launch_utils.h"
#include "chrome/browser/ui/web_applications/web_app_menu_model.h"
#include "chrome/browser/ui/webui/ntp/app_launcher_handler.h"
#include "chrome/browser/web_applications/manifest_update_manager.h"
#include "chrome/browser/web_applications/policy/web_app_policy_constants.h"
#include "chrome/browser/web_applications/policy/web_app_policy_manager.h"
#include "chrome/browser/web_applications/test/web_app_install_test_utils.h"
#include "chrome/browser/web_applications/test/web_app_test_observers.h"
#include "chrome/browser/web_applications/web_app_constants.h"
#include "chrome/browser/web_applications/web_app_id.h"
#include "chrome/browser/web_applications/web_app_install_finalizer.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "chrome/browser/web_applications/web_app_sync_bridge.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/webapps/browser/installable/installable_metrics.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/test_web_ui.h"
#include "extensions/browser/extension_dialog_auto_confirm.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "services/network/public/cpp/network_switches.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/mojom/manifest/display_mode.mojom-shared.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/widget/any_widget_observer.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/ui/views/apps/app_dialog/app_uninstall_dialog_view.h"
#include "components/services/app_service/public/mojom/types.mojom-shared.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/constants/ash_features.h"
#endif

namespace web_app {

namespace {

const base::flat_map<std::string, std::string> g_site_mode_to_path = {
    {"SiteA", "site_a"},
    {"SiteB", "site_b"},
    {"SiteC", "site_c"},
    {"SiteAFoo", "site_a/foo"},
    {"SiteABar", "site_a/bar"}};

class TestAppLauncherHandler : public AppLauncherHandler {
 public:
  TestAppLauncherHandler(extensions::ExtensionService* extension_service,
                         WebAppProvider* provider,
                         content::TestWebUI* test_web_ui)
      : AppLauncherHandler(extension_service, provider) {
    DCHECK(test_web_ui->GetWebContents());
    DCHECK(test_web_ui->GetWebContents()->GetBrowserContext());
    set_web_ui(test_web_ui);
  }
};

class BrowserAddedWaiter final : public BrowserListObserver {
 public:
  BrowserAddedWaiter() { BrowserList::AddObserver(this); }
  ~BrowserAddedWaiter() override = default;

  void Wait() { run_loop_.Run(); }

  // BrowserListObserver
  void OnBrowserAdded(Browser* browser) override {
    browser_added_ = browser;
    BrowserList::RemoveObserver(this);
    // Post a task to ensure the Remove event has been dispatched to all
    // observers.
    base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                  run_loop_.QuitClosure());
  }
  Browser* browser_added() const { return browser_added_; }

 private:
  base::RunLoop run_loop_;
  Browser* browser_added_ = nullptr;
};

Browser* GetBrowserForAppId(const AppId& app_id) {
  BrowserList* browser_list = BrowserList::GetInstance();
  for (Browser* browser : *browser_list) {
    if (AppBrowserController::IsForWebApp(browser, app_id))
      return browser;
  }
  return nullptr;
}

absl::optional<ProfileState> GetStateForProfile(StateSnapshot* state_snapshot,
                                                Profile* profile) {
  DCHECK(state_snapshot);
  DCHECK(profile);
  auto it = state_snapshot->profiles.find(profile);
  return it == state_snapshot->profiles.end()
             ? absl::nullopt
             : absl::make_optional<ProfileState>(it->second);
}

absl::optional<BrowserState> GetStateForBrowser(StateSnapshot* state_snapshot,
                                                Profile* profile,
                                                Browser* browser) {
  absl::optional<ProfileState> profile_state =
      GetStateForProfile(state_snapshot, profile);
  if (!profile_state) {
    return absl::nullopt;
  }

  auto it = profile_state->browsers.find(browser);
  return it == profile_state->browsers.end()
             ? absl::nullopt
             : absl::make_optional<BrowserState>(it->second);
}

absl::optional<TabState> GetStateForActiveTab(BrowserState browser_state) {
  if (!browser_state.active_tab) {
    return absl::nullopt;
  }

  auto it = browser_state.tabs.find(browser_state.active_tab);
  DCHECK(it != browser_state.tabs.end());
  return absl::make_optional<TabState>(it->second);
}

absl::optional<AppState> GetStateForAppId(StateSnapshot* state_snapshot,
                                          Profile* profile,
                                          web_app::AppId id) {
  absl::optional<ProfileState> profile_state =
      GetStateForProfile(state_snapshot, profile);
  if (!profile_state) {
    return absl::nullopt;
  }

  auto it = profile_state->apps.find(id);
  return it == profile_state->apps.end()
             ? absl::nullopt
             : absl::make_optional<AppState>(it->second);
}

}  // anonymous namespace

BrowserState::BrowserState(
    Browser* browser_ptr,
    base::flat_map<content::WebContents*, TabState> tab_state,
    content::WebContents* active_web_contents,
    const AppId& app_id,
    bool install_icon_visible,
    bool launch_icon_visible)
    : browser(browser_ptr),
      tabs(std::move(tab_state)),
      active_tab(active_web_contents),
      app_id(app_id),
      install_icon_shown(install_icon_visible),
      launch_icon_shown(launch_icon_visible) {}
BrowserState::~BrowserState() = default;
BrowserState::BrowserState(const BrowserState&) = default;
bool BrowserState::operator==(const BrowserState& other) const {
  return browser == other.browser && tabs == other.tabs &&
         active_tab == other.active_tab && app_id == other.app_id &&
         install_icon_shown == other.install_icon_shown &&
         launch_icon_shown == other.launch_icon_shown;
}

AppState::AppState(web_app::AppId app_id,
                   const std::string app_name,
                   const GURL app_scope,
                   const blink::mojom::DisplayMode& effective_display_mode,
                   const blink::mojom::DisplayMode& user_display_mode,
                   bool installed_locally)
    : id(app_id),
      name(app_name),
      scope(app_scope),
      effective_display_mode(effective_display_mode),
      user_display_mode(user_display_mode),
      is_installed_locally(installed_locally) {}
AppState::~AppState() = default;
AppState::AppState(const AppState&) = default;
bool AppState::operator==(const AppState& other) const {
  return id == other.id && name == other.name && scope == other.scope &&
         effective_display_mode == other.effective_display_mode &&
         user_display_mode == other.user_display_mode &&
         is_installed_locally == other.is_installed_locally;
}

ProfileState::ProfileState(base::flat_map<Browser*, BrowserState> browser_state,
                           base::flat_map<web_app::AppId, AppState> app_state)
    : browsers(std::move(browser_state)), apps(std::move(app_state)) {}
ProfileState::~ProfileState() = default;
ProfileState::ProfileState(const ProfileState&) = default;
bool ProfileState::operator==(const ProfileState& other) const {
  return browsers == other.browsers && apps == other.apps;
}

StateSnapshot::StateSnapshot(
    base::flat_map<Profile*, ProfileState> profile_state)
    : profiles(std::move(profile_state)) {}
StateSnapshot::~StateSnapshot() = default;
StateSnapshot::StateSnapshot(const StateSnapshot&) = default;
bool StateSnapshot::operator==(const StateSnapshot& other) const {
  return profiles == other.profiles;
}

std::ostream& operator<<(std::ostream& os, const StateSnapshot& state) {
  base::Value root(base::Value::Type::DICTIONARY);
  base::Value& profiles_value =
      *root.SetKey("profiles", base::Value(base::Value::Type::DICTIONARY));
  for (const auto& profile_pair : state.profiles) {
    base::Value profile_value(base::Value::Type::DICTIONARY);

    base::Value browsers_value(base::Value::Type::DICTIONARY);
    const ProfileState& profile = profile_pair.second;
    for (const auto& browser_pair : profile.browsers) {
      base::Value browser_value(base::Value::Type::DICTIONARY);
      const BrowserState& browser = browser_pair.second;

      browser_value.SetStringKey("browser",
                                 base::StringPrintf("%p", browser.browser));

      base::Value tab_values(base::Value::Type::DICTIONARY);
      for (const auto& tab_pair : browser.tabs) {
        base::Value tab_value(base::Value::Type::DICTIONARY);
        const TabState& tab = tab_pair.second;
        tab_value.SetStringKey("url", tab.url.spec());
        tab_value.SetBoolKey("is_installable", tab.is_installable);
        tab_values.SetKey(base::StringPrintf("%p", tab_pair.first),
                          std::move(tab_value));
      }
      browser_value.SetKey("tabs", std::move(tab_values));
      browser_value.SetStringKey("active_tab",
                                 base::StringPrintf("%p", browser.active_tab));
      browser_value.SetStringKey("app_id", browser.app_id);
      browser_value.SetBoolKey("install_icon_shown",
                               browser.install_icon_shown);
      browser_value.SetBoolKey("launch_icon_shown", browser.launch_icon_shown);

      browsers_value.SetKey(base::StringPrintf("%p", browser_pair.first),
                            std::move(browser_value));
    }
    base::Value app_values(base::Value::Type::DICTIONARY);
    for (const auto& app_pair : profile.apps) {
      base::Value app_value(base::Value::Type::DICTIONARY);
      const AppState& app = app_pair.second;

      app_value.SetStringKey("id", app.id);
      app_value.SetStringKey("name", app.name);
      app_value.SetIntKey("effective_display_mode",
                          static_cast<int>(app.effective_display_mode));
      app_value.SetIntKey("user_display_mode",
                          static_cast<int>(app.effective_display_mode));
      app_value.SetBoolKey("is_installed_locally", app.is_installed_locally);

      app_values.SetKey(app_pair.first, std::move(app_value));
    }

    profile_value.SetKey("browsers", std::move(browsers_value));
    profile_value.SetKey("apps", std::move(app_values));
    profiles_value.SetKey(base::StringPrintf("%p", profile_pair.first),
                          std::move(profile_value));
  }
  os << root.DebugString();
  return os;
}

WebAppIntegrationTestDriver::WebAppIntegrationTestDriver(TestDelegate* delegate)
    : delegate_(delegate) {}

WebAppIntegrationTestDriver::~WebAppIntegrationTestDriver() = default;

void WebAppIntegrationTestDriver::SetUp() {
  webapps::TestAppBannerManagerDesktop::SetUp();
}

void WebAppIntegrationTestDriver::SetUpOnMainThread() {
  os_hooks_suppress_ = OsIntegrationManager::ScopedSuppressOsHooksForTesting();

  // Only support manifest updates on non-sync tests, as the current
  // infrastructure here only supports listening on one profile.
  if (!delegate_->IsSyncTest()) {
    observation_.Observe(&provider()->registrar());
  }
  web_app::test::WaitUntilReady(
      web_app::WebAppProvider::GetForTest(browser()->profile()));
}

void WebAppIntegrationTestDriver::TearDownOnMainThread() {
  observation_.Reset();
}

void WebAppIntegrationTestDriver::CloseCustomToolbar() {
  BeforeStateChangeAction();
  ASSERT_TRUE(app_browser());
  BrowserView* app_view = BrowserView::GetBrowserViewForBrowser(app_browser());
  content::WebContents* web_contents = app_view->GetActiveWebContents();
  content::TestNavigationObserver nav_observer(web_contents);
  EXPECT_TRUE(app_view->toolbar()
                  ->custom_tab_bar()
                  ->close_button_for_testing()
                  ->GetVisible());
  app_view->toolbar()->custom_tab_bar()->GoBackToAppForTesting();
  nav_observer.Wait();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::ClosePwa() {
  BeforeStateChangeAction();
  ASSERT_TRUE(app_browser()) << "No current app browser";
  app_browser()->window()->Close();
  ui_test_utils::WaitForBrowserToClose(app_browser());
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::InstallCreateShortcutTabbed(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  MaybeNavigateTabbedBrowserInScope(site_mode);
  InstallCreateShortcut(/*open_in_window=*/false);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::InstallCreateShortcutWindowed(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  MaybeNavigateTabbedBrowserInScope(site_mode);
  InstallCreateShortcut(/*open_in_window=*/true);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::InstallMenuOption(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  MaybeNavigateTabbedBrowserInScope(site_mode);
  chrome::SetAutoAcceptPWAInstallConfirmationForTesting(/*auto_accept=*/true);
  content::WindowedNotificationObserver app_loaded_observer(
      content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
      content::NotificationService::AllSources());
  WebAppTestInstallWithOsHooksObserver install_observer(profile());
  install_observer.BeginListening();
  CHECK(chrome::ExecuteCommand(browser(), IDC_INSTALL_PWA));
  app_loaded_observer.Wait();
  active_app_id_ = install_observer.Wait();
  app_browser_ = GetBrowserForAppId(active_app_id_);
  chrome::SetAutoAcceptPWAInstallConfirmationForTesting(/*auto_accept=*/false);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::InstallLocally(const std::string& site_mode) {
  BeforeStateChangeAction();
  absl::optional<AppState> app_state = GetAppBySiteMode(
      before_state_change_action_state_.get(), profile(), site_mode);
  ASSERT_TRUE(app_state) << "App not installed: " << site_mode;
  content::TestWebUI test_web_ui;
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetWebContentsAt(0);
  DCHECK(web_contents);
  test_web_ui.set_web_contents(web_contents);
  TestAppLauncherHandler handler(/*extension_service=*/nullptr, provider(),
                                 &test_web_ui);
  base::ListValue web_app_ids;
  web_app_ids.Append(app_state->id);

  WebAppTestInstallWithOsHooksObserver observer(profile());
  observer.BeginListening();
  handler.HandleInstallAppLocally(&web_app_ids);
  observer.Wait();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::InstallOmniboxIcon(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  MaybeNavigateTabbedBrowserInScope(site_mode);
  chrome::SetAutoAcceptPWAInstallConfirmationForTesting(true);

  web_app::AppId app_id;
  base::RunLoop run_loop;
  web_app::SetInstalledCallbackForTesting(base::BindLambdaForTesting(
      [&app_id, &run_loop](const web_app::AppId& installed_app_id,
                           web_app::InstallResultCode code) {
        app_id = installed_app_id;
        run_loop.Quit();
      }));
  content::WindowedNotificationObserver app_loaded_observer(
      content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
      content::NotificationService::AllSources());

  ASSERT_TRUE(pwa_install_view()->GetVisible());
  WebAppTestInstallWithOsHooksObserver install_observer(profile());
  install_observer.BeginListening();
  pwa_install_view()->ExecuteForTesting();

  run_loop.Run();
  app_loaded_observer.Wait();
  active_app_id_ = install_observer.Wait();
  DCHECK_EQ(app_id, active_app_id_);
  app_browser_ = GetBrowserForAppId(active_app_id_);

  chrome::SetAutoAcceptPWAInstallConfirmationForTesting(false);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::InstallPolicyAppTabbedNoShortcut(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  InstallPolicyAppInternal(site_mode,
                           base::Value(kDefaultLaunchContainerTabValue),
                           /*create_shortcut=*/false);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::InstallPolicyAppTabbedShortcut(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  InstallPolicyAppInternal(site_mode,
                           base::Value(kDefaultLaunchContainerTabValue),
                           /*create_shortcut=*/true);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::InstallPolicyAppWindowedNoShortcut(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  InstallPolicyAppInternal(site_mode,
                           base::Value(kDefaultLaunchContainerWindowValue),
                           /*create_shortcut=*/false);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::InstallPolicyAppWindowedShortcut(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  InstallPolicyAppInternal(site_mode,
                           base::Value(kDefaultLaunchContainerWindowValue),
                           /*create_shortcut=*/true);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::LaunchFromChromeApps(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  absl::optional<AppState> app_state = GetAppBySiteMode(
      before_state_change_action_state_.get(), profile(), site_mode);
  ASSERT_TRUE(app_state.has_value())
      << "No app installed for site: " << site_mode;
  auto app_id = app_state->id;
  WebAppRegistrar& app_registrar = provider()->registrar();
  DisplayMode display_mode = app_registrar.GetAppEffectiveDisplayMode(app_id);
  if (display_mode == blink::mojom::DisplayMode::kBrowser) {
    ui_test_utils::UrlLoadObserver url_observer(
        app_registrar.GetAppLaunchUrl(app_id),
        content::NotificationService::AllSources());
    Browser* browser = LaunchBrowserForWebAppInTab(profile(), app_id);
    url_observer.Wait();
    auto* app_banner_manager =
        webapps::TestAppBannerManagerDesktop::FromWebContents(
            GetCurrentTab(browser));
    app_banner_manager->WaitForInstallableCheck();
  } else {
    app_browser_ = LaunchWebAppBrowserAndWait(profile(), app_id);
    active_app_id_ = app_id;
    app_browser_ = GetBrowserForAppId(active_app_id_);
  }
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::LaunchFromLaunchIcon(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  NavigateBrowser(site_mode);

  EXPECT_TRUE(intent_picker_view()->GetVisible());

  BrowserAddedWaiter browser_added_waiter;

  if (!IntentPickerBubbleView::intent_picker_bubble()) {
    views::NamedWidgetShownWaiter waiter(
        views::test::AnyWidgetTestPasskey{},
        IntentPickerBubbleView::kViewClassName);
    EXPECT_FALSE(IntentPickerBubbleView::intent_picker_bubble());
    intent_picker_view()->ExecuteForTesting();
    waiter.WaitIfNeededAndGet();
  }

  ASSERT_TRUE(IntentPickerBubbleView::intent_picker_bubble());
  EXPECT_TRUE(IntentPickerBubbleView::intent_picker_bubble()->GetVisible());

  IntentPickerBubbleView::intent_picker_bubble()->AcceptDialog();
  browser_added_waiter.Wait();
  app_browser_ = browser_added_waiter.browser_added();
  active_app_id_ = app_browser()->app_controller()->app_id();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::NavigateBrowser(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  NavigateTabbedBrowserToSite(GetInScopeURL(site_mode));
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::NavigatePwaSiteATo(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  app_browser_ = GetAppBrowserForSite("SiteA");
  NavigateToURLAndWait(app_browser(), GetAppStartURL(site_mode), false);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::NavigateNotfoundUrl() {
  BeforeStateChangeAction();
  NavigateTabbedBrowserToSite(
      embedded_test_server()->GetURL("/non-existant/index.html"));
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::NavigateTabbedBrowserToSite(const GURL& url) {
  BeforeStateChangeAction();
  DCHECK(browser());
  content::WebContents* web_contents = GetCurrentTab(browser());
  auto* app_banner_manager =
      webapps::TestAppBannerManagerDesktop::FromWebContents(web_contents);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  app_banner_manager->WaitForInstallableCheck();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::ManifestUpdateDisplayMinimal(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  // TODO(dmurph): Create a map of supported manifest updates keyed on site
  // mode.
  ASSERT_EQ("SiteA", site_mode);
  ForceUpdateManifestContents(
      site_mode,
      GetAppURLForManifest(site_mode, blink::mojom::DisplayMode::kMinimalUi));
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::SetOpenInTab(const std::string& site_mode) {
  BeforeStateChangeAction();
  absl::optional<AppState> app_state = GetAppBySiteMode(
      before_state_change_action_state_.get(), profile(), site_mode);
  ASSERT_TRUE(app_state.has_value())
      << "No app installed for site: " << site_mode;
  auto app_id = app_state->id;
  auto& sync_bridge = WebAppProvider::GetForTest(profile())->sync_bridge();
  sync_bridge.SetAppUserDisplayMode(app_id, blink::mojom::DisplayMode::kBrowser,
                                    true);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::SetOpenInWindow(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  absl::optional<AppState> app_state = GetAppBySiteMode(
      before_state_change_action_state_.get(), profile(), site_mode);
  ASSERT_TRUE(app_state.has_value())
      << "No app installed for site: " << site_mode;
  auto app_id = app_state->id;
  auto& sync_bridge = WebAppProvider::GetForTest(profile())->sync_bridge();
  sync_bridge.SetAppUserDisplayMode(
      app_id, blink::mojom::DisplayMode::kStandalone, true);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::SwitchProfileClients(
    const std::string& client_mode) {
  BeforeStateChangeAction();
  std::vector<Profile*> profiles = delegate_->GetAllProfiles();
  ASSERT_EQ(2U, profiles.size())
      << "Cannot switch profile clients if delegate only supports one profile";
  DCHECK(active_profile_);
  if (client_mode == "Client1") {
    active_profile_ = profiles[0];
  } else if (client_mode == "Client2") {
    active_profile_ = profiles[1];
  } else {
    NOTREACHED() << "Unknown client mode " << client_mode;
  }
  active_browser_ = chrome::FindTabbedBrowser(
      active_profile_, /*match_original_profiles=*/false);
  delegate_->AwaitWebAppQuiescence();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::SyncTurnOff() {
  BeforeStateChangeAction();
  delegate_->SyncTurnOff();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::SyncTurnOn() {
  BeforeStateChangeAction();
  delegate_->SyncTurnOn();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::UninstallFromList(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  absl::optional<AppState> app_state = GetAppBySiteMode(
      before_state_change_action_state_.get(), profile(), site_mode);
  ASSERT_TRUE(app_state) << "App not installed: " << site_mode;

  WebAppTestUninstallObserver observer(profile());
  observer.BeginListening();
  extensions::ScopedTestDialogAutoConfirm auto_confirm(
      extensions::ScopedTestDialogAutoConfirm::ACCEPT);

#if BUILDFLAG(IS_CHROMEOS_ASH)
  apps::AppServiceProxy* app_service_proxy =
      apps::AppServiceProxyFactory::GetForProfile(profile());
  base::RunLoop run_loop;
  app_service_proxy->UninstallForTesting(app_state->id, nullptr,
                                         run_loop.QuitClosure());
  run_loop.Run();

  ASSERT_NE(nullptr, AppUninstallDialogView::GetActiveViewForTesting());
  AppUninstallDialogView::GetActiveViewForTesting()->AcceptDialog();
#elif BUILDFLAG(IS_CHROMEOS_LACROS)
  // The lacros implementation doesn't use a confirmation dialog so we can
  // call the normal method.
  apps::AppServiceProxy* app_service_proxy =
      apps::AppServiceProxyFactory::GetForProfile(profile());
  app_service_proxy->Uninstall(app_state->id,
                               apps::mojom::UninstallSource::kAppList, nullptr);
#else
  content::TestWebUI test_web_ui;
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetWebContentsAt(0);
  DCHECK(web_contents);
  test_web_ui.set_web_contents(web_contents);
  TestAppLauncherHandler handler(/*extension_service=*/nullptr, provider(),
                                 &test_web_ui);
  base::ListValue web_app_ids;
  web_app_ids.Append(app_state->id);
  handler.HandleUninstallApp(&web_app_ids);
#endif

  observer.Wait();

  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::UninstallFromMenu(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  absl::optional<AppState> app_state = GetAppBySiteMode(
      before_state_change_action_state_.get(), profile(), site_mode);
  ASSERT_TRUE(app_state.has_value())
      << "No app installed for site: " << site_mode;
  auto app_id = app_state->id;
  WebAppTestUninstallObserver observer(profile());
  observer.BeginListening({app_id});

  extensions::ScopedTestDialogAutoConfirm auto_confirm(
      extensions::ScopedTestDialogAutoConfirm::ACCEPT);
  Browser* app_browser = GetAppBrowserForSite(site_mode);
  ASSERT_TRUE(app_browser);
  auto app_menu_model =
      std::make_unique<WebAppMenuModel>(/*provider=*/nullptr, app_browser);
  app_menu_model->Init();
  ui::MenuModel* model = app_menu_model.get();
  int index = -1;
  const bool found = app_menu_model->GetModelAndIndexForCommandId(
      WebAppMenuModel::kUninstallAppCommandId, &model, &index);
  EXPECT_TRUE(found);
  EXPECT_TRUE(model->IsEnabledAt(index));

  app_menu_model->ExecuteCommand(WebAppMenuModel::kUninstallAppCommandId,
                                 /*event_flags=*/0);
  // The |app_menu_model| must be destroyed here, as the |observer| waits
  // until the app is fully uninstalled, which includes closing and deleting
  // the app_browser.
  app_menu_model.reset();
  observer.Wait();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::UninstallPolicyApp(
    const std::string& site_mode) {
  BeforeStateChangeAction();
  GURL url = GetAppStartURL(site_mode);
  auto policy_app = GetAppBySiteMode(before_state_change_action_state_.get(),
                                     profile(), site_mode);
  DCHECK(policy_app);
  base::RunLoop run_loop;
  WebAppTestRegistryObserverAdapter observer(profile());
  observer.SetWebAppUninstalledDelegate(
      base::BindLambdaForTesting([&](const AppId& app_id) {
        if (policy_app->id == app_id) {
          run_loop.Quit();
        }
      }));
  // If there are still install sources, the app might not be fully uninstalled,
  // so this will listen for the removal of the policy install source.
  provider()->install_finalizer().SetRemoveSourceCallbackForTesting(
      base::BindLambdaForTesting([&](const AppId& app_id) {
        if (policy_app->id == app_id)
          run_loop.Quit();
      }));
  {
    ListPrefUpdate update(profile()->GetPrefs(),
                          prefs::kWebAppInstallForceList);
    size_t removed_count =
        update->EraseListValueIf([&](const base::Value& item) {
          const base::Value* url_value = item.FindKey(kUrlKey);
          return url_value && url_value->GetString() == url.spec();
        });
    ASSERT_GT(removed_count, 0U);
  }
  run_loop.Run();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::CheckAppListEmpty() {
  BeforeStateCheckAction();
  absl::optional<ProfileState> state =
      GetStateForProfile(after_state_change_action_state_.get(), profile());
  ASSERT_TRUE(state.has_value());
  EXPECT_TRUE(state->apps.empty());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckAppInListNotLocallyInstalled(
    const std::string& site_mode) {
  BeforeStateCheckAction();
  // Note: This is a partially supported action.
  absl::optional<AppState> app_state = GetAppBySiteMode(
      after_state_change_action_state_.get(), profile(), site_mode);
  ASSERT_TRUE(app_state.has_value());
  EXPECT_FALSE(app_state->is_installed_locally);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckAppInListTabbed(
    const std::string& site_mode) {
  BeforeStateCheckAction();
  // Note: This is a partially supported action.
  absl::optional<AppState> app_state = GetAppBySiteMode(
      after_state_change_action_state_.get(), profile(), site_mode);
  ASSERT_TRUE(app_state.has_value());
  EXPECT_EQ(app_state->user_display_mode, blink::mojom::DisplayMode::kBrowser);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckAppInListWindowed(
    const std::string& site_mode) {
  BeforeStateCheckAction();
  // Note: This is a partially supported action.
  absl::optional<AppState> app_state = GetAppBySiteMode(
      after_state_change_action_state_.get(), profile(), site_mode);
  ASSERT_TRUE(app_state.has_value());
  EXPECT_EQ(app_state->user_display_mode,
            blink::mojom::DisplayMode::kStandalone);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckAppNavigationIsStartUrl() {
  BeforeStateCheckAction();
  ASSERT_FALSE(active_app_id_.empty());
  ASSERT_TRUE(app_browser());
  GURL url = app_browser()->tab_strip_model()->GetActiveWebContents()->GetURL();
  EXPECT_EQ(url, provider()->registrar().GetAppStartUrl(active_app_id_));
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckAppNotInList(
    const std::string& site_mode) {
  BeforeStateCheckAction();
  absl::optional<AppState> app_state = GetAppBySiteMode(
      after_state_change_action_state_.get(), profile(), site_mode);
  EXPECT_FALSE(app_state.has_value());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckInstallable() {
  BeforeStateCheckAction();
  absl::optional<BrowserState> browser_state = GetStateForBrowser(
      after_state_change_action_state_.get(), profile(), browser());
  ASSERT_TRUE(browser_state.has_value());
  absl::optional<TabState> active_tab =
      GetStateForActiveTab(browser_state.value());
  ASSERT_TRUE(active_tab.has_value());
  EXPECT_TRUE(active_tab->is_installable);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckInstallIconShown() {
  BeforeStateCheckAction();
  absl::optional<BrowserState> browser_state = GetStateForBrowser(
      after_state_change_action_state_.get(), profile(), browser());
  ASSERT_TRUE(browser_state.has_value());
  EXPECT_TRUE(browser_state->install_icon_shown);
  EXPECT_TRUE(pwa_install_view()->GetVisible());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckInstallIconNotShown() {
  BeforeStateCheckAction();
  absl::optional<BrowserState> browser_state = GetStateForBrowser(
      after_state_change_action_state_.get(), profile(), browser());
  ASSERT_TRUE(browser_state.has_value());
  EXPECT_FALSE(browser_state->install_icon_shown);
  EXPECT_FALSE(pwa_install_view()->GetVisible());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckLaunchIconShown() {
  BeforeStateCheckAction();
  absl::optional<BrowserState> browser_state = GetStateForBrowser(
      after_state_change_action_state_.get(), profile(), browser());
  ASSERT_TRUE(browser_state.has_value());
  EXPECT_TRUE(browser_state->launch_icon_shown);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckLaunchIconNotShown() {
  BeforeStateCheckAction();
  absl::optional<BrowserState> browser_state = GetStateForBrowser(
      after_state_change_action_state_.get(), profile(), browser());
  ASSERT_TRUE(browser_state.has_value());
  EXPECT_FALSE(browser_state->launch_icon_shown);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckTabCreated() {
  BeforeStateCheckAction();
  DCHECK(before_state_change_action_state_);
  absl::optional<BrowserState> most_recent_browser_state = GetStateForBrowser(
      after_state_change_action_state_.get(), profile(), browser());
  absl::optional<BrowserState> previous_browser_state = GetStateForBrowser(
      before_state_change_action_state_.get(), profile(), browser());
  ASSERT_TRUE(most_recent_browser_state.has_value());
  ASSERT_TRUE(previous_browser_state.has_value());
  EXPECT_GT(most_recent_browser_state->tabs.size(),
            previous_browser_state->tabs.size());

  absl::optional<TabState> active_tab =
      GetStateForActiveTab(most_recent_browser_state.value());
  ASSERT_TRUE(active_tab.has_value());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckCustomToolbar() {
  BeforeStateCheckAction();
  ASSERT_TRUE(app_browser());
  EXPECT_TRUE(app_browser()->app_controller()->ShouldShowCustomTabBar());
  BrowserView* app_view = BrowserView::GetBrowserViewForBrowser(app_browser());
  EXPECT_TRUE(app_view->toolbar()
                  ->custom_tab_bar()
                  ->close_button_for_testing()
                  ->GetVisible());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckUserDisplayModeInternal(
    DisplayMode display_mode) {
  BeforeStateCheckAction();
  absl::optional<AppState> app_state = GetStateForAppId(
      after_state_change_action_state_.get(), profile(), active_app_id_);
  ASSERT_TRUE(app_state.has_value());
  EXPECT_EQ(display_mode, app_state->user_display_mode);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckWindowClosed() {
  BeforeStateCheckAction();
  DCHECK(before_state_change_action_state_);
  absl::optional<ProfileState> after_action_profile =
      GetStateForProfile(after_state_change_action_state_.get(), profile());
  absl::optional<ProfileState> before_action_profile =
      GetStateForProfile(before_state_change_action_state_.get(), profile());
  ASSERT_TRUE(after_action_profile.has_value());
  ASSERT_TRUE(before_action_profile.has_value());
  EXPECT_LT(after_action_profile->browsers.size(),
            before_action_profile->browsers.size());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckWindowCreated() {
  BeforeStateCheckAction();
  DCHECK(before_state_change_action_state_);
  absl::optional<ProfileState> after_action_profile =
      GetStateForProfile(after_state_change_action_state_.get(), profile());
  absl::optional<ProfileState> before_action_profile =
      GetStateForProfile(before_state_change_action_state_.get(), profile());
  ASSERT_TRUE(after_action_profile.has_value());
  ASSERT_TRUE(before_action_profile.has_value());
  EXPECT_GT(after_action_profile->browsers.size(),
            before_action_profile->browsers.size())
      << "Before: \n"
      << *before_state_change_action_state_ << "\nAfter:\n"
      << *after_state_change_action_state_;
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckWindowDisplayMinimal() {
  BeforeStateCheckAction();
  DCHECK(app_browser());
  DCHECK(app_browser()->app_controller()->AsWebAppBrowserController());
  absl::optional<AppState> app_state = GetStateForAppId(
      after_state_change_action_state_.get(), profile(), active_app_id_);
  ASSERT_TRUE(app_state.has_value());

  content::WebContents* web_contents =
      app_browser()->tab_strip_model()->GetActiveWebContents();
  DCHECK(web_contents);
  DisplayMode window_display_mode =
      web_contents->GetDelegate()->GetDisplayMode(web_contents);

  EXPECT_TRUE(app_browser()->app_controller()->HasMinimalUiButtons());
  EXPECT_EQ(app_state->effective_display_mode,
            blink::mojom::DisplayMode::kMinimalUi);
  EXPECT_EQ(window_display_mode, blink::mojom::DisplayMode::kMinimalUi);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckWindowDisplayStandalone() {
  BeforeStateCheckAction();
  DCHECK(app_browser());
  DCHECK(app_browser()->app_controller()->AsWebAppBrowserController());
  absl::optional<AppState> app_state = GetStateForAppId(
      after_state_change_action_state_.get(), profile(), active_app_id_);
  ASSERT_TRUE(app_state.has_value());

  content::WebContents* web_contents =
      app_browser()->tab_strip_model()->GetActiveWebContents();
  DCHECK(web_contents);
  DisplayMode window_display_mode =
      web_contents->GetDelegate()->GetDisplayMode(web_contents);

  EXPECT_FALSE(app_browser()->app_controller()->HasMinimalUiButtons());
  EXPECT_EQ(app_state->effective_display_mode,
            blink::mojom::DisplayMode::kStandalone);
  EXPECT_EQ(window_display_mode, blink::mojom::DisplayMode::kStandalone);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::OnWebAppManifestUpdated(
    const AppId& app_id,
    base::StringPiece old_name) {
  DCHECK_EQ(1ul, delegate_->GetAllProfiles().size())
      << "Manifest update waiting only supported on single profile tests.";
  bool is_waiting = app_ids_with_pending_manifest_updates_.erase(app_id);
  ASSERT_TRUE(is_waiting) << "Received manifest update that was unexpected";
  if (waiting_for_update_id_ && app_id == waiting_for_update_id_.value()) {
    DCHECK(waiting_for_update_run_loop_);
    waiting_for_update_run_loop_->Quit();
    waiting_for_update_id_ = absl::nullopt;
    // The `BeforeState*Action()` methods check that the
    // `after_state_change_action_state_` has not changed from the current
    // state. This is great, except for the manifest update edge case, which can
    // happen asynchronously outside of actions. In this case, re-grab the
    // snapshot after the update.
    if (executing_action_level_ == 0 && after_state_change_action_state_)
      after_state_change_action_state_ = ConstructStateSnapshot();
  }
}

void WebAppIntegrationTestDriver::BeforeStateChangeAction() {
  ++executing_action_level_;
  std::unique_ptr<StateSnapshot> current_state = ConstructStateSnapshot();
  if (after_state_change_action_state_) {
    DCHECK_EQ(*after_state_change_action_state_, *current_state)
        << "State cannot be changed outside of state change actions.";
    before_state_change_action_state_ =
        std::move(after_state_change_action_state_);
  } else {
    before_state_change_action_state_ = std::move(current_state);
  }
}

void WebAppIntegrationTestDriver::AfterStateChangeAction() {
  DCHECK(executing_action_level_ > 0);
  --executing_action_level_;
  after_state_change_action_state_ = ConstructStateSnapshot();
  MaybeWaitForManifestUpdates();
}

void WebAppIntegrationTestDriver::BeforeStateCheckAction() {
  ++executing_action_level_;
  DCHECK(after_state_change_action_state_);
}

void WebAppIntegrationTestDriver::AfterStateCheckAction() {
  DCHECK(executing_action_level_ > 0);
  --executing_action_level_;
  if (!after_state_change_action_state_)
    return;
  DCHECK_EQ(*after_state_change_action_state_, *ConstructStateSnapshot());
}

GURL WebAppIntegrationTestDriver::GetAppStartURL(const std::string& site_mode) {
  DCHECK(g_site_mode_to_path.contains(site_mode));
  auto scope_url_path = g_site_mode_to_path.find(site_mode)->second;
  return embedded_test_server()->GetURL(
      base::StringPrintf("/web_apps/%s/basic.html", scope_url_path.c_str()));
}

absl::optional<AppState> WebAppIntegrationTestDriver::GetAppBySiteMode(
    StateSnapshot* state_snapshot,
    Profile* profile,
    const std::string& site_mode) {
  absl::optional<ProfileState> profile_state =
      GetStateForProfile(state_snapshot, profile);
  if (!profile_state) {
    return absl::nullopt;
  }

  GURL scope = GetScopeForSiteMode(site_mode);
  auto it =
      std::find_if(profile_state->apps.begin(), profile_state->apps.end(),
                   [scope](std::pair<web_app::AppId, AppState>& app_entry) {
                     return app_entry.second.scope == scope;
                   });

  return it == profile_state->apps.end()
             ? absl::nullopt
             : absl::make_optional<AppState>(it->second);
}

WebAppProvider* WebAppIntegrationTestDriver::GetProviderForProfile(
    Profile* profile) {
  return WebAppProvider::GetForTest(profile);
}

std::unique_ptr<StateSnapshot>
WebAppIntegrationTestDriver::ConstructStateSnapshot() {
  base::flat_map<Profile*, ProfileState> profile_state_map;
  for (Profile* profile : delegate_->GetAllProfiles()) {
    base::flat_map<Browser*, BrowserState> browser_state;
    auto* browser_list = BrowserList::GetInstance();
    for (Browser* browser : *browser_list) {
      if (browser->profile() != profile) {
        continue;
      }

      TabStripModel* tabs = browser->tab_strip_model();
      base::flat_map<content::WebContents*, TabState> tab_state_map;
      for (int i = 0; i < tabs->count(); ++i) {
        content::WebContents* tab = tabs->GetWebContentsAt(i);
        DCHECK(tab);
        GURL url = tab->GetURL();
        auto* app_banner_manager =
            webapps::TestAppBannerManagerDesktop::FromWebContents(tab);
        bool installable = app_banner_manager->WaitForInstallableCheck();

        tab_state_map.emplace(tab, TabState(url, installable));
      }
      content::WebContents* active_tab = tabs->GetActiveWebContents();
      bool is_app_browser = AppBrowserController::IsWebApp(browser);
      bool install_icon_visible = false;
      bool launch_icon_visible = false;
      if (!is_app_browser && active_tab != nullptr) {
        install_icon_visible = pwa_install_view()->GetVisible();
        launch_icon_visible = intent_picker_view()->GetVisible();
      }
      AppId app_id;
      if (AppBrowserController::IsWebApp(browser))
        app_id = browser->app_controller()->app_id();

      browser_state.emplace(
          browser, BrowserState(browser, tab_state_map, active_tab, app_id,
                                install_icon_visible, launch_icon_visible));
    }

    WebAppRegistrar& registrar = GetProviderForProfile(profile)->registrar();
    auto app_ids = registrar.GetAppIds();
    base::flat_map<AppId, AppState> app_state;
    for (const auto& app_id : app_ids) {
      app_state.emplace(app_id,
                        AppState(app_id, registrar.GetAppShortName(app_id),
                                 registrar.GetAppScope(app_id),
                                 registrar.GetAppEffectiveDisplayMode(app_id),
                                 registrar.GetAppUserDisplayMode(app_id),
                                 registrar.IsLocallyInstalled(app_id)));
    }
    profile_state_map.emplace(
        profile, ProfileState(std::move(browser_state), std::move(app_state)));
  }
  return std::make_unique<StateSnapshot>(std::move(profile_state_map));
}

GURL WebAppIntegrationTestDriver::GetAppURLForManifest(
    const std::string& site_mode,
    DisplayMode display_mode) {
  DCHECK(g_site_mode_to_path.contains(site_mode));
  auto scope_url_path = g_site_mode_to_path.find(site_mode)->second;
  std::string str_template = "/web_apps/%s/basic.html";
  if (display_mode == blink::mojom::DisplayMode::kMinimalUi) {
    str_template += "?manifest=manifest_minimal_ui.json";
  }
  return embedded_test_server()->GetURL(
      base::StringPrintf(str_template.c_str(), scope_url_path.c_str()));
}

content::WebContents* WebAppIntegrationTestDriver::GetCurrentTab(
    Browser* browser) {
  return browser->tab_strip_model()->GetActiveWebContents();
}

GURL WebAppIntegrationTestDriver::GetInScopeURL(const std::string& site_mode) {
  return GetAppStartURL(site_mode);
}

GURL WebAppIntegrationTestDriver::GetScopeForSiteMode(
    const std::string& site_mode) {
  DCHECK(g_site_mode_to_path.contains(site_mode));
  auto scope_url_path = g_site_mode_to_path.find(site_mode)->second;
  return embedded_test_server()->GetURL(
      base::StringPrintf("/web_apps/%s/", scope_url_path.c_str()));
}

void WebAppIntegrationTestDriver::InstallCreateShortcut(bool open_in_window) {
  chrome::SetAutoAcceptWebAppDialogForTesting(
      /*auto_accept=*/true,
      /*auto_open_in_window=*/open_in_window);
  WebAppTestInstallWithOsHooksObserver observer(profile());
  observer.BeginListening();
  content::WindowedNotificationObserver app_loaded_observer(
      content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
      content::NotificationService::AllSources());
  CHECK(chrome::ExecuteCommand(browser(), IDC_CREATE_SHORTCUT));
  active_app_id_ = observer.Wait();
  chrome::SetAutoAcceptWebAppDialogForTesting(false, false);
  if (open_in_window) {
    app_loaded_observer.Wait();
    app_browser_ = GetBrowserForAppId(active_app_id_);
  }
}

void WebAppIntegrationTestDriver::InstallPolicyAppInternal(
    const std::string& site_mode,
    base::Value default_launch_container,
    const bool create_shortcut) {
  GURL url = GetAppStartURL(site_mode);
  WebAppTestInstallWithOsHooksObserver observer(profile());
  observer.BeginListening();
  {
    base::Value item(base::Value::Type::DICTIONARY);
    item.SetKey(kUrlKey, base::Value(url.spec()));
    item.SetKey(kDefaultLaunchContainerKey,
                std::move(default_launch_container));
    item.SetKey(kCreateDesktopShortcutKey, base::Value(create_shortcut));
    ListPrefUpdate update(profile()->GetPrefs(),
                          prefs::kWebAppInstallForceList);
    update->Append(item.Clone());
  }
  active_app_id_ = observer.Wait();
}

bool WebAppIntegrationTestDriver::AreNoAppWindowsOpen(Profile* profile,
                                                      const AppId& app_id) {
  auto* provider = GetProviderForProfile(profile);
  const GURL& app_scope = provider->registrar().GetAppScope(app_id);
  auto* browser_list = BrowserList::GetInstance();
  for (Browser* browser : *browser_list) {
    if (browser->IsAttemptingToCloseBrowser()) {
      continue;
    }
    const GURL& browser_url =
        browser->tab_strip_model()->GetActiveWebContents()->GetURL();
    if (AppBrowserController::IsWebApp(browser) &&
        IsInScope(browser_url, app_scope)) {
      return false;
    }
  }
  return true;
}

void WebAppIntegrationTestDriver::ForceUpdateManifestContents(
    const std::string& site_mode,
    GURL app_url_with_manifest_param) {
  absl::optional<AppState> app_state = GetAppBySiteMode(
      before_state_change_action_state_.get(), profile(), site_mode);
  ASSERT_TRUE(app_state.has_value());
  auto app_id = app_state->id;
  active_app_id_ = app_id;

  // Manifest updates must occur as the first navigation after a webapp is
  // installed, otherwise the throttle is tripped.
  ASSERT_FALSE(provider()->manifest_update_manager().IsUpdateConsumed(app_id));
  NavigateTabbedBrowserToSite(app_url_with_manifest_param);
  app_ids_with_pending_manifest_updates_.insert(app_id);
}

void WebAppIntegrationTestDriver::MaybeWaitForManifestUpdates() {
  if (delegate_->GetAllProfiles().size() > 1) {
    return;
  }
  bool continue_checking_for_updates = true;
  while (continue_checking_for_updates) {
    continue_checking_for_updates = false;
    for (const AppId& app_id : app_ids_with_pending_manifest_updates_) {
      if (AreNoAppWindowsOpen(profile(), app_id)) {
        waiting_for_update_id_ = absl::make_optional(app_id);
        waiting_for_update_run_loop_ = std::make_unique<base::RunLoop>();
        waiting_for_update_run_loop_->Run();
        waiting_for_update_run_loop_ = nullptr;
        DCHECK(!waiting_for_update_id_);
        // To prevent iteration-during-modification, break and restart
        // the loop.
        continue_checking_for_updates = true;
        break;
      }
    }
  }
}

void WebAppIntegrationTestDriver::MaybeNavigateTabbedBrowserInScope(
    const std::string& site_mode) {
  auto browser_url = GetCurrentTab(browser())->GetURL();
  auto dest_url = GetInScopeURL(site_mode);
  if (browser_url.is_empty() || browser_url != dest_url) {
    NavigateTabbedBrowserToSite(dest_url);
  }
}

Browser* WebAppIntegrationTestDriver::GetAppBrowserForSite(
    const std::string& site_mode,
    bool launch_if_not_open) {
  StateSnapshot* state = after_state_change_action_state_
                             ? after_state_change_action_state_.get()
                             : before_state_change_action_state_.get();
  DCHECK(state);
  absl::optional<AppState> app_state =
      GetAppBySiteMode(state, profile(), site_mode);
  DCHECK(app_state) << "Could not find installed app for site mode "
                    << site_mode;

  auto profile_state = GetStateForProfile(state, profile());
  DCHECK(profile_state);
  for (const auto& browser_state_pair : profile_state->browsers) {
    if (browser_state_pair.second.app_id == app_state->id)
      return browser_state_pair.second.browser;
  }
  if (!launch_if_not_open)
    return nullptr;
  return LaunchWebAppBrowserAndWait(profile(), app_state->id);
}

Browser* WebAppIntegrationTestDriver::browser() {
  Browser* browser = active_browser_
                         ? active_browser_
                         : chrome::FindTabbedBrowser(
                               profile(), /*match_original_profiles=*/false);
  DCHECK(browser);
  if (!browser->tab_strip_model()->count()) {
    delegate_->AddBlankTabAndShow(browser);
  }
  return browser;
}

const net::EmbeddedTestServer*
WebAppIntegrationTestDriver::embedded_test_server() {
  return delegate_->EmbeddedTestServer();
}

PageActionIconView* WebAppIntegrationTestDriver::pwa_install_view() {
  PageActionIconView* pwa_install_view =
      BrowserView::GetBrowserViewForBrowser(browser())
          ->toolbar_button_provider()
          ->GetPageActionIconView(PageActionIconType::kPwaInstall);
  DCHECK(pwa_install_view);
  return pwa_install_view;
}

PageActionIconView* WebAppIntegrationTestDriver::intent_picker_view() {
  PageActionIconView* intent_picker_view =
      BrowserView::GetBrowserViewForBrowser(browser())
          ->toolbar_button_provider()
          ->GetPageActionIconView(PageActionIconType::kIntentPicker);
  DCHECK(intent_picker_view);
  return intent_picker_view;
}

WebAppIntegrationBrowserTest::WebAppIntegrationBrowserTest() : helper_(this) {
#if BUILDFLAG(IS_CHROMEOS_ASH)
  scoped_feature_list_.InitWithFeatures(
      {}, {features::kWebAppsCrosapi, chromeos::features::kLacrosPrimary});
#endif
}

WebAppIntegrationBrowserTest::~WebAppIntegrationBrowserTest() = default;

void WebAppIntegrationBrowserTest::SetUp() {
  helper_.SetUp();
  InProcessBrowserTest::SetUp();
  chrome::SetAutoAcceptAppIdentityUpdateForTesting(false);
}

void WebAppIntegrationBrowserTest::SetUpOnMainThread() {
  helper_.SetUpOnMainThread();
}
void WebAppIntegrationBrowserTest::TearDownOnMainThread() {
  helper_.TearDownOnMainThread();
}

void WebAppIntegrationBrowserTest::SetUpCommandLine(
    base::CommandLine* command_line) {
  ASSERT_TRUE(embedded_test_server()->Start());
}

Browser* WebAppIntegrationBrowserTest::CreateBrowser(Profile* profile) {
  return InProcessBrowserTest::CreateBrowser(profile);
}

void WebAppIntegrationBrowserTest::AddBlankTabAndShow(Browser* browser) {
  InProcessBrowserTest::AddBlankTabAndShow(browser);
}

net::EmbeddedTestServer* WebAppIntegrationBrowserTest::EmbeddedTestServer() {
  return embedded_test_server();
}

std::vector<Profile*> WebAppIntegrationBrowserTest::GetAllProfiles() {
  return std::vector<Profile*>{browser()->profile()};
}

bool WebAppIntegrationBrowserTest::IsSyncTest() {
  return false;
}

void WebAppIntegrationBrowserTest::SyncTurnOff() {
  NOTREACHED();
}
void WebAppIntegrationBrowserTest::SyncTurnOn() {
  NOTREACHED();
}
void WebAppIntegrationBrowserTest::AwaitWebAppQuiescence() {
  NOTREACHED();
}

}  // namespace web_app
