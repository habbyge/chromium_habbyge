// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/app_list/app_list_controller_impl.h"

#include <utility>
#include <vector>

#include "ash/app_list/app_list_bubble_presenter.h"
#include "ash/app_list/app_list_metrics.h"
#include "ash/app_list/app_list_model_provider.h"
#include "ash/app_list/app_list_presenter_impl.h"
#include "ash/app_list/model/app_list_folder_item.h"
#include "ash/app_list/model/app_list_item.h"
#include "ash/app_list/views/app_list_main_view.h"
#include "ash/app_list/views/app_list_view.h"
#include "ash/app_list/views/apps_container_view.h"
#include "ash/app_list/views/apps_grid_view.h"
#include "ash/app_list/views/contents_view.h"
#include "ash/app_list/views/paged_apps_grid_view.h"
#include "ash/app_list/views/search_box_view.h"
#include "ash/assistant/assistant_controller_impl.h"
#include "ash/assistant/ui/assistant_ui_constants.h"
#include "ash/assistant/ui/assistant_view_delegate.h"
#include "ash/assistant/util/assistant_util.h"
#include "ash/assistant/util/deep_link_util.h"
#include "ash/constants/ash_features.h"
#include "ash/constants/ash_pref_names.h"
#include "ash/keyboard/ui/keyboard_ui_controller.h"
#include "ash/public/cpp/app_list/app_list_client.h"
#include "ash/public/cpp/app_list/app_list_config.h"
#include "ash/public/cpp/app_list/app_list_controller_observer.h"
#include "ash/public/cpp/app_list/app_list_features.h"
#include "ash/public/cpp/app_list/app_list_metrics.h"
#include "ash/public/cpp/app_list/app_list_notifier.h"
#include "ash/public/cpp/app_list/app_list_types.h"
#include "ash/public/cpp/assistant/controller/assistant_controller.h"
#include "ash/public/cpp/assistant/controller/assistant_ui_controller.h"
#include "ash/public/cpp/shelf_config.h"
#include "ash/public/cpp/shelf_types.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/public/cpp/window_properties.h"
#include "ash/root_window_controller.h"
#include "ash/scoped_animation_disabler.h"
#include "ash/screen_util.h"
#include "ash/session/session_controller_impl.h"
#include "ash/shell.h"
#include "ash/wallpaper/wallpaper_controller_impl.h"
#include "ash/wm/mru_window_tracker.h"
#include "ash/wm/overview/overview_controller.h"
#include "ash/wm/splitview/split_view_controller.h"
#include "ash/wm/tablet_mode/tablet_mode_controller.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "base/barrier_closure.h"
#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/callback_list.h"
#include "base/containers/contains.h"
#include "base/containers/cxx20_erase.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/strings/utf_string_conversions.h"
#include "chromeos/services/assistant/public/cpp/assistant_enums.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/services/app_service/public/cpp/app_registry_cache_wrapper.h"
#include "extensions/common/constants.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/display/manager/display_manager.h"
#include "ui/display/screen.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/wm/core/coordinate_conversion.h"
#include "ui/wm/core/window_animations.h"
#include "ui/wm/public/activation_client.h"

namespace ash {

using chromeos::assistant::AssistantEntryPoint;
using chromeos::assistant::AssistantExitPoint;

namespace {

constexpr char kHomescreenAnimationHistogram[] =
    "Ash.Homescreen.AnimationSmoothness";

// The target scale to which (or from which) the home launcher will animate when
// overview is being shown (or hidden) using fade transitions while home
// launcher is shown.
constexpr float kOverviewFadeAnimationScale = 0.92f;

// The home launcher animation duration for transitions that accompany overview
// fading transitions.
constexpr base::TimeDelta kOverviewFadeAnimationDuration =
    base::Milliseconds(350);

// Update layer animation settings for launcher scale and opacity animation that
// runs on overview mode change.
void UpdateOverviewSettings(base::TimeDelta duration,
                            ui::ScopedLayerAnimationSettings* settings) {
  settings->SetTransitionDuration(kOverviewFadeAnimationDuration);
  settings->SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);
  settings->SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
}

// Layer animation observer that waits for layer animator to schedule, and
// complete animations. When all animations complete, it fires |callback| and
// deletes itself.
class WindowAnimationsCallback : public ui::LayerAnimationObserver {
 public:
  WindowAnimationsCallback(base::OnceClosure callback,
                           ui::LayerAnimator* animator)
      : callback_(std::move(callback)), animator_(animator) {
    subscription_ = animator_->AddSequenceScheduledCallback(
        base::BindRepeating(&WindowAnimationsCallback::OnSequenceScheduled,
                            base::Unretained(this)));
  }
  WindowAnimationsCallback(const WindowAnimationsCallback&) = delete;
  WindowAnimationsCallback& operator=(const WindowAnimationsCallback&) = delete;
  ~WindowAnimationsCallback() override = default;

  // ui::LayerAnimationObserver:
  void OnLayerAnimationEnded(ui::LayerAnimationSequence* sequence) override {
    FireCallbackIfDone();
  }
  void OnLayerAnimationAborted(ui::LayerAnimationSequence* sequence) override {
    FireCallbackIfDone();
  }
  void OnLayerAnimationScheduled(
      ui::LayerAnimationSequence* sequence) override {}

  void OnDetachedFromSequence(ui::LayerAnimationSequence* sequence) override {
    FireCallbackIfDone();
  }

 private:
  void OnSequenceScheduled(ui::LayerAnimationSequence* sequence) {
    // LayerAnimationSequence::RemoveObserver is called by the ancestor during
    // destruction.
    sequence->AddObserver(this);
  }

  // Fires the callback if all scheduled animations completed (either ended or
  // got aborted).
  void FireCallbackIfDone() {
    if (!callback_ || animator_->is_animating())
      return;
    std::move(callback_).Run();
    delete this;
  }

  base::OnceClosure callback_;
  ui::LayerAnimator* animator_;  // Owned by the layer that is animating.
  base::CallbackListSubscription subscription_;
};

// Minimizes all windows in |windows| that aren't in the home screen container,
// and are not in |windows_to_ignore|. Done in reverse order to preserve the mru
// ordering.
// Returns true if any windows are minimized.
bool MinimizeAllWindows(const aura::Window::Windows& windows,
                        const aura::Window::Windows& windows_to_ignore) {
  aura::Window* container = Shell::Get()->GetPrimaryRootWindow()->GetChildById(
      kShellWindowId_HomeScreenContainer);
  aura::Window::Windows windows_to_minimize;
  for (auto it = windows.rbegin(); it != windows.rend(); it++) {
    if (!container->Contains(*it) && !base::Contains(windows_to_ignore, *it) &&
        !WindowState::Get(*it)->IsMinimized()) {
      windows_to_minimize.push_back(*it);
    }
  }

  window_util::MinimizeAndHideWithoutAnimation(windows_to_minimize);
  return !windows_to_minimize.empty();
}

bool IsTabletMode() {
  return Shell::Get()->tablet_mode_controller()->InTabletMode();
}

TabletModeAnimationTransition CalculateAnimationTransitionForMetrics(
    HomeLauncherAnimationTrigger trigger,
    bool launcher_should_show) {
  switch (trigger) {
    case HomeLauncherAnimationTrigger::kHideForWindow:
      return TabletModeAnimationTransition::kHideHomeLauncherForWindow;
    case HomeLauncherAnimationTrigger::kLauncherButton:
      return TabletModeAnimationTransition::kHomeButtonShow;
    case HomeLauncherAnimationTrigger::kDragRelease:
      return launcher_should_show
                 ? TabletModeAnimationTransition::kDragReleaseShow
                 : TabletModeAnimationTransition::kDragReleaseHide;
    case HomeLauncherAnimationTrigger::kOverviewModeFade:
      return launcher_should_show
                 ? TabletModeAnimationTransition::kFadeOutOverview
                 : TabletModeAnimationTransition::kFadeInOverview;
  }
}

int GetSuggestedContentInfoShownCount() {
  PrefService* prefs =
      Shell::Get()->session_controller()->GetLastActiveUserPrefService();
  return prefs->GetInteger(prefs::kSuggestedContentInfoShownInLauncher);
}

void SetSuggestedContentInfoShownCount(int count) {
  PrefService* prefs =
      Shell::Get()->session_controller()->GetLastActiveUserPrefService();
  prefs->SetInteger(prefs::kSuggestedContentInfoShownInLauncher, count);
}

bool IsSuggestedContentInfoDismissed() {
  PrefService* prefs =
      Shell::Get()->session_controller()->GetLastActiveUserPrefService();
  return prefs->GetBoolean(prefs::kSuggestedContentInfoDismissedInLauncher);
}

void SetSuggestedContentInfoDismissed() {
  PrefService* prefs =
      Shell::Get()->session_controller()->GetLastActiveUserPrefService();
  prefs->SetBoolean(prefs::kSuggestedContentInfoDismissedInLauncher, true);
}

bool IsSuggestedContentEnabled() {
  PrefService* prefs =
      Shell::Get()->session_controller()->GetLastActiveUserPrefService();
  return prefs->GetBoolean(chromeos::prefs::kSuggestedContentEnabled);
}

int GetOffset(int offset, const char* pref_name) {
  PrefService* prefs =
      Shell::Get()->session_controller()->GetLastActiveUserPrefService();
  return prefs->GetBoolean(pref_name) ? -offset : offset;
}

// Gets the MRU window shown over the applist when in tablet mode.
// Returns nullptr if no windows are shown over the applist.
aura::Window* GetTopVisibleWindow() {
  std::vector<aura::Window*> window_list =
      Shell::Get()->mru_window_tracker()->BuildWindowListIgnoreModal(
          DesksMruType::kActiveDesk);
  for (auto* window : window_list) {
    if (window->TargetVisibility() && !WindowState::Get(window)->IsMinimized())
      return window;
  }
  return nullptr;
}

void LogAppListShowSource(AppListShowSource show_source, bool app_list_bubble) {
  if (app_list_bubble) {
    UMA_HISTOGRAM_ENUMERATION("Apps.AppListBubbleShowSource", show_source);
    return;
  }
  UMA_HISTOGRAM_ENUMERATION("Apps.AppListShowSource", show_source);
}

absl::optional<TabletModeAnimationTransition>
GetTransitionFromMetricsAnimationInfo(
    absl::optional<HomeLauncherAnimationInfo> animation_info) {
  if (!animation_info.has_value())
    return absl::nullopt;

  return CalculateAnimationTransitionForMetrics(animation_info->trigger,
                                                animation_info->showing);
}

}  // namespace

