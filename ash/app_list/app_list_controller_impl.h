// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_APP_LIST_APP_LIST_CONTROLLER_IMPL_H_
#define ASH_APP_LIST_APP_LIST_CONTROLLER_IMPL_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ash/app_list/app_list_color_provider_impl.h"
#include "ash/app_list/app_list_metrics.h"
#include "ash/app_list/app_list_view_delegate.h"
#include "ash/app_list/home_launcher_animation_info.h"
#include "ash/app_list/model/app_list_model.h"
#include "ash/app_list/model/app_list_model_observer.h"
#include "ash/app_list/model/search/search_model.h"
#include "ash/ash_export.h"
#include "ash/assistant/model/assistant_ui_model_observer.h"
#include "ash/display/window_tree_host_manager.h"
#include "ash/public/cpp/app_list/app_list_controller.h"
#include "ash/public/cpp/app_list/app_list_model_delegate.h"
#include "ash/public/cpp/assistant/controller/assistant_controller_observer.h"
#include "ash/public/cpp/keyboard/keyboard_controller_observer.h"
#include "ash/public/cpp/session/session_observer.h"
#include "ash/public/cpp/shelf_types.h"
#include "ash/public/cpp/tablet_mode_observer.h"
#include "ash/public/cpp/wallpaper/wallpaper_controller_observer.h"
#include "ash/shelf/shelf_layout_manager.h"
#include "ash/shell_observer.h"
#include "ash/wm/overview/overview_observer.h"
#include "ash/wm/overview/overview_types.h"
#include "ash/wm/splitview/split_view_observer.h"
#include "base/callback_helpers.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/scoped_observation.h"
#include "components/services/app_service/public/cpp/app_registry_cache.h"
#include "components/sync/model/string_ordinal.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/aura/window_observer.h"
#include "ui/display/types/display_constants.h"

class PrefChangeRegistrar;
class PrefRegistrySimple;

namespace ui {
class MouseWheelEvent;
}  // namespace ui

