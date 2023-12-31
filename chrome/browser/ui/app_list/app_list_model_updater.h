// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APP_LIST_APP_LIST_MODEL_UPDATER_H_
#define CHROME_BROWSER_UI_APP_LIST_APP_LIST_MODEL_UPDATER_H_

#include <memory>
#include <string>
#include <vector>

#include "ash/public/cpp/app_list/app_list_types.h"
#include "base/callback_forward.h"
#include "base/containers/flat_map.h"
#include "chrome/browser/ui/app_list/app_list_model_updater_observer.h"
#include "chrome/browser/ui/app_list/app_list_syncable_service.h"
#include "chrome/browser/ui/app_list/chrome_app_list_item.h"

class ChromeSearchResult;

namespace ui {
class SimpleMenuModel;
}  // namespace ui

// An interface to wrap AppListModel access in browser.
class AppListModelUpdater {
 public:
  class TestApi {
   public:
    explicit TestApi(AppListModelUpdater* model_updater)
        : model_updater_(model_updater) {}
    ~TestApi() = default;

    void SetItemPosition(const std::string& id,
                         const syncer::StringOrdinal& new_position) {
      model_updater_->SetItemPosition(id, new_position);
    }

   private:
    AppListModelUpdater* const model_updater_;
  };

  virtual ~AppListModelUpdater() {}

  int model_id() const { return model_id_; }

  // Set whether this model updater is active.
  // When we have multiple user profiles, only the active one has access to the
  // model. All others profile can only cache model changes in Chrome.
  virtual void SetActive(bool active) {}

  // For AppListModel:
  virtual void AddItem(std::unique_ptr<ChromeAppListItem> item) {}
  virtual void AddItemToFolder(std::unique_ptr<ChromeAppListItem> item,
                               const std::string& folder_id) {}
  virtual void RemoveItem(const std::string& id) {}
  virtual void RemoveUninstalledItem(const std::string& id) {}
  virtual void SetStatus(ash::AppListModelStatus status) {}
  // For SearchModel:
  virtual void SetSearchEngineIsGoogle(bool is_google) {}
  virtual void UpdateSearchBox(const std::u16string& text,
                               bool initiated_by_user) {}
  virtual void PublishSearchResults(
      const std::vector<ChromeSearchResult*>& results,
      const std::vector<ash::AppListSearchResultCategory>& categories) {}
  virtual std::vector<ChromeSearchResult*> GetPublishedSearchResultsForTest();

  // Item field setters only used by ChromeAppListItem and its derived classes.
  virtual void SetItemIconVersion(const std::string& id, int icon_version) {}
  virtual void SetItemIcon(const std::string& id, const gfx::ImageSkia& icon) {}
  virtual void SetItemName(const std::string& id, const std::string& name) {}
  virtual void SetItemNameAndShortName(const std::string& id,
                                       const std::string& name,
                                       const std::string& short_name) {}
  virtual void SetAppStatus(const std::string& id, ash::AppStatus app_status) {}
  virtual void SetItemPosition(const std::string& id,
                               const syncer::StringOrdinal& new_position) {}
  virtual void SetItemIsPersistent(const std::string& id, bool is_persistent) {}
  virtual void SetItemFolderId(const std::string& id,
                               const std::string& folder_id) = 0;
  virtual void SetNotificationBadgeColor(const std::string& id,
                                         const SkColor color) {}

  virtual void SetSearchResultMetadata(
      const std::string& id,
      std::unique_ptr<ash::SearchResultMetadata> metadata) {}
  virtual void SetSearchResultIcon(const std::string& id,
                                   const gfx::ImageSkia& icon) {}
  virtual void SetSearchResultBadgeIcon(const std::string& id,
                                        const gfx::ImageSkia& badge_icon) {}
  virtual void ActivateChromeItem(const std::string& id, int event_flags) {}
  virtual void LoadAppIcon(const std::string& id) {}

  // For AppListModel:
  virtual ChromeAppListItem* FindItem(const std::string& id) = 0;
  virtual std::vector<const ChromeAppListItem*> GetItems() const = 0;
  virtual size_t ItemCount() = 0;
  virtual std::vector<ChromeAppListItem*> GetTopLevelItems() const = 0;
  virtual ChromeAppListItem* ItemAtForTest(size_t index) = 0;
  virtual ChromeAppListItem* FindFolderItem(const std::string& folder_id) = 0;
  virtual bool FindItemIndexForTest(const std::string& id, size_t* index) = 0;
  using GetIdToAppListIndexMapCallback =
      base::OnceCallback<void(const base::flat_map<std::string, uint16_t>&)>;
  virtual void GetIdToAppListIndexMap(GetIdToAppListIndexMapCallback callback) {
  }
  // Calculates the default position of `new_item` that is not added to the
  // model yet.
  // TODO(https://crbug.com/1261899): This function cannot be const because
  // during calculation the sort order saved in prefs could be reset. The
  // function name is misleading. Replace it with a better function name.
  virtual syncer::StringOrdinal CalculatePositionForNewItem(
      const ChromeAppListItem& new_item) = 0;
  // Returns a position which is before the first item in the item list.
  virtual syncer::StringOrdinal GetPositionBeforeFirstItem() const = 0;

  // Methods for AppListSyncableService:
  virtual void UpdateAppItemFromSyncItem(
      app_list::AppListSyncableService::SyncItem* sync_item,
      bool update_name,
      bool update_folder) {}
  virtual void NotifyProcessSyncChangesFinished() {}

  using GetMenuModelCallback =
      base::OnceCallback<void(std::unique_ptr<ui::SimpleMenuModel>)>;
  virtual void GetContextMenuModel(const std::string& id,
                                   GetMenuModelCallback callback) = 0;
  virtual size_t BadgedItemCount() = 0;
  // For SearchModel:
  virtual bool SearchEngineIsGoogle() = 0;

  // Methods for handle model updates in ash:
  virtual void OnSortRequested(ash::AppListSortOrder order) = 0;
  virtual void OnSortRevertRequested() = 0;

  // Notifies when the app list gets hidden.
  virtual void OnAppListHidden() = 0;

  virtual void AddObserver(AppListModelUpdaterObserver* observer) = 0;
  virtual void RemoveObserver(AppListModelUpdaterObserver* observer) = 0;

 protected:
  FRIEND_TEST_ALL_PREFIXES(AppListSyncableServiceTest, FirstAvailablePosition);
  FRIEND_TEST_ALL_PREFIXES(AppListSyncableServiceTest,
                           FirstAvailablePositionNotExist);

  AppListModelUpdater();

  // Returns the first available position in app list.
  syncer::StringOrdinal GetFirstAvailablePosition() const;

  // Returns a position which is before the first item in the app list. If
  // |top_level_items| is empty, creates an initial position instead.
  static syncer::StringOrdinal GetPositionBeforeFirstItemInternal(
      const std::vector<ChromeAppListItem*>& top_level_items);

 private:
  const int model_id_;
};

#endif  // CHROME_BROWSER_UI_APP_LIST_APP_LIST_MODEL_UPDATER_H_