AppListControllerImpl::AppListControllerImpl()
    : model_provider_(std::make_unique<AppListModelProvider>()),
      fullscreen_presenter_(std::make_unique<AppListPresenterImpl>(this)) {
  if (features::IsProductivityLauncherEnabled())
    bubble_presenter_ = std::make_unique<AppListBubblePresenter>(this);

  SessionControllerImpl* session_controller =
      Shell::Get()->session_controller();
  session_controller->AddObserver(this);

  // In case of crash-and-restart case where session state starts with ACTIVE
  // and does not change to trigger OnSessionStateChanged(), notify the current
  // session state here to ensure that the app list is shown.
  OnSessionStateChanged(session_controller->GetSessionState());

  Shell* shell = Shell::Get();
  shell->tablet_mode_controller()->AddObserver(this);
  shell->wallpaper_controller()->AddObserver(this);
  shell->AddShellObserver(this);
  shell->overview_controller()->AddObserver(this);
  keyboard::KeyboardUIController::Get()->AddObserver(this);
  AssistantState::Get()->AddObserver(this);
  shell->window_tree_host_manager()->AddObserver(this);
  AssistantController::Get()->AddObserver(this);
  AssistantUiController::Get()->GetModel()->AddObserver(this);
}

AppListControllerImpl::~AppListControllerImpl() {
  if (tracked_app_window_) {
    tracked_app_window_->RemoveObserver(this);
    tracked_app_window_ = nullptr;
  }

  // If this is being destroyed before the Shell starts shutting down, first
  // remove this from objects it's observing.
  if (!is_shutdown_)
    Shutdown();

  if (client_)
    client_->OnAppListControllerDestroyed();

  // Dismiss the window before `fullscreen_presenter_` is reset, because
  // Dimiss() may call back into this object and access `fullscreen_presenter_`.
  fullscreen_presenter_->Dismiss(base::TimeTicks());
}

// static
void AppListControllerImpl::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterIntegerPref(
      prefs::kSuggestedContentInfoShownInLauncher, 0,
      user_prefs::PrefRegistrySyncable::SYNCABLE_OS_PREF);
  registry->RegisterBooleanPref(
      prefs::kSuggestedContentInfoDismissedInLauncher, false,
      user_prefs::PrefRegistrySyncable::SYNCABLE_OS_PREF);
}

void AppListControllerImpl::SetClient(AppListClient* client) {
  client_ = client;
}

AppListClient* AppListControllerImpl::GetClient() {
  DCHECK(client_);
  return client_;
}

AppListNotifier* AppListControllerImpl::GetNotifier() {
  if (!client_)
    return nullptr;
  return client_->GetNotifier();
}

void AppListControllerImpl::SetActiveModel(int profile_id,
                                           AppListModel* model,
                                           SearchModel* search_model) {
  model_observation_.Reset();

  profile_id_ = profile_id;

  model_provider_->SetActiveModel(model, search_model);

  if (model)
    model_observation_.Observe(model);

  UpdateAssistantVisibility();
}

void AppListControllerImpl::ClearActiveModel() {
  model_observation_.Reset();
  profile_id_ = kAppListInvalidProfileID;
  model_provider_->ClearActiveModel();
  UpdateAssistantVisibility();
}

void AppListControllerImpl::NotifyProcessSyncChangesFinished() {
  // When there are incompatible apps on different devices under the same
  // user account, it is possible that moving or adding an app on an empty
  // spot on a page of a different type of device (e.g. Device 1) may cause app
  // overflow on another device (e.g. Device 2) since it may have more apps on
  // the same page. See details in http://crbug.com/1098174.
  // When the change is synced to the Device 2, paged view structure may load
  // meta data and detect a full page of apps without a page break item
  // at the end of the overflowed page. Therefore, after the sync service has
  // finished processing sync change, SaveToMetaData should be called to insert
  // page break items if there are any missing at the end of full pages.
  AppListView* const app_list_view = fullscreen_presenter_->GetView();
  if (app_list_view) {
    app_list_view->app_list_main_view()
        ->contents_view()
        ->apps_container_view()
        ->apps_grid_view()
        ->UpdatePagedViewStructure();
  }
}

void AppListControllerImpl::DismissAppList() {
  if (tracked_app_window_) {
    tracked_app_window_->RemoveObserver(this);
    tracked_app_window_ = nullptr;
  }

  // Don't check tablet mode here. This function can be called during tablet
  // mode transitions and we always want to close anyway.
  if (features::IsProductivityLauncherEnabled())
    bubble_presenter_->Dismiss();

  fullscreen_presenter_->Dismiss(base::TimeTicks());
}

void AppListControllerImpl::GetAppInfoDialogBounds(
    GetAppInfoDialogBoundsCallback callback) {
  AppListView* app_list_view = fullscreen_presenter_->GetView();
  gfx::Rect bounds = gfx::Rect();
  if (app_list_view)
    bounds = app_list_view->GetAppInfoDialogBounds();
  std::move(callback).Run(bounds);
}

void AppListControllerImpl::ShowAppList() {
  if (ShouldShowAppListBubble()) {
    DCHECK(!fullscreen_presenter_->GetTargetVisibility());
    bubble_presenter_->Show(GetDisplayIdToShowAppListOn());
    return;
  }
  DCHECK(!features::IsProductivityLauncherEnabled() ||
         !bubble_presenter_->IsShowing());
  fullscreen_presenter_->Show(AppListViewState::kPeeking,
                              GetDisplayIdToShowAppListOn(), base::TimeTicks(),
                              /*show_source=*/absl::nullopt);
}

aura::Window* AppListControllerImpl::GetWindow() {
  if (ShouldShowAppListBubble())
    return bubble_presenter_->GetWindow();
  return fullscreen_presenter_->GetWindow();
}

bool AppListControllerImpl::IsVisible(
    const absl::optional<int64_t>& display_id) {
  return last_visible_ && (!display_id.has_value() ||
                           display_id.value() == last_visible_display_id_);
}

bool AppListControllerImpl::IsVisible() {
  return IsVisible(absl::nullopt);
}

////////////////////////////////////////////////////////////////////////////////
// AppListModelObserver:

void AppListControllerImpl::OnAppListItemAdded(AppListItem* item) {
  if (cache_ && notification_badging_pref_enabled_.value_or(false)) {
    // Update the notification badge indicator for the newly added app list
    // item.
    cache_->ForOneApp(item->id(), [item](const apps::AppUpdate& update) {
      item->UpdateNotificationBadge(update.HasBadge() ==
                                    apps::mojom::OptionalBool::kTrue);
    });
  }
}

void AppListControllerImpl::OnActiveUserPrefServiceChanged(
    PrefService* pref_service) {
  pref_change_registrar_ = std::make_unique<PrefChangeRegistrar>();
  pref_change_registrar_->Init(pref_service);

  pref_change_registrar_->Add(
      prefs::kAppNotificationBadgingEnabled,
      base::BindRepeating(&AppListControllerImpl::UpdateAppNotificationBadging,
                          base::Unretained(this)));

  // Observe AppRegistryCache for the current active account to get
  // notification updates.
  AccountId account_id =
      Shell::Get()->session_controller()->GetActiveAccountId();
  cache_ = apps::AppRegistryCacheWrapper::Get().GetAppRegistryCache(account_id);
  Observe(cache_);

  // Resetting the recorded pref forces the next call to
  // UpdateAppNotificationBadging() to update notification badging for every
  // app item.
  notification_badging_pref_enabled_.reset();
  UpdateAppNotificationBadging();

  if (!IsTabletMode()) {
    DismissAppList();
    return;
  }

  // The app list is not dismissed before switching user, suggestion chips will
  // not be shown. So reset app list state and trigger an initial search here to
  // update the suggestion results.
  if (fullscreen_presenter_->GetView()) {
    fullscreen_presenter_->GetView()->CloseOpenedPage();
    fullscreen_presenter_->GetView()->search_box_view()->ClearSearch();
  }
}

void AppListControllerImpl::OnSessionStateChanged(
    session_manager::SessionState state) {
  if (!IsTabletMode())
    return;

  if (state != session_manager::SessionState::ACTIVE)
    return;

  // Show the app list after signing in in tablet mode. For metrics, the app
  // list is not considered shown since the browser window is shown over app
  // list upon login.
  if (!fullscreen_presenter_->GetTargetVisibility())
    ShowHomeScreen();

  // Hide app list UI initially to prevent app list from flashing in background
  // while the initial app window is being shown.
  if (!last_target_visible_ && !ShouldHomeLauncherBeVisible())
    fullscreen_presenter_->SetViewVisibility(false);
  else
    OnVisibilityChanged(true, last_visible_display_id_);
}