namespace ash {

class AppListBubblePresenter;
class AppListControllerObserver;
class AppListModelProvider;
class AppListPresenterImpl;
enum class AppListSortOrder;

// Ash's AppListController owns the AppListModel and implements interface
// functions that allow Chrome to modify and observe the Shelf and AppListModel
// state. It also controls the "home launcher", the tablet mode app list.
class ASH_EXPORT AppListControllerImpl
    : public AppListController,
      public SessionObserver,
      public AppListModelObserver,
      public AppListViewDelegate,
      public ShellObserver,
      public OverviewObserver,
      public SplitViewObserver,
      public TabletModeObserver,
      public KeyboardControllerObserver,
      public WallpaperControllerObserver,
      public AssistantStateObserver,
      public WindowTreeHostManager::Observer,
      public aura::WindowObserver,
      public AssistantControllerObserver,
      public AssistantUiModelObserver,
      public apps::AppRegistryCache::Observer {
 public:
  AppListControllerImpl();

  AppListControllerImpl(const AppListControllerImpl&) = delete;
  AppListControllerImpl& operator=(const AppListControllerImpl&) = delete;

  ~AppListControllerImpl() override;

  enum HomeLauncherTransitionState {
    kFinished,      // No drag or animation is in progress
    kMostlyShown,   // The home launcher occupies more than half of the screen
    kMostlyHidden,  // The home launcher occupies less than half of the screen
  };

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // TODO(crbug.com/1204554): Rename to fullscreen_presenter().
  AppListPresenterImpl* presenter() { return fullscreen_presenter_.get(); }

  // AppListController:
  void SetClient(AppListClient* client) override;
  AppListClient* GetClient() override;
  void AddObserver(AppListControllerObserver* observer) override;
  void RemoveObserver(AppListControllerObserver* obsever) override;
  void SetActiveModel(int profile_id,
                      AppListModel* model,
                      SearchModel* search_model) override;
  void ClearActiveModel() override;
  void NotifyProcessSyncChangesFinished() override;
  void DismissAppList() override;
  void GetAppInfoDialogBounds(GetAppInfoDialogBoundsCallback callback) override;
  void ShowAppList() override;
  aura::Window* GetWindow() override;
  bool IsVisible(const absl::optional<int64_t>& display_id) override;
  bool IsVisible() override;

  // AppListModelObserver:
  void OnAppListItemAdded(AppListItem* item) override;

  // SessionObserver:
  void OnActiveUserPrefServiceChanged(PrefService* pref_service) override;
  void OnSessionStateChanged(session_manager::SessionState state) override;

  // Methods used in ash:
  bool GetTargetVisibility(const absl::optional<int64_t>& display_id) const;
  void Show(int64_t display_id,
            absl::optional<AppListShowSource> show_source,
            base::TimeTicks event_time_stamp);
  void UpdateYPositionAndOpacity(int y_position_in_screen,
                                 float background_opacity);
  void EndDragFromShelf(AppListViewState app_list_state);
  void ProcessMouseWheelEvent(const ui::MouseWheelEvent& event);
  void ProcessScrollEvent(const ui::ScrollEvent& event);

  // In tablet mode, takes the user to the home screen, either by ending
  // Overview Mode/Split View Mode or by minimizing the other windows. Returns
  // false if there was nothing to do because the given display was already
  // "home". Illegal to call in clamshell mode.
  bool GoHome(int64_t display_id);

  // Toggles app list visibility. In tablet mode, this can only show the app
  // list (by hiding any windows that might be shown over the homde launcher).
  // |display_id| is the id of display where app list should toggle.
  // |show_source| is the source of the event. |event_time_stamp| records the
  // event timestamp.
  ShelfAction ToggleAppList(int64_t display_id,
                            AppListShowSource show_source,
                            base::TimeTicks event_time_stamp);

  // Returns whether the home launcher should be visible.
  bool ShouldHomeLauncherBeVisible() const;

  // AppListViewDelegate:
  AppListNotifier* GetNotifier() override;
  void StartAssistant() override;
  void StartSearch(const std::u16string& raw_query) override;
  void OpenSearchResult(const std::string& result_id,
                        AppListSearchResultType result_type,
                        int event_flags,
                        AppListLaunchedFrom launched_from,
                        AppListLaunchType launch_type,
                        int suggestion_index,
                        bool launch_as_default) override;
  void InvokeSearchResultAction(const std::string& result_id,
                                SearchResultActionType action) override;
  using GetContextMenuModelCallback =
      AppListViewDelegate::GetContextMenuModelCallback;
  void GetSearchResultContextMenuModel(
      const std::string& result_id,
      GetContextMenuModelCallback callback) override;
  void ViewShown(int64_t display_id) override;
  bool AppListTargetVisibility() const override;
  void ViewClosing() override;
  const std::vector<SkColor>& GetWallpaperProminentColors() override;
  void ActivateItem(const std::string& id,
                    int event_flags,
                    AppListLaunchedFrom launched_from) override;
  void GetContextMenuModel(const std::string& id,
                           GetContextMenuModelCallback callback) override;
  void SortAppList(AppListSortOrder order) override;
  void RevertAppListSort() override;
  ui::ImplicitAnimationObserver* GetAnimationObserver(
      AppListViewState target_state) override;
  void ShowWallpaperContextMenu(const gfx::Point& onscreen_location,
                                ui::MenuSourceType source_type) override;
  bool KeyboardTraversalEngaged() override;
  bool CanProcessEventsOnApplistViews() override;
  bool ShouldDismissImmediately() override;
  int GetTargetYForAppListHide(aura::Window* root_window) override;
  AssistantViewDelegate* GetAssistantViewDelegate() override;
  void OnSearchResultVisibilityChanged(const std::string& id,
                                       bool visibility) override;
  void NotifySearchResultsForLogging(
      const std::u16string& raw_query,
      const SearchResultIdWithPositionIndices& results,
      int position_index) override;
  void MaybeIncreaseSuggestedContentInfoShownCount() override;
  bool IsAssistantAllowedAndEnabled() const override;
  bool ShouldShowSuggestedContentInfo() const override;
  void MarkSuggestedContentInfoDismissed() override;
  void OnStateTransitionAnimationCompleted(
      AppListViewState state,
      bool was_animation_interrupted) override;
  int AdjustAppListViewScrollOffset(int offset, ui::EventType type) override;
  void LoadIcon(const std::string& app_id) override;

  void GetAppLaunchedMetricParams(
      AppLaunchedMetricParams* metric_params) override;
  gfx::Rect SnapBoundsToDisplayEdge(const gfx::Rect& bounds) override;
  AppListState GetCurrentAppListPage() const override;
  void OnAppListPageChanged(AppListState page) override;
  AppListViewState GetAppListViewState() const override;
  void OnViewStateChanged(AppListViewState state) override;
  int GetShelfSize() override;
  bool IsInTabletMode() override;
  AppListColorProviderImpl* GetColorProvider();

  // Notifies observers of AppList visibility changes.
  void OnVisibilityChanged(bool visible, int64_t display_id);
  void OnVisibilityWillChange(bool visible, int64_t display_id);

  // ShellObserver:
  void OnShelfAlignmentChanged(aura::Window* root_window,
                               ShelfAlignment old_alignment) override;
  void OnShellDestroying() override;

  // OverviewObserver:
  void OnOverviewModeStarting() override;
  void OnOverviewModeStartingAnimationComplete(bool canceled) override;
  void OnOverviewModeEnding(OverviewSession* session) override;
  void OnOverviewModeEnded() override;
  void OnOverviewModeEndingAnimationComplete(bool canceled) override;

  // SplitViewObserver:
  void OnSplitViewStateChanged(SplitViewController::State previous_state,
                               SplitViewController::State state) override;

  // TabletModeObserver:
  void OnTabletModeStarted() override;
  void OnTabletModeEnded() override;

  // KeyboardControllerObserver:
  void OnKeyboardVisibilityChanged(bool is_visible) override;

  // WallpaperControllerObserver:
  void OnWallpaperColorsChanged() override;
  void OnWallpaperPreviewStarted() override;
  void OnWallpaperPreviewEnded() override;

  // AssistantStateObserver:
  void OnAssistantStatusChanged(
      chromeos::assistant::AssistantStatus status) override;
  void OnAssistantSettingsEnabled(bool enabled) override;
  void OnAssistantFeatureAllowedChanged(
      chromeos::assistant::AssistantAllowedState state) override;

  // WindowTreeHostManager::Observer:
  void OnDisplayConfigurationChanged() override;

  // aura::WindowObserver:
  void OnWindowVisibilityChanging(aura::Window* window, bool visible) override;
  void OnWindowDestroyed(aura::Window* window) override;

  // AssistantControllerObserver:
  void OnAssistantReady() override;

  // AssistantUiModelObserver:
  void OnUiVisibilityChanged(
      AssistantVisibility new_visibility,
      AssistantVisibility old_visibility,
      absl::optional<AssistantEntryPoint> entry_point,
      absl::optional<AssistantExitPoint> exit_point) override;

  // Gets the home screen window, if available, or null if the home screen
  // window is being hidden for effects (e.g. when dragging windows or
  // previewing the wallpaper).
  aura::Window* GetHomeScreenWindow() const;

  // Scales the home launcher view maintaining the view center point, and
  // updates its opacity. If |callback| is non-null, the update should be
  // animated, and the |callback| should be called with the animation settings.
  // |animation_info| - Information about the transition trigger that will be
  // used to report animation metrics. Should be set only if |callback| is
  // not null (otherwise the transition will not be animated).
  using UpdateAnimationSettingsCallback =
      base::RepeatingCallback<void(ui::ScopedLayerAnimationSettings* settings)>;
  void UpdateScaleAndOpacityForHomeLauncher(
      float scale,
      float opacity,
      absl::optional<HomeLauncherAnimationInfo> animation_info,
      UpdateAnimationSettingsCallback callback);

  // Disables background blur in home screen UI while the returned
  // ScopedClosureRunner is in scope.
  base::ScopedClosureRunner DisableHomeScreenBackgroundBlur();

  // Called when the HomeLauncher positional animation has completed.
  void OnHomeLauncherAnimationComplete(bool shown, int64_t display_id);

  // Called when the HomeLauncher has changed its position on the screen,
  // during either an animation or a drag.
  void OnHomeLauncherPositionChanged(int percent_shown, int64_t display_id);

  // True if home screen is visible.
  bool IsHomeScreenVisible();

  // Called when a window starts/ends dragging. If the home screen is shown, we
  // should hide it during dragging a window and reshow it when the drag ends.
  void OnWindowDragStarted();

  // If |animate| is true, scale-in-to-show home screen if home screen should
  // be shown after drag ends.
  void OnWindowDragEnded(bool animate);

  // apps::AppRegistryCache::Observer:
  void OnAppUpdate(const apps::AppUpdate& update) override;
  void OnAppRegistryCacheWillBeDestroyed(
      apps::AppRegistryCache* cache) override;

  bool onscreen_keyboard_shown() const { return onscreen_keyboard_shown_; }

  // Performs the 'back' action for the active page.
  void Back();

  void SetKeyboardTraversalMode(bool engaged);

  // Returns whether the assistant page is showing (either in bubble app list or
  // fullscreen app list).
  bool IsShowingEmbeddedAssistantUI() const;

  // Get updated app list view state after dragging from shelf.
  AppListViewState CalculateStateAfterShelfDrag(
      const ui::LocatedEvent& event_in_screen,
      float launcher_above_shelf_bottom_amount) const;

  using StateTransitionAnimationCallback =
      base::RepeatingCallback<void(AppListViewState)>;

  void SetStateTransitionAnimationCallbackForTesting(
      StateTransitionAnimationCallback callback);

  using HomeLauncherAnimationCallback =
      base::RepeatingCallback<void(bool shown)>;
  void SetHomeLauncherAnimationCallbackForTesting(
      HomeLauncherAnimationCallback callback);

  AppListBubblePresenter* bubble_presenter_for_test() {
    return bubble_presenter_.get();
  }

  void RecordShelfAppLaunched();

  // Updates which container the launcher window should be in.
  void UpdateLauncherContainer(
      absl::optional<int64_t> display_id = absl::nullopt);

  // Gets the container which should contain the AppList.
  int GetContainerId() const;

  // Returns whether the launcher should show behinds apps or infront of them.
  bool ShouldLauncherShowBehindApps() const;

  // Returns the parent window of the applist for a |display_id|.
  aura::Window* GetContainerForDisplayId(
      absl::optional<int64_t> display_id = absl::nullopt);

  // Methods for recording the state of the app list before it changes in order
  // to record metrics.
  void RecordAppListState();

 private:
  // Convenience methods for getting models from `model_provider_`.
  AppListModel* GetModel();
  SearchModel* GetSearchModel();

  std::unique_ptr<AppListItem> CreateAppListItem(
      std::unique_ptr<AppListItemMetadata> metadata);
  // Update the visibility of Assistant functionality.
  void UpdateAssistantVisibility();

  int64_t GetDisplayIdToShowAppListOn();

  void ResetHomeLauncherIfShown();

  void ShowHomeScreen();

  // Updates the visibility of the home screen based on e.g. if the device is
  // in overview mode.
  void UpdateHomeScreenVisibility();

  // Returns true if home screen should be shown based on the current
  // configuration.
  bool ShouldShowHomeScreen() const;

  // Returns true if the bubble app list should be shown (instead of the
  // fullscreen app list), based on tablet mode state and the feature flag.
  bool ShouldShowAppListBubble() const;

  // Updates home launcher scale and opacity when the overview mode state
  // changes. `show_home_launcher` - whether the home launcher should be shown.
  // `animate` - whether the transition should be animated.
  void UpdateForOverviewModeChange(bool show_home_launcher, bool animate);

  // Returns the length of the most recent query.
  int GetLastQueryLength();

  // Shuts down the AppListControllerImpl, removing itself as an observer.
  void Shutdown();

  // Record the app launch for AppListAppLaunchedV2 metric.
  void RecordAppLaunched(AppListLaunchedFrom launched_from);

  // Updates the window that is tracked as |tracked_app_window_|.
  void UpdateTrackedAppWindow();

  // Updates whether a notification badge is shown for the AppListItemView
  // corresponding with the |app_id|.
  void UpdateItemNotificationBadge(const std::string& app_id,
                                   apps::mojom::OptionalBool has_badge);

  // Checks the notification badging pref and then updates whether a
  // notification badge is shown for each AppListItem.
  void UpdateAppNotificationBadging();

  // Responsible for starting or stopping |smoothness_tracker_|.
  void StartTrackingAnimationSmoothness(int64_t display_id);
  void RecordAnimationSmoothness();

  // Called when all the window minimize animations triggered by a tablet mode
  // "Go Home" have ended. |display_id| is the home screen display ID.
  void OnGoHomeWindowAnimationsEnded(int64_t display_id);

  // Whether the home launcher is
  // * being shown (either through an animation or a drag)
  // * being hidden (either through an animation or a drag)
  // * not animating nor being dragged.
  // In the case where the home launcher is being dragged, the gesture can
  // reverse direction at any point during the drag, in which case the only
  // information given by "showing" versus "hiding" is the starting point of
  // the drag and the assumed final state (which won't be accurate if the
  // gesture is reversed).
  HomeLauncherTransitionState home_launcher_transition_state_ = kFinished;

  AppListClient* client_ = nullptr;

  // Tracks active app list and search models to app list UI stack. It can be
  // accessed outside AppListModelControllerImpl using
  // `AppListModelController::Get()`.
  std::unique_ptr<AppListModelProvider> model_provider_;

  // Used to fetch colors from AshColorProvider. Should be destructed after
  // |presenter_| and UI.
  AppListColorProviderImpl color_provider_;

  // Manages the fullscreen/peeking launcher and the tablet mode home launcher.
  // |fullscreen_presenter_| should be put below |client_| and |model_| to
  // prevent a crash in destruction.
  std::unique_ptr<AppListPresenterImpl> fullscreen_presenter_;

  // Manages the clamshell launcher bubble. Null when the feature AppListBubble
  // is disabled.
  std::unique_ptr<AppListBubblePresenter> bubble_presenter_;

  // Tracks the current page shown in the app list view (tracked for the
  // fullscreen presenter).
  AppListState app_list_page_ = AppListState::kInvalidState;

  // Tracks the current state of `AppListView` (tracked for the fullscreen
  // presenter)
  AppListViewState app_list_view_state_ = AppListViewState::kClosed;

  // True if the on-screen keyboard is shown.
  bool onscreen_keyboard_shown_ = false;

  // True if the most recent event handled by |presenter_| was a key event.
  bool keyboard_traversal_engaged_ = false;

  // True if Shutdown() has been called.
  bool is_shutdown_ = false;

  // Whether to immediately dismiss the AppListView.
  bool should_dismiss_immediately_ = false;

  // The last target visibility change and its display id.
  bool last_target_visible_ = false;
  int64_t last_target_visible_display_id_ = display::kInvalidDisplayId;

  // The last visibility change and its display id.
  bool last_visible_ = false;
  int64_t last_visible_display_id_ = display::kInvalidDisplayId;

  // Used in mojo callings to specify the profile whose app list data is
  // read/written by Ash side through IPC. Notice that in multi-profile mode,
  // each profile has its own AppListModelUpdater to manipulate app list items.
  int profile_id_ = kAppListInvalidProfileID;

  // Used when tablet mode is active to track the MRU window among the windows
  // that were obscuring the home launcher when the home launcher visibility was
  // last calculated.
  // This window changing it's visibility to false is used as a signal that the
  // home launcher visibility should be recalculated.
  aura::Window* tracked_app_window_ = nullptr;

  // A callback that can be registered by a test to wait for the app list state
  // transition animation to finish.
  StateTransitionAnimationCallback state_transition_animation_callback_;

  // A callback that can be registered by a test to wait for the home launcher
  // visibility animation to finish. Should only be used in tablet mode.
  HomeLauncherAnimationCallback home_launcher_animation_callback_;

  // The AppListViewState at the moment it was recorded, used to record app
  // launching metrics. This allows an accurate AppListViewState to be recorded
  // before AppListViewState changes.
  absl::optional<AppListViewState> recorded_app_list_view_state_;

  // Whether the applist was shown at the moment it was recorded, used to record
  // app launching metrics. This is recorded because AppList visibility can
  // change before the metric is recorded.
  absl::optional<bool> recorded_app_list_visibility_;

  // ScopedClosureRunner which while in scope keeps background blur in home
  // screen (in particular, apps container suggestion chips background)
  // disabled. Set while home screen transitions are in progress.
  absl::optional<base::ScopedClosureRunner> home_screen_blur_disabler_;

  base::ObserverList<AppListControllerObserver> observers_;

  // Observed to update notification badging on app list items. Also used to get
  // initial notification badge information when app list items are added.
  apps::AppRegistryCache* cache_ = nullptr;

  // Observes user profile prefs for the app list.
  std::unique_ptr<PrefChangeRegistrar> pref_change_registrar_;

  // Whether the pref for notification badging is enabled.
  absl::optional<bool> notification_badging_pref_enabled_;

  // Whether the wallpaper is being previewed. The home screen should be hidden
  // during wallpaper preview.
  bool in_wallpaper_preview_ = false;

  // Whether we're currently in a window dragging process.
  bool in_window_dragging_ = false;

  // The last overview mode exit type - cached when the overview exit starts, so
  // it can be used to decide how to update home screen when overview mode exit
  // animations are finished (at which point this information will not be
  // available).
  absl::optional<OverviewEnterExitType> overview_exit_type_;

  // Responsible for recording smoothness related UMA stats for home screen
  // animations.
  absl::optional<ui::ThroughputTracker> smoothness_tracker_;

  // Used for closing the Assistant ui in the asynchronous way.
  base::ScopedClosureRunner close_assistant_ui_runner_;

  base::ScopedObservation<AppListModel, AppListModelObserver>
      model_observation_{this};

  base::ScopedObservation<SplitViewController, SplitViewObserver>
      split_view_observation_{this};

  base::WeakPtrFactory<AppListControllerImpl> weak_ptr_factory_{this};
};

}  // namespace ash

#endif  // ASH_APP_LIST_APP_LIST_CONTROLLER_IMPL_H_