////////////////////////////////////////////////////////////////////////////////
// Methods used in Ash

bool AppListControllerImpl::GetTargetVisibility(
    const absl::optional<int64_t>& display_id) const {
  return last_target_visible_ &&
         (!display_id.has_value() ||
          display_id.value() == last_visible_display_id_);
}

void AppListControllerImpl::Show(int64_t display_id,
                                 absl::optional<AppListShowSource> show_source,
                                 base::TimeTicks event_time_stamp) {
  const bool show_app_list_bubble = ShouldShowAppListBubble();
  if (show_source.has_value())
    LogAppListShowSource(show_source.value(), show_app_list_bubble);

  if (show_app_list_bubble) {
    bubble_presenter_->Show(display_id);
    return;
  }
  fullscreen_presenter_->Show(AppListViewState::kPeeking, display_id,
                              event_time_stamp, show_source);
}

void AppListControllerImpl::UpdateYPositionAndOpacity(
    int y_position_in_screen,
    float background_opacity) {
  // Avoid changing app list opacity and position when homecher is enabled.
  if (IsTabletMode())
    return;
  fullscreen_presenter_->UpdateYPositionAndOpacity(y_position_in_screen,
                                                   background_opacity);
}

void AppListControllerImpl::EndDragFromShelf(AppListViewState app_list_state) {
  // Avoid dragging app list when homecher is enabled.
  if (IsTabletMode())
    return;
  fullscreen_presenter_->EndDragFromShelf(app_list_state);
}

void AppListControllerImpl::ProcessMouseWheelEvent(
    const ui::MouseWheelEvent& event) {
  fullscreen_presenter_->ProcessMouseWheelOffset(event.location(),
                                                 event.offset());
}

void AppListControllerImpl::ProcessScrollEvent(const ui::ScrollEvent& event) {
  gfx::Vector2d offset(event.x_offset(), event.y_offset());
  fullscreen_presenter_->ProcessScrollOffset(event.location(), offset);
}

ShelfAction AppListControllerImpl::ToggleAppList(
    int64_t display_id,
    AppListShowSource show_source,
    base::TimeTicks event_time_stamp) {
  if (IsTabletMode()) {
    bool handled = GoHome(display_id);

    // Perform the "back" action for the app list.
    if (!handled) {
      Back();
      return SHELF_ACTION_APP_LIST_BACK;
    }
    LogAppListShowSource(show_source, /*app_list_bubble=*/false);
    return SHELF_ACTION_APP_LIST_SHOWN;
  }

  if (features::IsProductivityLauncherEnabled()) {
#if !defined(OFFICIAL_BUILD)
    // Make shift-click on the shelf button toggle the non-bubble app list. This
    // allows developers to compare behavior without restarting to flip the
    // flag. TODO(crbug.com/1232168): Remove before feature launch.
    if (show_source == AppListShowSource::kShelfButtonFullscreen) {
      bubble_presenter_->Dismiss();
      return fullscreen_presenter_->ToggleAppList(display_id, show_source,
                                                  event_time_stamp);
    }
    fullscreen_presenter_->Dismiss(event_time_stamp);
#endif  // !defined(OFFICIAL_BUILD)
    ShelfAction action = bubble_presenter_->Toggle(display_id);
    if (action == SHELF_ACTION_APP_LIST_SHOWN)
      LogAppListShowSource(show_source, /*app_list_bubble=*/true);
    return action;
  }

  base::AutoReset<bool> auto_reset(&should_dismiss_immediately_,
                                   display_id != last_visible_display_id_);
  ShelfAction action = fullscreen_presenter_->ToggleAppList(
      display_id, show_source, event_time_stamp);
  if (action == SHELF_ACTION_APP_LIST_SHOWN)
    LogAppListShowSource(show_source, /*app_list_bubble=*/false);
  return action;
}

bool AppListControllerImpl::GoHome(int64_t display_id) {
  DCHECK(Shell::Get()->tablet_mode_controller()->InTabletMode());

  if (fullscreen_presenter_->IsShowingEmbeddedAssistantUI())
    fullscreen_presenter_->ShowEmbeddedAssistantUI(false);

  SplitViewController* split_view_controller =
      SplitViewController::Get(Shell::GetPrimaryRootWindow());
  const bool split_view_active = split_view_controller->InSplitViewMode();

  // The home screen opens for the current active desk, there's no need to
  // minimize windows in the inactive desks.
  aura::Window::Windows windows =
      Shell::Get()->mru_window_tracker()->BuildWindowForCycleList(kActiveDesk);

  // The foreground window or windows (for split mode) - the windows that will
  // not be minimized without animations (instead they will be animated into the
  // home screen).
  std::vector<aura::Window*> foreground_windows;
  if (split_view_active) {
    foreground_windows = {split_view_controller->left_window(),
                          split_view_controller->right_window()};
    base::EraseIf(foreground_windows,
                  [](aura::Window* window) { return !window; });
  } else if (!windows.empty() && !WindowState::Get(windows[0])->IsMinimized()) {
    foreground_windows.push_back(windows[0]);
  }

  OverviewController* overview_controller = Shell::Get()->overview_controller();
  if (split_view_active) {
    // If overview session is active (e.g. on one side of the split view), end
    // it immediately, to prevent overview UI being visible while transitioning
    // to home screen.
    if (overview_controller->InOverviewSession()) {
      overview_controller->EndOverview(OverviewEndAction::kEnterHomeLauncher,
                                       OverviewEnterExitType::kImmediateExit);
    }

    // End split view mode.
    split_view_controller->EndSplitView(
        SplitViewController::EndReason::kHomeLauncherPressed);
  }

  // If overview is active (if overview was active in split view, it exited by
  // this point), just fade it out to home screen.
  if (overview_controller->InOverviewSession()) {
    overview_controller->EndOverview(OverviewEndAction::kEnterHomeLauncher,
                                     OverviewEnterExitType::kFadeOutExit);
    return true;
  }

  // First minimize all inactive windows.
  const bool window_minimized =
      MinimizeAllWindows(windows, foreground_windows /*windows_to_ignore*/);

  if (foreground_windows.empty())
    return window_minimized;

  {
    // Disable window animations before updating home launcher target
    // position. Calling OnHomeLauncherPositionChanged() can cause
    // display work area update, and resulting cross-fade window bounds change
    // animation can interfere with WindowTransformToHomeScreenAnimation
    // visuals.
    //
    // TODO(https://crbug.com/1019531): This can be removed once transitions
    // between in-app state and home do not cause work area updates.
    std::vector<std::unique_ptr<ScopedAnimationDisabler>> animation_disablers;
    for (auto* window : foreground_windows) {
      animation_disablers.push_back(
          std::make_unique<ScopedAnimationDisabler>(window));
    }

    OnHomeLauncherPositionChanged(/*percent_shown=*/100, display_id);
  }

  StartTrackingAnimationSmoothness(display_id);

  base::RepeatingClosure window_transforms_callback = base::BarrierClosure(
      foreground_windows.size(),
      base::BindOnce(&AppListControllerImpl::OnGoHomeWindowAnimationsEnded,
                     weak_ptr_factory_.GetWeakPtr(), display_id));

  // Minimize currently active windows, but this time, using animation.
  // Home screen will show when all the windows are done minimizing.
  for (auto* foreground_window : foreground_windows) {
    if (::wm::WindowAnimationsDisabled(foreground_window)) {
      WindowState::Get(foreground_window)->Minimize();
      window_transforms_callback.Run();
    } else {
      // Create animator observer that will fire |window_transforms_callback|
      // once the window layer stops animating - it deletes itself when
      // animations complete.
      new WindowAnimationsCallback(window_transforms_callback,
                                   foreground_window->layer()->GetAnimator());
      WindowState::Get(foreground_window)->Minimize();
    }
  }

  return true;
}

bool AppListControllerImpl::ShouldHomeLauncherBeVisible() const {
  if (!IsTabletMode())
    return false;

  if (home_launcher_transition_state_ ==
      HomeLauncherTransitionState::kMostlyShown)
    return true;

  return !Shell::Get()->overview_controller()->InOverviewSession() &&
         !GetTopVisibleWindow();
}

void AppListControllerImpl::OnShelfAlignmentChanged(
    aura::Window* root_window,
    ShelfAlignment old_alignment) {
  if (!IsTabletMode())
    DismissAppList();
}

void AppListControllerImpl::OnShellDestroying() {
  // Stop observing at the beginning of ~Shell to avoid unnecessary work during
  // Shell shutdown.
  Shutdown();
}

void AppListControllerImpl::OnOverviewModeStarting() {
  const OverviewEnterExitType overview_enter_type =
      Shell::Get()
          ->overview_controller()
          ->overview_session()
          ->enter_exit_overview_type();

  const bool animate =
      IsHomeScreenVisible() &&
      overview_enter_type == OverviewEnterExitType::kFadeInEnter;

  UpdateForOverviewModeChange(/*show_home_launcher=*/false, animate);

  if (IsTabletMode()) {
    const int64_t display_id = last_visible_display_id_;
    OnVisibilityWillChange(false /*shown*/, display_id);
  } else {
    DismissAppList();
  }
}

void AppListControllerImpl::OnOverviewModeStartingAnimationComplete(
    bool canceled) {
  if (!IsTabletMode())
    return;

  // If overview start was canceled, overview end animations are about to start.
  // Preemptively update the target app list visibility.
  if (canceled) {
    OnVisibilityWillChange(!GetTopVisibleWindow(), last_visible_display_id_);
    return;
  }

  OnVisibilityChanged(false /* shown */, last_visible_display_id_);
}

void AppListControllerImpl::OnOverviewModeEnding(OverviewSession* session) {
  // The launcher will be shown after overview mode finishes animating, in
  // OnOverviewModeEndingAnimationComplete(). Overview however is nullptr by
  // the time the animations are finished, so cache the exit type here.
  overview_exit_type_ =
      absl::make_optional(session->enter_exit_overview_type());

  // If the overview is fading out, start the home launcher animation in
  // parallel. Otherwise the transition will be initiated in
  // OnOverviewModeEndingAnimationComplete().
  if (session->enter_exit_overview_type() ==
      OverviewEnterExitType::kFadeOutExit) {
    UpdateForOverviewModeChange(/*show_home_launcher=*/true, /*animate=*/true);

    // Make sure the window visibility is updated, in case it was previously
    // hidden due to overview being shown.
    UpdateHomeScreenVisibility();
  }

  if (!IsTabletMode())
    return;

  // Overview state might end during home launcher transition - if that is the
  // case, respect the final state set by in-progress home launcher transition.
  if (home_launcher_transition_state_ != HomeLauncherTransitionState::kFinished)
    return;

  OnVisibilityWillChange(!GetTopVisibleWindow() /*shown*/,
                         last_visible_display_id_);
}

void AppListControllerImpl::OnOverviewModeEnded() {
  if (!IsTabletMode())
    return;
  // Overview state might end during home launcher transition - if that is the
  // case, respect the final state set by in-progress home launcher transition.
  if (home_launcher_transition_state_ != HomeLauncherTransitionState::kFinished)
    return;
  OnVisibilityChanged(!GetTopVisibleWindow(), last_visible_display_id_);
}

void AppListControllerImpl::OnOverviewModeEndingAnimationComplete(
    bool canceled) {
  DCHECK(overview_exit_type_.has_value());

  // For kFadeOutExit OverviewEnterExitType, the home animation is scheduled in
  // OnOverviewModeEnding(), so there is nothing else to do at this point.
  if (canceled || *overview_exit_type_ == OverviewEnterExitType::kFadeOutExit) {
    overview_exit_type_ = absl::nullopt;
    return;
  }

  const bool animate =
      *overview_exit_type_ == OverviewEnterExitType::kFadeOutExit;
  overview_exit_type_ = absl::nullopt;

  UpdateForOverviewModeChange(/*show_home_launcher=*/true, animate);

  // Make sure the window visibility is updated, in case it was previously
  // hidden due to overview being shown.
  UpdateHomeScreenVisibility();
}

void AppListControllerImpl::OnSplitViewStateChanged(
    SplitViewController::State previous_state,
    SplitViewController::State state) {
  UpdateHomeScreenVisibility();
}

void AppListControllerImpl::OnTabletModeStarted() {
  const AppListView* app_list_view = fullscreen_presenter_->GetView();
  // In tablet mode shelf orientation is always "bottom". Dismiss app list if
  // switching to tablet mode from side shelf app list, to ensure the app list
  // is re-shown and laid out with correct "side shelf" value.
  if (app_list_view && app_list_view->is_side_shelf())
    DismissAppList();

  // The bubble launcher is only used in clamshell mode.
  if (features::IsProductivityLauncherEnabled())
    DismissAppList();

  fullscreen_presenter_->OnTabletModeChanged(true);

  // Show the app list if the tablet mode starts.
  if (Shell::Get()->session_controller()->GetSessionState() ==
      session_manager::SessionState::ACTIVE) {
    ShowHomeScreen();
  }
  UpdateLauncherContainer();

  // If the app list is visible before the transition to tablet mode,
  // AppListPresenter relies on the active window change to detect the app list
  // view got hidden behind a window. Though, app list UI moving behind an app
  // window does not always cause an active window change:
  // *   If the app list is still being shown - given that app list takes focus
  //     from the top window only when it's fully shown, the focus will remain
  //     within the app window throughout the tablet mode transition.
  // *   If the assistant UI is visible before the tablet mode transition - the
  //     assistant will keep the focus during transition, even though the app
  //     window will be shown over the app list view.
  // Ensure the app list visibility is properly updated if the app list is
  // hidden behind a window at this point.
  if (last_target_visible_ && !ShouldHomeLauncherBeVisible())
    OnVisibilityChanged(false, last_visible_display_id_);
}

void AppListControllerImpl::OnTabletModeEnded() {
  aura::Window* window = fullscreen_presenter_->GetWindow();
  base::AutoReset<bool> auto_reset(
      &should_dismiss_immediately_,
      window && RootWindowController::ForWindow(window)
                    ->GetShelfLayoutManager()
                    ->HasVisibleWindow());
  fullscreen_presenter_->OnTabletModeChanged(false);
  UpdateLauncherContainer();

  // Dismiss the app list if the tablet mode ends.
  DismissAppList();
}

void AppListControllerImpl::OnWallpaperColorsChanged() {
  if (IsVisible(last_visible_display_id_))
    fullscreen_presenter_->GetView()->OnWallpaperColorsChanged();
}

void AppListControllerImpl::OnWallpaperPreviewStarted() {
  in_wallpaper_preview_ = true;
  UpdateHomeScreenVisibility();
}

void AppListControllerImpl::OnWallpaperPreviewEnded() {
  in_wallpaper_preview_ = false;
  UpdateHomeScreenVisibility();
}

void AppListControllerImpl::OnKeyboardVisibilityChanged(const bool is_visible) {
  onscreen_keyboard_shown_ = is_visible;
  AppListView* app_list_view = fullscreen_presenter_->GetView();
  if (app_list_view)
    app_list_view->OnScreenKeyboardShown(is_visible);
}

void AppListControllerImpl::OnAssistantStatusChanged(
    chromeos::assistant::AssistantStatus status) {
  UpdateAssistantVisibility();
}

void AppListControllerImpl::OnAssistantSettingsEnabled(bool enabled) {
  UpdateAssistantVisibility();
}

void AppListControllerImpl::OnAssistantFeatureAllowedChanged(
    chromeos::assistant::AssistantAllowedState state) {
  UpdateAssistantVisibility();
}

void AppListControllerImpl::OnDisplayConfigurationChanged() {
  // Entering tablet mode triggers a display configuration change when we
  // automatically switch to mirror mode. Switching to mirror mode happens
  // asynchronously (see DisplayConfigurationObserver::OnTabletModeStarted()).
  // This may result in the removal of a window tree host, as in the example of
  // switching to tablet mode while Unified Desktop mode is on; the Unified host
  // will be destroyed and the Home Launcher (which was created earlier when we
  // entered tablet mode) will be dismissed.
  // To avoid crashes, we must ensure that the Home Launcher shown status is as
  // expected if it's enabled and we're still in tablet mode.
  // https://crbug.com/900956.
  const bool should_be_shown =
      IsTabletMode() && Shell::Get()->session_controller()->GetSessionState() ==
                            session_manager::SessionState::ACTIVE;

  if (!should_be_shown ||
      should_be_shown == fullscreen_presenter_->GetTargetVisibility()) {
    return;
  }
  ShowHomeScreen();
}

void AppListControllerImpl::OnAssistantReady() {
  UpdateAssistantVisibility();
}

void AppListControllerImpl::OnUiVisibilityChanged(
    AssistantVisibility new_visibility,
    AssistantVisibility old_visibility,
    absl::optional<AssistantEntryPoint> entry_point,
    absl::optional<AssistantExitPoint> exit_point) {
  const bool is_old_visibility_closing =
      (old_visibility == AssistantVisibility::kClosing);

  switch (new_visibility) {
    case AssistantVisibility::kVisible:
      if (!IsVisible() || is_old_visibility_closing) {
        absl::optional<AppListView::ScopedContentsResetDisabler> disabler;
        if (is_old_visibility_closing) {
          // Avoid resetting the contents view when the transition to close the
          // Assistant ui is going to be reversed.
          disabler.emplace(fullscreen_presenter_->GetView());

          // Reset `close_assistant_ui_runner_` because the Assistant ui is
          // going to show.
          DCHECK(close_assistant_ui_runner_);
          IgnoreResult(close_assistant_ui_runner_.Release());
        }

        Show(GetDisplayIdToShowAppListOn(), kAssistantEntryPoint,
             base::TimeTicks());
      }
      if (ShouldShowAppListBubble()) {
        bubble_presenter_->ShowEmbeddedAssistantUI();
      } else {
        if (!fullscreen_presenter_->IsShowingEmbeddedAssistantUI() ||
            is_old_visibility_closing) {
          fullscreen_presenter_->ShowEmbeddedAssistantUI(true);
        }

        // Make sure that app list views are visible - they might get hidden
        // during session startup, and the app list visibility might not have
        // yet changed to visible by this point. https://crbug.com/1040751
        fullscreen_presenter_->SetViewVisibility(true);
      }
      break;
    case AssistantVisibility::kClosed:
      if (!IsShowingEmbeddedAssistantUI())
        break;

      // When Launcher is closing, we do not want to call
      // |ShowEmbeddedAssistantUI(false)|, which will show previous state page
      // in Launcher and make the UI flash.
      if (IsTabletMode()) {
        absl::optional<ContentsView::ScopedSetActiveStateAnimationDisabler>
            set_active_state_animation_disabler;
        // When taking a screenshot by Assistant, we do not want to animate to
        // the final state. Otherwise the screenshot may have transient state
        // during the animation. In tablet mode, we want to go back to
        // kStateApps immediately, i.e. skipping the animation in
        // |SetActiveStateInternal|, which are called from
        // |ShowEmbeddedAssistantUI(false)| and
        // |ClearSearchAndDeactivateSearchBox()|.
        if (exit_point == AssistantExitPoint::kScreenshot) {
          set_active_state_animation_disabler.emplace(
              fullscreen_presenter_->GetView()
                  ->app_list_main_view()
                  ->contents_view());
        }

        fullscreen_presenter_->ShowEmbeddedAssistantUI(false);

        if (exit_point != AssistantExitPoint::kBackInLauncher) {
          fullscreen_presenter_->GetView()
              ->search_box_view()
              ->ClearSearchAndDeactivateSearchBox();
        }
      } else if (exit_point != AssistantExitPoint::kBackInLauncher) {
        // Similarly, when taking a screenshot by Assistant in clamshell mode,
        // we do not want to dismiss launcher with animation. Otherwise the
        // screenshot may have transient state during the animation.
        base::AutoReset<bool> auto_reset(
            &should_dismiss_immediately_,
            exit_point == AssistantExitPoint::kScreenshot);
        DismissAppList();
      }
      break;
    case AssistantVisibility::kClosing:
      break;
  }
}

base::ScopedClosureRunner
AppListControllerImpl::DisableHomeScreenBackgroundBlur() {
  AppListView* const app_list_view = fullscreen_presenter_->GetView();
  if (!app_list_view)
    return base::ScopedClosureRunner(base::DoNothing());
  return app_list_view->app_list_main_view()
      ->contents_view()
      ->apps_container_view()
      ->DisableSuggestionChipsBlur();
}

void AppListControllerImpl::OnHomeLauncherAnimationComplete(
    bool shown,
    int64_t display_id) {
  // Stop disabling background blur in home screen when the home screen
  // transition ends.
  home_screen_blur_disabler_.reset();

  home_launcher_transition_state_ = HomeLauncherTransitionState::kFinished;

  AssistantUiController::Get()->CloseUi(
      shown ? AssistantExitPoint::kLauncherOpen
            : AssistantExitPoint::kLauncherClose);

  // Animations can be reversed (e.g. in a drag). Let's ensure the target
  // visibility is correct first.
  OnVisibilityChanged(shown, display_id);
}

void AppListControllerImpl::OnHomeLauncherPositionChanged(int percent_shown,
                                                          int64_t display_id) {
  // Disable home screen background blur if the home launcher transition is
  // staring - the blur disabler will be reset when the transition ends (in
  // OnHomeLauncherAnimationComplete()).
  if (home_launcher_transition_state_ == HomeLauncherTransitionState::kFinished)
    home_screen_blur_disabler_ = DisableHomeScreenBackgroundBlur();

  const bool mostly_shown = percent_shown >= 50;
  home_launcher_transition_state_ =
      mostly_shown ? HomeLauncherTransitionState::kMostlyShown
                   : HomeLauncherTransitionState::kMostlyHidden;
  OnVisibilityWillChange(mostly_shown, display_id);
}

aura::Window* AppListControllerImpl::GetHomeScreenWindow() const {
  return fullscreen_presenter_->GetWindow();
}

void AppListControllerImpl::UpdateScaleAndOpacityForHomeLauncher(
    float scale,
    float opacity,
    absl::optional<HomeLauncherAnimationInfo> animation_info,
    UpdateAnimationSettingsCallback callback) {
  DCHECK(!animation_info.has_value() || !callback.is_null());

  fullscreen_presenter_->UpdateScaleAndOpacityForHomeLauncher(
      scale, opacity,
      GetTransitionFromMetricsAnimationInfo(std::move(animation_info)),
      std::move(callback));
}

void AppListControllerImpl::Back() {
  fullscreen_presenter_->GetView()->Back();
}

void AppListControllerImpl::SetKeyboardTraversalMode(bool engaged) {
  if (keyboard_traversal_engaged_ == engaged)
    return;

  keyboard_traversal_engaged_ = engaged;

  // No need to schedule paint for bubble presenter.
  if (bubble_presenter_ && bubble_presenter_->IsShowing())
    return;

  views::View* focused_view =
      fullscreen_presenter_->GetView()->GetFocusManager()->GetFocusedView();

  if (!focused_view)
    return;

  // When the search box has focus, it is actually the textfield that has focus.
  // As such, the |SearchBoxView| must be told to repaint directly.
  if (focused_view ==
      fullscreen_presenter_->GetView()->search_box_view()->search_box()) {
    fullscreen_presenter_->GetView()->search_box_view()->SchedulePaint();
  } else {
    // Ensure that when an app list item's focus ring is triggered by key
    // events, the item is selected.
    // TODO(https://crbug.com/1262236): class name comparision and static cast
    // should be avoided in the production code. Find a better way to guarantee
    // the item's selection status.
    if (focused_view->GetClassName() == AppListItemView::kViewClassName)
      static_cast<AppListItemView*>(focused_view)->EnsureSelected();

    focused_view->SchedulePaint();
  }
}

bool AppListControllerImpl::IsShowingEmbeddedAssistantUI() const {
  if (bubble_presenter_ && bubble_presenter_->IsShowingEmbeddedAssistantUI())
    return true;
  return fullscreen_presenter_->IsShowingEmbeddedAssistantUI();
}

AppListViewState AppListControllerImpl::CalculateStateAfterShelfDrag(
    const ui::LocatedEvent& event_in_screen,
    float launcher_above_shelf_bottom_amount) const {
  if (fullscreen_presenter_->GetView()) {
    return fullscreen_presenter_->GetView()->CalculateStateAfterShelfDrag(
        event_in_screen, launcher_above_shelf_bottom_amount);
  }
  return AppListViewState::kClosed;
}

void AppListControllerImpl::SetStateTransitionAnimationCallbackForTesting(
    StateTransitionAnimationCallback callback) {
  state_transition_animation_callback_ = std::move(callback);
}

void AppListControllerImpl::SetHomeLauncherAnimationCallbackForTesting(
    HomeLauncherAnimationCallback callback) {
  home_launcher_animation_callback_ = std::move(callback);
}

void AppListControllerImpl::RecordShelfAppLaunched() {
  RecordAppListAppLaunched(
      AppListLaunchedFrom::kLaunchedFromShelf,
      recorded_app_list_view_state_.value_or(GetAppListViewState()),
      IsTabletMode(), recorded_app_list_visibility_.value_or(last_visible_));
  recorded_app_list_view_state_ = absl::nullopt;
  recorded_app_list_visibility_ = absl::nullopt;
}

////////////////////////////////////////////////////////////////////////////////
// Methods of |client_|:

void AppListControllerImpl::StartAssistant() {
  AssistantUiController::Get()->ShowUi(
      AssistantEntryPoint::kLauncherSearchBoxIcon);
}

void AppListControllerImpl::StartSearch(const std::u16string& raw_query) {
  if (client_) {
    std::u16string query;
    base::TrimWhitespace(raw_query, base::TRIM_ALL, &query);
    client_->StartSearch(query);
    auto* notifier = GetNotifier();
    if (notifier)
      notifier->NotifySearchQueryChanged(raw_query);
  }
}

void AppListControllerImpl::OpenSearchResult(
    const std::string& result_id,
    AppListSearchResultType result_type,
    int event_flags,
    AppListLaunchedFrom launched_from,
    AppListLaunchType launch_type,
    int suggestion_index,
    bool launch_as_default) {
  SearchModel* search_model = GetSearchModel();
  SearchResult* result = search_model->FindSearchResult(result_id);
  if (!result)
    return;

  if (launch_type == AppListLaunchType::kAppSearchResult) {
    switch (launched_from) {
      case AppListLaunchedFrom::kLaunchedFromSearchBox:
      case AppListLaunchedFrom::kLaunchedFromSuggestionChip:
        RecordAppLaunched(launched_from);
        break;
      case AppListLaunchedFrom::kLaunchedFromGrid:
      case AppListLaunchedFrom::kLaunchedFromShelf:
        break;
    }
  }

  UMA_HISTOGRAM_ENUMERATION("Apps.AppListSearchResultOpenDisplayType",
                            result->display_type(),
                            SearchResultDisplayType::kLast);

  // Suggestion chips are not represented to the user as search results, so do
  // not record search result metrics for them.
  if (launched_from != AppListLaunchedFrom::kLaunchedFromSuggestionChip) {
    base::RecordAction(base::UserMetricsAction("AppList_OpenSearchResult"));

    UMA_HISTOGRAM_COUNTS_100("Apps.AppListSearchQueryLength",
                             GetLastQueryLength());
    if (IsTabletMode()) {
      UMA_HISTOGRAM_COUNTS_100("Apps.AppListSearchQueryLength.TabletMode",
                               GetLastQueryLength());
    } else {
      UMA_HISTOGRAM_COUNTS_100("Apps.AppListSearchQueryLength.ClamshellMode",
                               GetLastQueryLength());
    }
  }

  auto* notifier = GetNotifier();
  if (notifier) {
    // Special-case chip results, because the display type of app results
    // doesn't account for whether it's being displayed in the suggestion chips
    // or app tiles.
    AppListNotifier::Result notifier_result(result->id(),
                                            result->metrics_type());
    if (launched_from == AppListLaunchedFrom::kLaunchedFromSuggestionChip) {
      notifier->NotifyLaunched(SearchResultDisplayType::kChip, notifier_result);
    } else {
      notifier->NotifyLaunched(result->display_type(), notifier_result);
    }
  }

  if (client_) {
    client_->OpenSearchResult(profile_id_, result_id, result_type, event_flags,
                              launched_from, launch_type, suggestion_index,
                              launch_as_default);
  }

  ResetHomeLauncherIfShown();
}

void AppListControllerImpl::InvokeSearchResultAction(
    const std::string& result_id,
    SearchResultActionType action) {
  if (client_)
    client_->InvokeSearchResultAction(result_id, action);
}

void AppListControllerImpl::GetSearchResultContextMenuModel(
    const std::string& result_id,
    GetContextMenuModelCallback callback) {
  if (client_)
    client_->GetSearchResultContextMenuModel(result_id, std::move(callback));
}

void AppListControllerImpl::ViewShown(int64_t display_id) {
  UpdateAssistantVisibility();

  if (client_)
    client_->ViewShown(display_id);

  // Note that IsHomeScreenVisible() might still return false at this point, as
  // the home screen visibility takes into account whether the app list view is
  // obscured by an app window, or overview UI. This method gets called when the
  // app list view widget visibility changes (regardless of whether anything is
  // stacked above the home screen).
  aura::Window* window = GetHomeScreenWindow();
  split_view_observation_.Observe(SplitViewController::Get(window));
  UpdateHomeScreenVisibility();

  // Ensure search box starts fresh with no ring each time it opens.
  keyboard_traversal_engaged_ = false;
}

bool AppListControllerImpl::AppListTargetVisibility() const {
  return last_target_visible_;
}

void AppListControllerImpl::ViewClosing() {
  if (client_)
    client_->ViewClosing();

  split_view_observation_.Reset();
}

const std::vector<SkColor>&
AppListControllerImpl::GetWallpaperProminentColors() {
  return Shell::Get()->wallpaper_controller()->GetWallpaperColors();
}

void AppListControllerImpl::ActivateItem(const std::string& id,
                                         int event_flags,
                                         AppListLaunchedFrom launched_from) {
  RecordAppLaunched(launched_from);

  if (client_)
    client_->ActivateItem(profile_id_, id, event_flags);

  ResetHomeLauncherIfShown();
}

void AppListControllerImpl::GetContextMenuModel(
    const std::string& id,
    GetContextMenuModelCallback callback) {
  if (client_)
    client_->GetContextMenuModel(profile_id_, id, std::move(callback));
}

void AppListControllerImpl::SortAppList(AppListSortOrder order) {
  if (client_)
    client_->OnAppListSortRequested(profile_id_, order);
}

void AppListControllerImpl::RevertAppListSort() {
  if (client_)
    client_->OnAppListSortRevertRequested(profile_id_);
}

ui::ImplicitAnimationObserver* AppListControllerImpl::GetAnimationObserver(
    AppListViewState target_state) {
  // |presenter_| observes the close animation only.
  if (target_state == AppListViewState::kClosed)
    return fullscreen_presenter_.get();
  return nullptr;
}

void AppListControllerImpl::ShowWallpaperContextMenu(
    const gfx::Point& onscreen_location,
    ui::MenuSourceType source_type) {
  Shell::Get()->ShowContextMenu(onscreen_location, source_type);
}

bool AppListControllerImpl::KeyboardTraversalEngaged() {
  return keyboard_traversal_engaged_;
}

bool AppListControllerImpl::CanProcessEventsOnApplistViews() {
  // Do not allow processing events during overview or while overview is
  // finished but still animating out.
  OverviewController* overview_controller = Shell::Get()->overview_controller();
  if (overview_controller->InOverviewSession() ||
      overview_controller->IsCompletingShutdownAnimations()) {
    return false;
  }

  return true;
}

bool AppListControllerImpl::ShouldDismissImmediately() {
  if (should_dismiss_immediately_)
    return true;

  DCHECK(Shell::HasInstance());
  const int ideal_shelf_y =
      Shelf::ForWindow(
          fullscreen_presenter_->GetView()->GetWidget()->GetNativeView())
          ->GetIdealBounds()
          .y();

  const int current_y = fullscreen_presenter_->GetView()
                            ->GetWidget()
                            ->GetNativeWindow()
                            ->bounds()
                            .y();
  return current_y > ideal_shelf_y;
}

int AppListControllerImpl::GetTargetYForAppListHide(aura::Window* root_window) {
  DCHECK(Shell::HasInstance());
  gfx::Point top_center =
      Shelf::ForWindow(root_window)->GetShelfBoundsInScreen().top_center();
  wm::ConvertPointFromScreen(root_window, &top_center);
  return top_center.y();
}

AssistantViewDelegate* AppListControllerImpl::GetAssistantViewDelegate() {
  return Shell::Get()->assistant_controller()->view_delegate();
}

void AppListControllerImpl::OnSearchResultVisibilityChanged(
    const std::string& id,
    bool visibility) {
  if (client_)
    client_->OnSearchResultVisibilityChanged(id, visibility);
}

void AppListControllerImpl::NotifySearchResultsForLogging(
    const std::u16string& raw_query,
    const SearchResultIdWithPositionIndices& results,
    int position_index) {
  if (client_) {
    std::u16string query;
    base::TrimWhitespace(raw_query, base::TRIM_ALL, &query);
    client_->NotifySearchResultsForLogging(query, results, position_index);
  }
}

void AppListControllerImpl::MaybeIncreaseSuggestedContentInfoShownCount() {
  if (ShouldShowSuggestedContentInfo()) {
    const int count = GetSuggestedContentInfoShownCount();
    SetSuggestedContentInfoShownCount(count + 1);
  }
}

bool AppListControllerImpl::IsAssistantAllowedAndEnabled() const {
  if (!Shell::Get()->assistant_controller()->IsAssistantReady())
    return false;

  auto* state = AssistantState::Get();
  return state->settings_enabled().value_or(false) &&
         state->allowed_state() ==
             chromeos::assistant::AssistantAllowedState::ALLOWED &&
         state->assistant_status() !=
             chromeos::assistant::AssistantStatus::NOT_READY;
}

bool AppListControllerImpl::ShouldShowSuggestedContentInfo() const {
  if (!IsSuggestedContentEnabled()) {
    // Don't show if user has interacted with the setting already.
    SetSuggestedContentInfoDismissed();
    return false;
  }

  if (IsSuggestedContentInfoDismissed()) {
    return false;
  }

  const int count = GetSuggestedContentInfoShownCount();
  constexpr int kThresholdToShow = 3;
  return count >= 0 && count <= kThresholdToShow;
}

void AppListControllerImpl::MarkSuggestedContentInfoDismissed() {
  // User dismissed the privacy info view. Will not show the view again.
  SetSuggestedContentInfoDismissed();
}

void AppListControllerImpl::OnStateTransitionAnimationCompleted(
    AppListViewState state,
    bool was_animation_interrupted) {
  if (!was_animation_interrupted &&
      !state_transition_animation_callback_.is_null()) {
    state_transition_animation_callback_.Run(state);
  }

  if (close_assistant_ui_runner_)
    close_assistant_ui_runner_.RunAndReset();
}

AppListViewState AppListControllerImpl::GetAppListViewState() const {
  return app_list_view_state_;
}

void AppListControllerImpl::OnViewStateChanged(AppListViewState state) {
  app_list_view_state_ = state;

  auto* notifier = GetNotifier();
  if (notifier)
    notifier->NotifyUIStateChanged(state);

  for (auto& observer : observers_)
    observer.OnViewStateChanged(state);

  // Close the Assistant in asynchronous way if the app list is going to be
  // closed while the Assistant is visible. If the app list close animation is
  // not reversed, `close_assistant_ui_runner_` runs at the end of the animation
  // to actually close the Assistant.
  const bool is_assistant_ui_visible =
      (AssistantUiController::Get()->GetModel()->visibility() ==
       AssistantVisibility::kVisible);
  if (state == AppListViewState::kClosed && is_assistant_ui_visible) {
    absl::optional<base::ScopedClosureRunner> runner =
        AssistantUiController::Get()->CloseUi(
            AssistantExitPoint::kLauncherClose);
    DCHECK(runner);
    DCHECK(!close_assistant_ui_runner_);
    close_assistant_ui_runner_.ReplaceClosure(runner->Release());
  }
}

int AppListControllerImpl::AdjustAppListViewScrollOffset(int offset,
                                                         ui::EventType type) {
  Shelf* shelf = Shelf::ForWindow(
      fullscreen_presenter_->GetView()->GetWidget()->GetNativeView());

  // When Natural/Reverse Scrolling is turned on, the events we receive have had
  // their offsets inverted to make that feature work. Certain behaviors, like
  // scrolling on the shelf to expand the app list, only make sense in their
  // original direction, so we undo that inversion.
  int adjusted_offset =
      (type == ui::ET_SCROLL || type == ui::ET_SCROLL_FLING_START)
          ? GetOffset(offset, prefs::kNaturalScroll)
          : GetOffset(offset, prefs::kMouseReverseScroll);

  // If the shelf is side mounted, we set the offset in terms of being toward
  // the shelf to simplify the logic later.
  if (!shelf->IsHorizontalAlignment()) {
    adjusted_offset = shelf->alignment() == ShelfAlignment::kLeft
                          ? adjusted_offset
                          : -adjusted_offset;
  }

  return adjusted_offset;
}

void AppListControllerImpl::LoadIcon(const std::string& app_id) {
  if (client_)
    client_->LoadIcon(profile_id_, app_id);
}

void AppListControllerImpl::GetAppLaunchedMetricParams(
    AppLaunchedMetricParams* metric_params) {
  metric_params->app_list_view_state = GetAppListViewState();
  metric_params->is_tablet_mode = IsTabletMode();
  metric_params->app_list_shown = last_visible_;
}

gfx::Rect AppListControllerImpl::SnapBoundsToDisplayEdge(
    const gfx::Rect& bounds) {
  AppListView* app_list_view = fullscreen_presenter_->GetView();
  DCHECK(app_list_view && app_list_view->GetWidget());
  aura::Window* window = app_list_view->GetWidget()->GetNativeView();
  return screen_util::SnapBoundsToDisplayEdge(bounds, window);
}

AppListState AppListControllerImpl::GetCurrentAppListPage() const {
  return app_list_page_;
}

void AppListControllerImpl::OnAppListPageChanged(AppListState page) {
  const AppListState old_page = app_list_page_;
  if (old_page == page)
    return;

  app_list_page_ = page;

  if (!fullscreen_presenter_)
    return;

  UpdateLauncherContainer();

  if (page == AppListState::kStateEmbeddedAssistant) {
    // ShowUi() will be no-op if the Assistant UI is already visible.
    AssistantUiController::Get()->ShowUi(AssistantEntryPoint::kUnspecified);
    return;
  }

  if (old_page == AppListState::kStateEmbeddedAssistant) {
    // CloseUi() will be no-op if the Assistant UI is already closed.
    AssistantUiController::Get()->CloseUi(AssistantExitPoint::kBackInLauncher);
  }
}

int AppListControllerImpl::GetShelfSize() {
  return ShelfConfig::Get()->system_shelf_size();
}

bool AppListControllerImpl::IsInTabletMode() {
  return Shell::Get()->tablet_mode_controller()->InTabletMode();
}

AppListColorProviderImpl* AppListControllerImpl::GetColorProvider() {
  return &color_provider_;
}

void AppListControllerImpl::RecordAppLaunched(
    AppListLaunchedFrom launched_from) {
  RecordAppListAppLaunched(launched_from, GetAppListViewState(), IsTabletMode(),
                           last_visible_);
}

void AppListControllerImpl::AddObserver(AppListControllerObserver* observer) {
  observers_.AddObserver(observer);
}

void AppListControllerImpl::RemoveObserver(
    AppListControllerObserver* observer) {
  observers_.RemoveObserver(observer);
}

void AppListControllerImpl::OnVisibilityChanged(bool visible,
                                                int64_t display_id) {
  // Focus and app visibility changes while finishing home launcher state
  // animation may cause OnVisibilityChanged() to be called before the home
  // launcher state transition finished - delay the visibility change until
  // the home launcher stops animating, so observers do not miss the animation
  // state update.
  if (home_launcher_transition_state_ !=
      HomeLauncherTransitionState::kFinished) {
    OnVisibilityWillChange(visible, display_id);
    return;
  }

  bool real_visibility = visible;
  // HomeLauncher is only visible when no other app windows are visible,
  // unless we are in the process of animating to (or dragging) the home
  // launcher.
  if (IsTabletMode()) {
    UpdateTrackedAppWindow();

    if (tracked_app_window_)
      real_visibility = false;

    // When transitioning to/from overview, ensure the AppList window is not in
    // the process of being hidden.
    aura::Window* app_list_window = GetWindow();
    real_visibility &= app_list_window && app_list_window->TargetVisibility();
  }

  OnVisibilityWillChange(real_visibility, display_id);

  // Skip adjacent same changes.
  if (last_visible_ == real_visibility &&
      last_visible_display_id_ == display_id) {
    return;
  }

  last_visible_display_id_ = display_id;

  AppListView* const app_list_view = fullscreen_presenter_->GetView();
  if (app_list_view) {
    app_list_view->UpdatePageResetTimer(real_visibility);

    if (!real_visibility) {
      app_list_view->search_box_view()->ClearSearchAndDeactivateSearchBox();
      // Reset the app list contents state, so the app list is in initial state
      // when the app list visibility changes again.
      app_list_view->app_list_main_view()->contents_view()->ResetForShow();
    }
  }

  // Notify chrome of visibility changes.
  if (last_visible_ != real_visibility) {
    // When showing the launcher with the virtual keyboard enabled, one
    // feature called "transient blur" (which means that if focus was lost but
    // regained a few seconds later, we would show the virtual keyboard again)
    // may show the virtual keyboard, which is not what we want. So hide the
    // virtual keyboard explicitly when the launcher shows.
    if (real_visibility)
      keyboard::KeyboardUIController::Get()->HideKeyboardExplicitlyBySystem();

    if (client_)
      client_->OnAppListVisibilityChanged(real_visibility);

    last_visible_ = real_visibility;

    // We could make Assistant sub-controllers an AppListControllerObserver,
    // but we do not want to introduce new dependency of AppListController to
    // Assistant.
    GetAssistantViewDelegate()->OnHostViewVisibilityChanged(real_visibility);
    for (auto& observer : observers_)
      observer.OnAppListVisibilityChanged(real_visibility, display_id);

    if (!home_launcher_animation_callback_.is_null())
      home_launcher_animation_callback_.Run(real_visibility);
  }
}

void AppListControllerImpl::OnWindowVisibilityChanging(aura::Window* window,
                                                       bool visible) {
  if (visible || window != tracked_app_window_)
    return;

  UpdateTrackedAppWindow();

  if (!tracked_app_window_ && ShouldHomeLauncherBeVisible())
    OnVisibilityChanged(true, last_visible_display_id_);
}

void AppListControllerImpl::OnWindowDestroyed(aura::Window* window) {
  if (window != tracked_app_window_)
    return;

  tracked_app_window_ = nullptr;
}

void AppListControllerImpl::OnVisibilityWillChange(bool visible,
                                                   int64_t display_id) {
  bool real_target_visibility = visible;
  // HomeLauncher is only visible when no other app windows are visible,
  // unless we are in the process of animating to (or dragging) the home
  // launcher.
  if (IsTabletMode() && home_launcher_transition_state_ ==
                            HomeLauncherTransitionState::kFinished) {
    real_target_visibility &= !GetTopVisibleWindow();
  }

  // Skip adjacent same changes.
  if (last_target_visible_ == real_target_visibility &&
      last_target_visible_display_id_ == display_id) {
    return;
  }

  // Notify chrome of target visibility changes.
  if (last_target_visible_ != real_target_visibility) {
    last_target_visible_ = real_target_visibility;
    last_target_visible_display_id_ = display_id;

    if (real_target_visibility && fullscreen_presenter_->GetView())
      fullscreen_presenter_->SetViewVisibility(true);

    if (client_)
      client_->OnAppListVisibilityWillChange(real_target_visibility);

    for (auto& observer : observers_) {
      observer.OnAppListVisibilityWillChange(real_target_visibility,
                                             display_id);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// Private used only:

AppListModel* AppListControllerImpl::GetModel() {
  return model_provider_->model();
}

SearchModel* AppListControllerImpl::GetSearchModel() {
  return model_provider_->search_model();
}

void AppListControllerImpl::UpdateAssistantVisibility() {
  GetSearchModel()->search_box()->SetShowAssistantButton(
      IsAssistantAllowedAndEnabled());
}

int64_t AppListControllerImpl::GetDisplayIdToShowAppListOn() {
  if (IsTabletMode() && !Shell::Get()->display_manager()->IsInUnifiedMode()) {
    return display::Display::HasInternalDisplay()
               ? display::Display::InternalDisplayId()
               : display::Screen::GetScreen()->GetPrimaryDisplay().id();
  }

  return display::Screen::GetScreen()
      ->GetDisplayNearestWindow(Shell::GetRootWindowForNewWindows())
      .id();
}

void AppListControllerImpl::ResetHomeLauncherIfShown() {
  if (!IsTabletMode() || !fullscreen_presenter_->IsVisibleDeprecated())
    return;

  auto* const keyboard_controller = keyboard::KeyboardUIController::Get();
  if (keyboard_controller->IsKeyboardVisible())
    keyboard_controller->HideKeyboardByUser();
  fullscreen_presenter_->GetView()->CloseOpenedPage();

  // Refresh the suggestion chips with empty query.
  StartSearch(std::u16string());
}

void AppListControllerImpl::ShowHomeScreen() {
  DCHECK(Shell::Get()->tablet_mode_controller()->InTabletMode());

  if (!Shell::Get()->session_controller()->IsActiveUserSessionStarted())
    return;

  // App list is only considered shown for metrics if there are currently no
  // other visible windows shown over the app list after the tablet
  // transition.
  absl::optional<AppListShowSource> show_source;
  if (!GetTopVisibleWindow())
    show_source = kTabletMode;

  Show(GetDisplayIdToShowAppListOn(), show_source, base::TimeTicks());
  UpdateHomeScreenVisibility();

  aura::Window* window = GetHomeScreenWindow();
  if (window)
    Shelf::ForWindow(window)->MaybeUpdateShelfBackground();
}

void AppListControllerImpl::UpdateHomeScreenVisibility() {
  if (!IsTabletMode())
    return;

  aura::Window* window = GetHomeScreenWindow();
  if (!window)
    return;

  if (ShouldShowHomeScreen())
    window->Show();
  else
    window->Hide();
}

bool AppListControllerImpl::ShouldShowHomeScreen() const {
  if (in_window_dragging_ || in_wallpaper_preview_)
    return false;

  aura::Window* window = GetHomeScreenWindow();
  if (!window)
    return false;

  auto* shell = Shell::Get();
  if (!shell->tablet_mode_controller()->InTabletMode())
    return false;
  if (shell->overview_controller()->InOverviewSession())
    return false;

  return !SplitViewController::Get(window)->InSplitViewMode();
}

bool AppListControllerImpl::ShouldShowAppListBubble() const {
  return !IsTabletMode() && features::IsProductivityLauncherEnabled();
}

void AppListControllerImpl::UpdateForOverviewModeChange(bool show_home_launcher,
                                                        bool animate) {
  // Force the home view into the expected initial state without animation,
  // except when transitioning out from home launcher. Gesture handling for
  // the gesture to move to overview can update the scale before triggering
  // transition to overview - undoing these changes here would make the UI
  // jump during the transition.
  if (animate && show_home_launcher) {
    UpdateScaleAndOpacityForHomeLauncher(
        kOverviewFadeAnimationScale,
        /*opacity=*/0.0f, /*animation_info=*/absl::nullopt,
        /*animation_settings_updater=*/base::NullCallback());
  }

  // Hide all transient child windows in the app list (e.g. uninstall dialog)
  // before starting the overview mode transition, and restore them when
  // reshowing the app list.
  aura::Window* app_list_window =
      Shell::Get()->app_list_controller()->GetHomeScreenWindow();
  if (app_list_window) {
    for (auto* child : wm::GetTransientChildren(app_list_window)) {
      if (show_home_launcher)
        child->Show();
      else
        child->Hide();
    }
  }

  absl::optional<HomeLauncherAnimationInfo> animation_info =
      animate ? absl::make_optional<HomeLauncherAnimationInfo>(
                    HomeLauncherAnimationTrigger::kOverviewModeFade,
                    show_home_launcher)
              : absl::nullopt;
  UpdateAnimationSettingsCallback animation_settings_updater =
      animate ? base::BindRepeating(&UpdateOverviewSettings,
                                    kOverviewFadeAnimationDuration)
              : base::NullCallback();
  const float target_scale =
      show_home_launcher ? 1.0f : kOverviewFadeAnimationScale;
  const float target_opacity = show_home_launcher ? 1.0f : 0.0f;
  UpdateScaleAndOpacityForHomeLauncher(target_scale, target_opacity,
                                       std::move(animation_info),
                                       animation_settings_updater);
}

void AppListControllerImpl::UpdateLauncherContainer(
    absl::optional<int64_t> display_id) {
  aura::Window* window = fullscreen_presenter_->GetWindow();
  if (!window)
    return;

  aura::Window* parent_window = GetContainerForDisplayId(display_id);
  if (parent_window && !parent_window->Contains(window)) {
    parent_window->AddChild(window);
    // Release focus if the launcher is moving behind apps, and there is app
    // window showing. Note that the app list can be shown behind apps in
    // tablet mode only.
    if (IsTabletMode() && !ShouldHomeLauncherBeVisible()) {
      WindowState* const window_state = WindowState::Get(window);
      if (window_state->IsActive())
        window_state->Deactivate();
    }
  }
}

int AppListControllerImpl::GetContainerId() const {
  return ShouldLauncherShowBehindApps() ? kShellWindowId_HomeScreenContainer
                                        : kShellWindowId_AppListContainer;
}

aura::Window* AppListControllerImpl::GetContainerForDisplayId(
    absl::optional<int64_t> display_id) {
  aura::Window* root_window = nullptr;
  if (display_id.has_value()) {
    root_window = Shell::GetRootWindowForDisplayId(display_id.value());
  } else if (fullscreen_presenter_->GetWindow()) {
    root_window = fullscreen_presenter_->GetWindow()->GetRootWindow();
  }

  return root_window ? root_window->GetChildById(GetContainerId()) : nullptr;
}

bool AppListControllerImpl::ShouldLauncherShowBehindApps() const {
  return IsTabletMode() &&
         app_list_page_ != AppListState::kStateEmbeddedAssistant;
}

int AppListControllerImpl::GetLastQueryLength() {
  std::u16string query;
  base::TrimWhitespace(GetSearchModel()->search_box()->text(), base::TRIM_ALL,
                       &query);
  return query.length();
}

void AppListControllerImpl::Shutdown() {
  DCHECK(!is_shutdown_);
  is_shutdown_ = true;

  Shell* shell = Shell::Get();
  AssistantController::Get()->RemoveObserver(this);
  AssistantUiController::Get()->GetModel()->RemoveObserver(this);
  shell->window_tree_host_manager()->RemoveObserver(this);
  AssistantState::Get()->RemoveObserver(this);
  keyboard::KeyboardUIController::Get()->RemoveObserver(this);
  shell->overview_controller()->RemoveObserver(this);
  shell->RemoveShellObserver(this);
  shell->wallpaper_controller()->RemoveObserver(this);
  shell->tablet_mode_controller()->RemoveObserver(this);
  shell->session_controller()->RemoveObserver(this);

  model_observation_.Reset();
}

bool AppListControllerImpl::IsHomeScreenVisible() {
  return IsTabletMode() && IsVisible();
}

void AppListControllerImpl::OnWindowDragStarted() {
  in_window_dragging_ = true;
  UpdateHomeScreenVisibility();

  // Dismiss Assistant if it's running when a window drag starts.
  if (fullscreen_presenter_->IsShowingEmbeddedAssistantUI())
    fullscreen_presenter_->ShowEmbeddedAssistantUI(false);
}

void AppListControllerImpl::OnWindowDragEnded(bool animate) {
  in_window_dragging_ = false;
  UpdateHomeScreenVisibility();
  if (ShouldShowHomeScreen())
    UpdateForOverviewModeChange(/*show_home_launcher=*/true, animate);
}

void AppListControllerImpl::OnAppUpdate(const apps::AppUpdate& update) {
  if (update.HasBadgeChanged() &&
      notification_badging_pref_enabled_.value_or(false)) {
    UpdateItemNotificationBadge(update.AppId(), update.HasBadge());
  }
}

void AppListControllerImpl::OnAppRegistryCacheWillBeDestroyed(
    apps::AppRegistryCache* cache) {
  Observe(nullptr);
}

void AppListControllerImpl::UpdateTrackedAppWindow() {
  // Do not want to observe new windows or further update
  // |tracked_app_window_| once Shutdown() has been called.
  aura::Window* top_window = !is_shutdown_ ? GetTopVisibleWindow() : nullptr;
  if (tracked_app_window_ == top_window)
    return;

  if (tracked_app_window_)
    tracked_app_window_->RemoveObserver(this);
  tracked_app_window_ = top_window;
  if (tracked_app_window_)
    tracked_app_window_->AddObserver(this);
}

void AppListControllerImpl::RecordAppListState() {
  recorded_app_list_view_state_ = GetAppListViewState();
  recorded_app_list_visibility_ = last_visible_;
}

void AppListControllerImpl::UpdateItemNotificationBadge(
    const std::string& app_id,
    apps::mojom::OptionalBool has_badge) {
  AppListItem* item = GetModel()->FindItem(app_id);
  if (item) {
    item->UpdateNotificationBadge(has_badge ==
                                  apps::mojom::OptionalBool::kTrue);
  }
}

void AppListControllerImpl::UpdateAppNotificationBadging() {
  bool new_badging_enabled = pref_change_registrar_
                                 ? pref_change_registrar_->prefs()->GetBoolean(
                                       prefs::kAppNotificationBadgingEnabled)
                                 : false;

  if (notification_badging_pref_enabled_.has_value() &&
      notification_badging_pref_enabled_.value() == new_badging_enabled) {
    return;
  }
  notification_badging_pref_enabled_ = new_badging_enabled;

  if (cache_) {
    cache_->ForEachApp([this](const apps::AppUpdate& update) {
      // Set the app notification badge hidden when the pref is disabled.
      apps::mojom::OptionalBool has_badge =
          notification_badging_pref_enabled_.value() &&
                  (update.HasBadge() == apps::mojom::OptionalBool::kTrue)
              ? apps::mojom::OptionalBool::kTrue
              : apps::mojom::OptionalBool::kFalse;
      UpdateItemNotificationBadge(update.AppId(), has_badge);
    });
  }
}

void AppListControllerImpl::StartTrackingAnimationSmoothness(
    int64_t display_id) {
  auto* root_window = Shell::GetRootWindowForDisplayId(display_id);
  auto* compositor = root_window->layer()->GetCompositor();
  smoothness_tracker_ = compositor->RequestNewThroughputTracker();
  smoothness_tracker_->Start(
      metrics_util::ForSmoothness(base::BindRepeating([](int smoothness) {
        UMA_HISTOGRAM_PERCENTAGE(kHomescreenAnimationHistogram, smoothness);
      })));
}

void AppListControllerImpl::RecordAnimationSmoothness() {
  if (!smoothness_tracker_)
    return;
  smoothness_tracker_->Stop();
  smoothness_tracker_.reset();
}

void AppListControllerImpl::OnGoHomeWindowAnimationsEnded(int64_t display_id) {
  RecordAnimationSmoothness();
  OnHomeLauncherAnimationComplete(/*shown=*/true, display_id);
}

}  // namespace ash
