// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/app_list_syncable_service.h"

#include <algorithm>
#include <utility>

#include "ash/constants/ash_features.h"
#include "ash/public/cpp/app_list/app_list_config.h"
#include "ash/public/cpp/app_list/internal_app_id_constants.h"
#include "base/bind.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/scoped_command_line.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/app_list/app_list_client_impl.h"
#include "chrome/browser/ui/app_list/app_list_model_updater.h"
#include "chrome/browser/ui/app_list/app_list_test_util.h"
#include "chrome/browser/ui/app_list/chrome_app_list_item.h"
#include "chrome/browser/ui/app_list/page_break_constants.h"
#include "chrome/browser/ui/app_list/reorder/app_list_reorder_delegate.h"
#include "chrome/browser/ui/app_list/test/app_list_syncable_service_test_base.h"
#include "chrome/browser/ui/app_list/test/fake_app_list_model_updater.h"
#include "chrome/browser/ui/settings_window_manager_chromeos.h"
#include "chrome/browser/web_applications/test/web_app_install_test_utils.h"
#include "chrome/browser/web_applications/web_app_id_constants.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "components/crx_file/id_util.h"
#include "components/sync/base/client_tag_hash.h"
#include "components/sync/model/sync_error_factory.h"
#include "components/sync/protocol/app_list_specifics.pb.h"
#include "components/sync/protocol/entity_specifics.pb.h"
#include "components/sync/test/model/fake_sync_change_processor.h"
#include "components/sync/test/model/sync_error_factory_mock.h"
#include "extensions/common/constants.h"
#include "testing/gmock/include/gmock/gmock.h"

using crx_file::id_util::GenerateId;
using testing::ElementsAre;
using ItemTestApi = ChromeAppListItem::TestApi;

namespace {

const char kOsSettingsUrl[] = "chrome://os-settings/";

// These constants are defined as functions so their values can be derived via
// function calls.  The constant naming scheme is kept to maintain readability.
const std::string kInvalidOrdinalsId() {
  return GenerateId("invalid_ordinals");
}
const std::string kEmptyItemNameId() {
  return GenerateId("empty_item_name");
}
const std::string kEmptyItemNameUnsetId() {
  return GenerateId("empty_item_name_unset");
}
const std::string kEmptyParentId() {
  return GenerateId("empty_parent_id");
}
const std::string kEmptyParentUnsetId() {
  return GenerateId("empty_parent_id_unset");
}
const std::string kEmptyOrdinalsId() {
  return GenerateId("empty_ordinals");
}
const std::string kEmptyOrdinalsUnsetId() {
  return GenerateId("empty_ordinals_unset");
}
const std::string kDupeItemId() {
  return GenerateId("dupe_item_id");
}
const std::string kParentId() {
  return GenerateId("parent_id");
}

syncer::SyncDataList CreateBadAppRemoteData(const std::string& id) {
  syncer::SyncDataList sync_list;
  // Invalid item_ordinal and item_pin_ordinal.
  sync_list.push_back(CreateAppRemoteData(
      id == kDefault ? kInvalidOrdinalsId() : id, "item_name", kParentId(),
      "$$invalid_ordinal$$", "$$invalid_ordinal$$"));
  // Empty item name.
  sync_list.push_back(
      CreateAppRemoteData(id == kDefault ? kEmptyItemNameId() : id, "",
                          kParentId(), "ordinal", "pinordinal"));
  sync_list.push_back(
      CreateAppRemoteData(id == kDefault ? kEmptyItemNameUnsetId() : id, kUnset,
                          kParentId(), "ordinal", "pinordinal"));
  // Empty parent ID.
  sync_list.push_back(
      CreateAppRemoteData(id == kDefault ? kEmptyParentId() : id, "item_name",
                          "", "ordinal", "pinordinal"));
  sync_list.push_back(
      CreateAppRemoteData(id == kDefault ? kEmptyParentUnsetId() : id,
                          "item_name", kUnset, "ordinal", "pinordinal"));
  // Empty item_ordinal and item_pin_ordinal.
  sync_list.push_back(
      CreateAppRemoteData(id == kDefault ? kEmptyOrdinalsId() : id, "item_name",
                          kParentId(), "", ""));
  sync_list.push_back(
      CreateAppRemoteData(id == kDefault ? kEmptyOrdinalsUnsetId() : id,
                          "item_name", kParentId(), kUnset, kUnset));
  // Duplicate item_id.
  sync_list.push_back(CreateAppRemoteData(id == kDefault ? kDupeItemId() : id,
                                          "item_name", kParentId(), "ordinal",
                                          "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(id == kDefault ? kDupeItemId() : id,
                                          "item_name_dupe", kParentId(),
                                          "ordinal", "pinordinal"));
  // Empty item_id.
  sync_list.push_back(CreateAppRemoteData("", "item_name", kParentId(),
                                          "ordinal", "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(kUnset, "item_name", kParentId(),
                                          "ordinal", "pinordinal"));
  // All fields empty.
  sync_list.push_back(CreateAppRemoteData("", "", "", "", ""));
  sync_list.push_back(
      CreateAppRemoteData(kUnset, kUnset, kUnset, kUnset, kUnset));

  return sync_list;
}

bool AreAllAppAtributesEqualInSync(
    const app_list::AppListSyncableService::SyncItem* item1,
    const app_list::AppListSyncableService::SyncItem* item2) {
  return item1->parent_id == item2->parent_id &&
         item1->item_ordinal.EqualsOrBothInvalid(item2->item_ordinal) &&
         item1->item_pin_ordinal.EqualsOrBothInvalid(item2->item_pin_ordinal);
}

bool AreAllAppAtributesNotEqualInSync(
    const app_list::AppListSyncableService::SyncItem* item1,
    const app_list::AppListSyncableService::SyncItem* item2) {
  return item1->parent_id != item2->parent_id &&
         !item1->item_ordinal.EqualsOrBothInvalid(item2->item_ordinal) &&
         !item1->item_pin_ordinal.EqualsOrBothInvalid(item2->item_pin_ordinal);
}

bool AreAllAppAtributesEqualInAppList(const ChromeAppListItem* item1,
                                      const ChromeAppListItem* item2) {
  // Note, there is no pin position in app list.
  return item1->folder_id() == item2->folder_id() &&
         item1->position().EqualsOrBothInvalid(item2->position());
}

bool AreAllAppAtributesNotEqualInAppList(const ChromeAppListItem* item1,
                                         const ChromeAppListItem* item2) {
  // Note, there is no pin position in app list.
  return item1->folder_id() != item2->folder_id() &&
         !item1->position().EqualsOrBothInvalid(item2->position());
}

std::string GetLastPositionString() {
  static syncer::StringOrdinal last_position;
  if (!last_position.IsValid())
    last_position = syncer::StringOrdinal::CreateInitialOrdinal();
  else
    last_position = last_position.CreateAfter();
  return last_position.ToDebugString();
}

}  // namespace

// The class that verifies app list syncable service features. Use a fake app
// list model updater during testing.
class AppListSyncableServiceTest : public test::AppListSyncableServiceTestBase {
 public:
  AppListSyncableServiceTest() = default;
  AppListSyncableServiceTest(const AppListSyncableServiceTest&) = delete;
  AppListSyncableServiceTest& operator=(const AppListSyncableServiceTest&) =
      delete;
  ~AppListSyncableServiceTest() override = default;

  // test::AppListSyncableServiceTestBase:
  void SetUp() override {
    AppListSyncableServiceTestBase::SetUp();
    model_updater_test_api_ =
        std::make_unique<AppListModelUpdater::TestApi>(GetModelUpdater());
  }

  void TearDown() override { app_list_syncable_service_.reset(); }

  AppListModelUpdater::TestApi* model_updater_test_api() {
    return model_updater_test_api_.get();
  }

 private:
  // test::AppListSyncableServiceTestBase:
  void SetUpFakeModelUpdaterFactoryIfNecessary() override {
    model_updater_factory_scope_ = std::make_unique<
        app_list::AppListSyncableService::ScopedModelUpdaterFactoryForTest>(
        base::BindRepeating(
            [](app_list::AppListReorderDelegate* reorder_delegate)
                -> std::unique_ptr<AppListModelUpdater> {
              return std::make_unique<FakeAppListModelUpdater>(
                  /*profile=*/nullptr, reorder_delegate);
            }));
  }

  std::unique_ptr<AppListModelUpdater::TestApi> model_updater_test_api_;
  std::unique_ptr<
      app_list::AppListSyncableService::ScopedModelUpdaterFactoryForTest>
      model_updater_factory_scope_;
};

TEST_F(AppListSyncableServiceTest, OEMFolderForConflictingPos) {
  // Create a "web store" app.
  const std::string web_store_app_id(extensions::kWebStoreAppId);
  scoped_refptr<extensions::Extension> store =
      MakeApp("webstore", web_store_app_id,
              extensions::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(store.get());

  // Create some app. Note its id should be greater than web store app id in
  // order to move app in case of conflicting pos after web store app.
  const std::string test_app_1_id = CreateNextAppId(extensions::kWebStoreAppId);
  scoped_refptr<extensions::Extension> test_app_1 =
      MakeApp(kSomeAppName, test_app_1_id,
              extensions ::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(test_app_1.get());

  const std::string test_app_2_id = CreateNextAppId(test_app_1_id);
  scoped_refptr<extensions::Extension> test_app_2 =
      MakeApp(kSomeAppName, test_app_2_id,
              extensions ::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(test_app_2.get());

  AppListModelUpdater* model_updater = GetModelUpdater();
  ChromeAppListItem* web_store_item = model_updater->FindItem(web_store_app_id);
  ASSERT_TRUE(web_store_item);
  ChromeAppListItem* test_app_1_item = model_updater->FindItem(test_app_1_id);
  ASSERT_TRUE(test_app_1_item);

  ChromeAppListItem* test_app_2_item = model_updater->FindItem(test_app_2_id);
  ASSERT_TRUE(test_app_2_item);
  // Simulate position conflict.
  model_updater_test_api()->SetItemPosition(web_store_item->id(),
                                            test_app_1_item->position());

  // Position second test app after the webstore and the first test app.
  model_updater_test_api()->SetItemPosition(
      test_app_2_item->id(), test_app_1_item->position().CreateAfter());

  // Install an OEM app. It must be placed by default after web store app but in
  // case of app of the same position should be shifted next.
  const std::string oem_app_id = CreateNextAppId(test_app_2_id);
  scoped_refptr<extensions::Extension> oem_app = MakeApp(
      kOemAppName, oem_app_id, extensions::Extension::WAS_INSTALLED_BY_OEM);
  InstallExtension(oem_app.get());

  // OEM item is not top level element.
  ChromeAppListItem* oem_app_item = model_updater->FindItem(oem_app_id);
  EXPECT_NE(nullptr, oem_app_item);
  EXPECT_EQ(oem_app_item->folder_id(), ash::kOemFolderId);

  // But OEM folder is.
  ChromeAppListItem* oem_folder = model_updater->FindItem(ash::kOemFolderId);
  ASSERT_NE(nullptr, oem_folder);
  EXPECT_EQ(oem_folder->folder_id(), "");

  EXPECT_TRUE(oem_folder->position().GreaterThan(web_store_item->position()));
  EXPECT_TRUE(oem_folder->position().GreaterThan(test_app_1_item->position()));
  EXPECT_TRUE(oem_folder->position().LessThan(test_app_2_item->position()));

  // Receiving initial sync data does not change the OEM folder position.
  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  EXPECT_TRUE(oem_folder->position().GreaterThan(web_store_item->position()));
  EXPECT_TRUE(oem_folder->position().GreaterThan(test_app_1_item->position()));
  EXPECT_TRUE(oem_folder->position().LessThan(test_app_2_item->position()));
}

// Verifies that OEM folder is positioned at the end of the list if initial sync
// data that contains non-default apps is received after an OEM data is
// received.
TEST_F(AppListSyncableServiceTest,
       OEMFolderPositionUpdatedAfterInitialSyncWithNonDefaultApps) {
  // Create a "web store" app.
  const std::string web_store_app_id(extensions::kWebStoreAppId);
  scoped_refptr<extensions::Extension> store =
      MakeApp("webstore", web_store_app_id,
              extensions::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(store.get());
  AppListModelUpdater* model_updater = GetModelUpdater();
  ChromeAppListItem* web_store_item = model_updater->FindItem(web_store_app_id);

  const std::string test_app_id = CreateNextAppId(extensions::kWebStoreAppId);
  scoped_refptr<extensions::Extension> test_app =
      MakeApp(kSomeAppName, test_app_id,
              extensions ::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(test_app.get());
  ChromeAppListItem* test_app_item = model_updater->FindItem(test_app_id);
  model_updater_test_api()->SetItemPosition(
      test_app_item->id(), web_store_item->position().CreateAfter());

  // Install an OEM app. It must be placed by default after web store app but in
  // case of app of the same position should be shifted next.
  const std::string oem_app_id = CreateNextAppId(test_app_id);
  scoped_refptr<extensions::Extension> oem_app = MakeApp(
      kOemAppName, oem_app_id, extensions::Extension::WAS_INSTALLED_BY_OEM);
  InstallExtension(oem_app.get());

  syncer::StringOrdinal sync_item_ordinal =
      test_app_item->position().CreateAfter();

  syncer::SyncDataList sync_list;
  sync_list.push_back(CreateAppRemoteData(
      "non-oem-app", "Non OEM app", std::string() /* parent_id */,
      sync_item_ordinal.ToInternalValue(),
      std::string() /* item_pin_ordinal */));
  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  ChromeAppListItem* oem_folder = model_updater->FindItem(ash::kOemFolderId);
  ASSERT_NE(nullptr, oem_folder);
  EXPECT_EQ(oem_folder->folder_id(), "");

  EXPECT_TRUE(oem_folder->position().GreaterThan(web_store_item->position()));
  EXPECT_TRUE(oem_folder->position().GreaterThan(test_app_item->position()));
  EXPECT_TRUE(oem_folder->position().GreaterThan(sync_item_ordinal));
}

TEST_F(AppListSyncableServiceTest,
       OEMFolderPositionedAtEndIfNonDefaultAppsSynced) {
  const std::string web_store_app_id(extensions::kWebStoreAppId);
  scoped_refptr<extensions::Extension> store =
      MakeApp("webstore", web_store_app_id,
              extensions::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(store.get());
  AppListModelUpdater* model_updater = GetModelUpdater();
  ChromeAppListItem* web_store_item = model_updater->FindItem(web_store_app_id);

  const std::string test_app_id = CreateNextAppId(extensions::kWebStoreAppId);
  scoped_refptr<extensions::Extension> test_app =
      MakeApp(kSomeAppName, test_app_id,
              extensions ::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(test_app.get());
  ChromeAppListItem* test_app_item = model_updater->FindItem(test_app_id);
  model_updater_test_api()->SetItemPosition(
      test_app_item->id(), web_store_item->position().CreateAfter());

  syncer::StringOrdinal sync_item_ordinal =
      test_app_item->position().CreateAfter();

  syncer::SyncDataList sync_list;
  sync_list.push_back(CreateAppRemoteData(
      "non-oem-app", "Non OEM app", std::string() /* parent_id */,
      sync_item_ordinal.ToInternalValue(),
      std::string() /* item_pin_ordinal */));
  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  // Install an OEM app. It must be placed by default after web store app but in
  // case of app of the same position should be shifted next.
  const std::string oem_app_id = CreateNextAppId(test_app_id);
  scoped_refptr<extensions::Extension> oem_app = MakeApp(
      kOemAppName, oem_app_id, extensions::Extension::WAS_INSTALLED_BY_OEM);
  InstallExtension(oem_app.get());

  ChromeAppListItem* oem_folder = model_updater->FindItem(ash::kOemFolderId);
  ASSERT_NE(nullptr, oem_folder);
  EXPECT_EQ(oem_folder->folder_id(), "");

  EXPECT_TRUE(oem_folder->position().GreaterThan(web_store_item->position()));
  EXPECT_TRUE(oem_folder->position().GreaterThan(test_app_item->position()));
  EXPECT_TRUE(oem_folder->position().GreaterThan(sync_item_ordinal));
}

TEST_F(AppListSyncableServiceTest,
       OEMFolderPositionedAfterWebstoreIfOnlyDefaultAppsSynced) {
  const std::string web_store_app_id(extensions::kWebStoreAppId);
  scoped_refptr<extensions::Extension> store =
      MakeApp("webstore", web_store_app_id,
              extensions::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(store.get());
  AppListModelUpdater* model_updater = GetModelUpdater();
  ChromeAppListItem* web_store_item = model_updater->FindItem(web_store_app_id);

  const std::string test_app_id = CreateNextAppId(extensions::kWebStoreAppId);
  scoped_refptr<extensions::Extension> test_app =
      MakeApp(kSomeAppName, test_app_id,
              extensions ::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(test_app.get());
  ChromeAppListItem* test_app_item = model_updater->FindItem(test_app_id);
  model_updater_test_api()->SetItemPosition(
      test_app_item->id(), web_store_item->position().CreateAfter());

  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  const std::string oem_app_id = CreateNextAppId(test_app_id);
  scoped_refptr<extensions::Extension> oem_app = MakeApp(
      kOemAppName, oem_app_id, extensions::Extension::WAS_INSTALLED_BY_OEM);
  InstallExtension(oem_app.get());

  ChromeAppListItem* oem_folder = model_updater->FindItem(ash::kOemFolderId);
  ASSERT_NE(nullptr, oem_folder);
  EXPECT_EQ(oem_folder->folder_id(), "");

  EXPECT_TRUE(oem_folder->position().GreaterThan(web_store_item->position()));
  EXPECT_TRUE(oem_folder->position().LessThan(test_app_item->position()));
}

// Verifies that OEM item preserves parent and doesn't change parent in case
// sync change says this.
TEST_F(AppListSyncableServiceTest, OEMItemIgnoreSyncParent) {
  const std::string oem_app_id = CreateNextAppId(extensions::kWebStoreAppId);
  scoped_refptr<extensions::Extension> oem_app = MakeApp(
      kOemAppName, oem_app_id, extensions::Extension::WAS_INSTALLED_BY_OEM);
  InstallExtension(oem_app.get());

  // OEM item is not top level element.
  AppListModelUpdater* model_updater = GetModelUpdater();
  ChromeAppListItem* oem_app_item = model_updater->FindItem(oem_app_id);
  ASSERT_TRUE(oem_app_item);
  EXPECT_EQ(ash::kOemFolderId, oem_app_item->folder_id());

  // Send sync that OEM app is top-level item.
  syncer::SyncDataList sync_list;
  sync_list.push_back(CreateAppRemoteData(
      oem_app_id, kOemAppName, std::string() /* parent_id */,
      oem_app_item->position().ToInternalValue(),
      std::string() /* item_pin_ordinal */));
  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  // Parent folder is not changed.
  EXPECT_EQ(ash::kOemFolderId, oem_app_item->folder_id());
}

TEST_F(AppListSyncableServiceTest, OEMAppParentNotOverridenInSync) {
  const std::string oem_app_id = CreateNextAppId(extensions::kWebStoreAppId);
  scoped_refptr<extensions::Extension> oem_app = MakeApp(
      kOemAppName, oem_app_id, extensions::Extension::WAS_INSTALLED_BY_OEM);
  const std::string oem_app_parent_in_sync = "nonoemfolder";

  // Send sync where the OEM app is parented by another folder.
  syncer::SyncDataList sync_list;
  sync_list.push_back(CreateAppRemoteData(
      oem_app_parent_in_sync, "Non OEM folder", std::string() /* parent_id */,
      syncer::StringOrdinal("nonoemfolderposition").ToInternalValue(),
      std::string() /* item_pin_ordinal*/,
      sync_pb::AppListSpecifics_AppListItemType_TYPE_FOLDER));
  sync_list.push_back(CreateAppRemoteData(
      oem_app_id, kOemAppName, oem_app_parent_in_sync,
      syncer::StringOrdinal("appposition").ToInternalValue(),
      std::string() /* item_pin_ordinal */));
  // Add an extra app to the folder to avoid invalid single item folder.
  sync_list.push_back(CreateAppRemoteData(
      "non-oem-app", "Non OEM app", oem_app_parent_in_sync,
      syncer::StringOrdinal("nonoemappposition").ToInternalValue(),
      std::string() /* item_pin_ordinal */));
  sync_list.push_back(CreateAppRemoteData(
      ash::kOemFolderId, "OEM", std::string() /*parent_id*/,
      syncer::StringOrdinal("oemposition").ToInternalValue(),
      std::string() /* item_pin_ordinal*/,
      sync_pb::AppListSpecifics_AppListItemType_TYPE_FOLDER));
  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  InstallExtension(oem_app.get());

  // The OEM app should be parented by the OEM folder locally.
  AppListModelUpdater* model_updater = GetModelUpdater();
  ChromeAppListItem* oem_app_item = model_updater->FindItem(oem_app_id);
  ASSERT_TRUE(oem_app_item);
  EXPECT_EQ(ash::kOemFolderId, oem_app_item->folder_id());

  ChromeAppListItem* oem_folder_item =
      model_updater->FindItem(ash::kOemFolderId);
  ASSERT_TRUE(oem_folder_item);
  EXPECT_EQ(oem_folder_item->position(), syncer::StringOrdinal("oemposition"));

  // Verify that the OEM parent has no changed in sync.
  const app_list::AppListSyncableService::SyncItem* app_sync_item =
      GetSyncItem(oem_app_id);
  ASSERT_TRUE(app_sync_item);
  EXPECT_EQ(oem_app_parent_in_sync, app_sync_item->parent_id);

  // Verify that the non OEM folder is not removed from sync, even though it's
  // not been created locally.
  EXPECT_FALSE(model_updater->FindItem(oem_app_parent_in_sync));
  EXPECT_TRUE(GetSyncItem(oem_app_parent_in_sync));
}

// Verifies that OEM folder position respects the OEM folder position in sync.
TEST_F(AppListSyncableServiceTest, OEMFolderPositionSync) {
  const std::string oem_app_id = CreateNextAppId(extensions::kWebStoreAppId);
  scoped_refptr<extensions::Extension> oem_app = MakeApp(
      kOemAppName, oem_app_id, extensions::Extension::WAS_INSTALLED_BY_OEM);

  // Send sync with an OEM folder item.
  syncer::SyncDataList sync_list;
  sync_list.push_back(CreateAppRemoteData(
      oem_app_id, kOemAppName, std::string() /* parent_id */,
      syncer::StringOrdinal("appposition").ToInternalValue(),
      std::string() /* item_pin_ordinal */));
  sync_list.push_back(CreateAppRemoteData(
      ash::kOemFolderId, "OEM", std::string() /*parent_id*/,
      syncer::StringOrdinal("oemposition").ToInternalValue(),
      std::string() /* item_pin_ordinal*/,
      sync_pb::AppListSpecifics_AppListItemType_TYPE_FOLDER));
  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  InstallExtension(oem_app.get());

  // OEM app should locally be parented by the OEM folder.
  AppListModelUpdater* model_updater = GetModelUpdater();
  ChromeAppListItem* oem_app_item = model_updater->FindItem(oem_app_id);
  ASSERT_TRUE(oem_app_item);
  EXPECT_EQ(ash::kOemFolderId, oem_app_item->folder_id());

  ChromeAppListItem* oem_folder_item =
      model_updater->FindItem(ash::kOemFolderId);
  ASSERT_TRUE(oem_folder_item);
  // The OEM folder folder should be set to the value set by sync.
  EXPECT_EQ(oem_folder_item->position(), syncer::StringOrdinal("oemposition"));
}

// Verifies that non-OEM item is not moved to OEM folder by sync.
TEST_F(AppListSyncableServiceTest, NonOEMItemIgnoreSyncToOEMFolder) {
  const std::string app_id = CreateNextAppId(extensions::kWebStoreAppId);
  scoped_refptr<extensions::Extension> app = MakeApp(
      kSomeAppName, app_id, extensions::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(app.get());

  ChromeAppListItem* app_item = GetModelUpdater()->FindItem(app_id);
  ASSERT_TRUE(app_item);
  // It is in the top list.
  EXPECT_EQ(std::string(), app_item->folder_id());

  // Send sync that this app is in OEM folder.
  syncer::SyncDataList sync_list;
  sync_list.push_back(
      CreateAppRemoteData(app_id, kSomeAppName, ash::kOemFolderId,
                          app_item->position().ToInternalValue(),
                          std::string() /* item_pin_ordinal */));
  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  // Parent folder is not changed.
  EXPECT_EQ(std::string(), app_item->folder_id());
}

TEST_F(AppListSyncableServiceTest, InitialMerge) {
  const std::string kItemId1 = GenerateId("item_id1");
  const std::string kItemId2 = GenerateId("item_id2");

  syncer::SyncDataList sync_list;
  sync_list.push_back(CreateAppRemoteData(kItemId1, "item_name1",
                                          GenerateId("parent_id1"), "ordinal",
                                          "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(kItemId2, "item_name2",
                                          GenerateId("parent_id2"), "ordinal",
                                          "pinordinal"));

  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  ASSERT_TRUE(GetSyncItem(kItemId1));
  EXPECT_EQ("item_name1", GetSyncItem(kItemId1)->item_name);
  EXPECT_EQ(GenerateId("parent_id1"), GetSyncItem(kItemId1)->parent_id);
  EXPECT_EQ("ordinal", GetSyncItem(kItemId1)->item_ordinal.ToDebugString());
  EXPECT_EQ("pinordinal",
            GetSyncItem(kItemId1)->item_pin_ordinal.ToDebugString());

  ASSERT_TRUE(GetSyncItem(kItemId2));
  EXPECT_EQ("item_name2", GetSyncItem(kItemId2)->item_name);
  EXPECT_EQ(GenerateId("parent_id2"), GetSyncItem(kItemId2)->parent_id);
  EXPECT_EQ("ordinal", GetSyncItem(kItemId2)->item_ordinal.ToDebugString());
  EXPECT_EQ("pinordinal",
            GetSyncItem(kItemId2)->item_pin_ordinal.ToDebugString());

  // Default page breaks are not installed for non-first time users that don't
  // have them in their sync.
  EXPECT_FALSE(GetSyncItem(app_list::kDefaultPageBreak1));
}

class AppListInternalAppSyncableServiceTest
    : public AppListSyncableServiceTest {
 public:
  AppListInternalAppSyncableServiceTest() {
    chrome::SettingsWindowManager::ForceDeprecatedSettingsWindowForTesting();
  }

  void SetUp() override {
    AppListSyncableServiceTest::SetUp();
    web_app::test::InstallDummyWebApp(testing_profile(), kOsSettingsUrl,
                                      GURL(kOsSettingsUrl));
  }

  ~AppListInternalAppSyncableServiceTest() override = default;
};

TEST_F(AppListInternalAppSyncableServiceTest, ExistingDefaultPageBreak) {
  // Non-first time users have items in their remote sync data.
  syncer::SyncDataList sync_list;
  sync_list.push_back(CreateAppRemoteData(
      app_list::kDefaultPageBreak1, "page_break_1", "", "ordinal", "pinordinal",
      sync_pb::AppListSpecifics_AppListItemType_TYPE_PAGE_BREAK));

  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  // Existing page break item in remote sync will be added, and its data will be
  // updated with the item's remote sync data.
  auto* page_break_sync_item = GetSyncItem(app_list::kDefaultPageBreak1);
  ASSERT_TRUE(page_break_sync_item);
  EXPECT_EQ(page_break_sync_item->item_type,
            sync_pb::AppListSpecifics::TYPE_PAGE_BREAK);
  EXPECT_EQ("page_break_1", page_break_sync_item->item_name);
  EXPECT_EQ("", page_break_sync_item->parent_id);
  EXPECT_EQ("ordinal", page_break_sync_item->item_ordinal.ToDebugString());
  EXPECT_EQ("pinordinal",
            page_break_sync_item->item_pin_ordinal.ToDebugString());
}

TEST_F(AppListInternalAppSyncableServiceTest, DefaultPageBreakFirstTimeUser) {
  // Empty sync list simulates a first time user.
  syncer::SyncDataList sync_list;

  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  auto* page_break_sync_item = GetSyncItem(app_list::kDefaultPageBreak1);
  ASSERT_TRUE(page_break_sync_item);
  EXPECT_EQ(page_break_sync_item->item_type,
            sync_pb::AppListSpecifics::TYPE_PAGE_BREAK);

  // Since internal apps are added by default, we'll use the settings apps to
  // test the ordering.
  auto* settings_app_sync_item = GetSyncItem(web_app::kOsSettingsAppId);
  auto* hosted_app_sync_item = GetSyncItem(kHostedAppId);
  ASSERT_TRUE(settings_app_sync_item);
  ASSERT_TRUE(hosted_app_sync_item);

  // The default page break should be between the hosted app, and the settings
  // app; i.e. the hosted app is in the first page, and the settings app is in
  // the second page.
  EXPECT_TRUE(page_break_sync_item->item_ordinal.LessThan(
      settings_app_sync_item->item_ordinal));
  EXPECT_TRUE(page_break_sync_item->item_ordinal.GreaterThan(
      hosted_app_sync_item->item_ordinal));
}

TEST_F(AppListSyncableServiceTest, InitialMerge_BadData) {
  const syncer::SyncDataList sync_list = CreateBadAppRemoteData(kDefault);

  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  // Invalid item_ordinal and item_pin_ordinal.
  // Invalid item_ordinal is fixed up.
  ASSERT_TRUE(GetSyncItem(kInvalidOrdinalsId()));
  EXPECT_EQ("n",
            GetSyncItem(kInvalidOrdinalsId())->item_ordinal.ToDebugString());
  EXPECT_EQ(
      "INVALID[$$invalid_ordinal$$]",
      GetSyncItem(kInvalidOrdinalsId())->item_pin_ordinal.ToDebugString());

  // Empty item name.
  ASSERT_TRUE(GetSyncItem(kEmptyItemNameId()));
  EXPECT_EQ("", GetSyncItem(kEmptyItemNameId())->item_name);
  EXPECT_TRUE(GetSyncItem(kEmptyItemNameUnsetId()));
  EXPECT_EQ("", GetSyncItem(kEmptyItemNameUnsetId())->item_name);

  // Empty parent ID.
  ASSERT_TRUE(GetSyncItem(kEmptyParentId()));
  EXPECT_EQ("", GetSyncItem(kEmptyParentId())->parent_id);
  EXPECT_TRUE(GetSyncItem(kEmptyParentUnsetId()));
  EXPECT_EQ("", GetSyncItem(kEmptyParentUnsetId())->parent_id);

  // Empty item_ordinal and item_pin_ordinal.
  // Empty item_ordinal is fixed up.
  ASSERT_TRUE(GetSyncItem(kEmptyOrdinalsId()));
  EXPECT_EQ("n", GetSyncItem(kEmptyOrdinalsId())->item_ordinal.ToDebugString());
  EXPECT_EQ("INVALID[]",
            GetSyncItem(kEmptyOrdinalsId())->item_pin_ordinal.ToDebugString());
  ASSERT_TRUE(GetSyncItem(kEmptyOrdinalsUnsetId()));
  EXPECT_EQ("n",
            GetSyncItem(kEmptyOrdinalsUnsetId())->item_ordinal.ToDebugString());
  EXPECT_EQ(
      "INVALID[]",
      GetSyncItem(kEmptyOrdinalsUnsetId())->item_pin_ordinal.ToDebugString());

  // Duplicate item_id overrides previous.
  ASSERT_TRUE(GetSyncItem(kDupeItemId()));
  EXPECT_EQ("item_name_dupe", GetSyncItem(kDupeItemId())->item_name);
}

TEST_F(AppListSyncableServiceTest, InitialMergeAndUpdate) {
  const std::string kItemId1 = GenerateId("item_id1");
  const std::string kItemId2 = GenerateId("item_id2");

  syncer::SyncDataList sync_list;
  sync_list.push_back(CreateAppRemoteData(kItemId1, "item_name1", kParentId(),
                                          "ordinal", "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(kItemId2, "item_name2", kParentId(),
                                          "ordinal", "pinordinal"));

  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  ASSERT_TRUE(GetSyncItem(kItemId1));
  ASSERT_TRUE(GetSyncItem(kItemId2));

  syncer::SyncChangeList change_list;
  change_list.push_back(syncer::SyncChange(
      FROM_HERE, syncer::SyncChange::ACTION_UPDATE,
      CreateAppRemoteData(kItemId1, "item_name1x", GenerateId("parent_id1x"),
                          "ordinalx", "pinordinalx")));
  change_list.push_back(syncer::SyncChange(
      FROM_HERE, syncer::SyncChange::ACTION_UPDATE,
      CreateAppRemoteData(kItemId2, "item_name2x", GenerateId("parent_id2x"),
                          "ordinalx", "pinordinalx")));

  app_list_syncable_service()->ProcessSyncChanges(base::Location(),
                                                  change_list);
  content::RunAllTasksUntilIdle();

  ASSERT_TRUE(GetSyncItem(kItemId1));
  EXPECT_EQ("item_name1x", GetSyncItem(kItemId1)->item_name);
  EXPECT_EQ(GenerateId("parent_id1x"), GetSyncItem(kItemId1)->parent_id);
  EXPECT_EQ("ordinalx", GetSyncItem(kItemId1)->item_ordinal.ToDebugString());
  EXPECT_EQ("pinordinalx",
            GetSyncItem(kItemId1)->item_pin_ordinal.ToDebugString());

  ASSERT_TRUE(GetSyncItem(kItemId2));
  EXPECT_EQ("item_name2x", GetSyncItem(kItemId2)->item_name);
  EXPECT_EQ(GenerateId("parent_id2x"), GetSyncItem(kItemId2)->parent_id);
  EXPECT_EQ("ordinalx", GetSyncItem(kItemId2)->item_ordinal.ToDebugString());
  EXPECT_EQ("pinordinalx",
            GetSyncItem(kItemId2)->item_pin_ordinal.ToDebugString());
}

TEST_F(AppListSyncableServiceTest, InitialMergeAndUpdate_BadData) {
  const std::string kItemId = GenerateId("item_id");

  syncer::SyncDataList sync_list;
  sync_list.push_back(CreateAppRemoteData(kItemId, "item_name", kParentId(),
                                          "ordinal", "pinordinal"));

  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  ASSERT_TRUE(GetSyncItem(kItemId));

  syncer::SyncChangeList change_list;
  const syncer::SyncDataList update_list = CreateBadAppRemoteData(kItemId);
  for (syncer::SyncDataList::const_iterator iter = update_list.begin();
       iter != update_list.end(); ++iter) {
    change_list.push_back(syncer::SyncChange(
        FROM_HERE, syncer::SyncChange::ACTION_UPDATE, *iter));
  }

  // Validate items with bad data are processed without crashing.
  app_list_syncable_service()->ProcessSyncChanges(base::Location(),
                                                  change_list);
  content::RunAllTasksUntilIdle();

  ASSERT_TRUE(GetSyncItem(kItemId));
}

TEST_F(AppListSyncableServiceTest, PruneEmptySyncFolder) {
  // Add a folder item and two items that are parented to the folder item.
  const std::string kFolderItemId = GenerateId("folder_item_id");
  const std::string kItemId1 = GenerateId("item_id_1");
  const std::string kItemId2 = GenerateId("item_id_2");

  syncer::SyncDataList sync_list;
  sync_list.push_back(CreateAppRemoteData(
      kFolderItemId, "folder_item_name", kParentId(), "ordinal", "pinordinal",
      sync_pb::AppListSpecifics_AppListItemType_TYPE_FOLDER));
  sync_list.push_back(CreateAppRemoteData(kItemId1, "item1_name", kFolderItemId,
                                          "ordinal1", "pinordinal1"));
  sync_list.push_back(CreateAppRemoteData(kItemId2, "item2_name", kFolderItemId,
                                          "ordinal2", "pinordinal2"));

  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  ASSERT_TRUE(GetSyncItem(kFolderItemId));
  ASSERT_TRUE(GetSyncItem(kItemId1));
  ASSERT_TRUE(GetSyncItem(kItemId2));

  // Remove one of the child item, the folder still has one item in it.
  app_list_syncable_service()->RemoveItem(kItemId1);
  content::RunAllTasksUntilIdle();

  ASSERT_TRUE(GetSyncItem(kFolderItemId));
  ASSERT_FALSE(GetSyncItem(kItemId1));
  ASSERT_TRUE(GetSyncItem(kItemId2));

  // Remove the other child item, the empty folder should be removed as well.
  app_list_syncable_service()->RemoveItem(kItemId2);
  content::RunAllTasksUntilIdle();

  ASSERT_FALSE(GetSyncItem(kFolderItemId));
  ASSERT_FALSE(GetSyncItem(kItemId2));
}

TEST_F(AppListSyncableServiceTest,
       CleanUpSingleItemSyncFolderAfterInitialMerge) {
  syncer::SyncDataList sync_list;
  // Add a top level item.
  const std::string kTopItem = GenerateId("top_item_id");
  sync_list.push_back(CreateAppRemoteData(
      kTopItem, "top_item_name", "", GetLastPositionString(), "pinordinal"));

  // Add a single app folder item with only one child app item in it.
  const std::string kFolderId1 = GenerateId("folder_id_1");
  const std::string kChildItemId1 = GenerateId("child_item_id_1");
  sync_list.push_back(CreateAppRemoteData(
      kFolderId1, "folder_1_name", "", GetLastPositionString(), "pinordinal",
      sync_pb::AppListSpecifics_AppListItemType_TYPE_FOLDER));
  sync_list.push_back(CreateAppRemoteData(kChildItemId1, "child_item_1_name",
                                          kFolderId1, GetLastPositionString(),
                                          "pinordinal"));

  // Add a regular folder with two app items in it.
  const std::string kFolderId2 = GenerateId("folder_id_2");
  const std::string kChildItemId2 = GenerateId("child_item_id_2");
  const std::string kChildItemId3 = GenerateId("child_item_id_3");
  sync_list.push_back(CreateAppRemoteData(
      kFolderId2, "folder_2_name", "", GetLastPositionString(), "pinordinal",
      sync_pb::AppListSpecifics_AppListItemType_TYPE_FOLDER));
  sync_list.push_back(CreateAppRemoteData(kChildItemId2, "child_item_2_name",
                                          kFolderId2, GetLastPositionString(),
                                          "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(kChildItemId3, "child_item_3_name",
                                          kFolderId2, GetLastPositionString(),
                                          "pinordinal"));

  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  // After the initial sync data is merged, the single item folder is expected
  // to be cleaned up.
  ASSERT_FALSE(GetSyncItem(kFolderId1));
  // The child item in the folder should be moved to the top level.
  ASSERT_TRUE(GetSyncItem(kChildItemId1));
  EXPECT_EQ("", GetSyncItem(kChildItemId1)->parent_id);
  EXPECT_TRUE(
      GetSyncItem(kChildItemId1)
          ->item_ordinal.GreaterThan(GetSyncItem(kTopItem)->item_ordinal));

  // Sync items should be created for regular folder.
  ASSERT_TRUE(GetSyncItem(kFolderId2));
  ASSERT_TRUE(GetSyncItem(kChildItemId2));
  EXPECT_EQ(kFolderId2, GetSyncItem(kChildItemId2)->parent_id);
  ASSERT_TRUE(GetSyncItem(kChildItemId3));
  EXPECT_EQ(kFolderId2, GetSyncItem(kChildItemId3)->parent_id);
  EXPECT_TRUE(
      GetSyncItem(kChildItemId1)
          ->item_ordinal.LessThan(GetSyncItem(kFolderId2)->item_ordinal));
}

// Simulates and verifies the fix of the single item folder issue of
// crbug.com/1082530. Here is the repro of the bug.
// When user signs in on a new device for the first time, a folder contains two
// app items, one is installed before another. After the first app is installed,
// user sees a single item folder with the first app. User moves the app out of
// the folder, the folder disappears from UI. Later, when the second app is
// installed, it will show up in a folder with only the second app in it.
// The fix will remove the second app from the folder after user removes the
// first app. Later, when the second app is installed, it will show at the top
// level.
TEST_F(AppListSyncableServiceTest, UpdateSyncItemRemoveLastItemFromFolder) {
  // Create two apps associated with a folder for sync data.
  const std::string kFolderId = GenerateId("folder_id");
  const std::string kChildItemId1 = extensions::kWebStoreAppId;
  const std::string kChildItemId2 = CreateNextAppId(extensions::kWebStoreAppId);
  syncer::SyncDataList sync_list;
  sync_list.push_back(CreateAppRemoteData(
      kFolderId, "folder_name", "", GetLastPositionString(), "pinordinal",
      sync_pb::AppListSpecifics_AppListItemType_TYPE_FOLDER));
  sync_list.push_back(CreateAppRemoteData(kChildItemId1, "child_item_1_name",
                                          kFolderId, GetLastPositionString(),
                                          "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(kChildItemId2, "child_item_2_name",
                                          kFolderId, GetLastPositionString(),
                                          "pinordinal"));

  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  ASSERT_TRUE(GetSyncItem(kFolderId));
  ASSERT_TRUE(GetSyncItem(kChildItemId1));
  ASSERT_TRUE(GetSyncItem(kChildItemId2));
  EXPECT_EQ(kFolderId, GetSyncItem(kChildItemId1)->parent_id);
  EXPECT_EQ(kFolderId, GetSyncItem(kChildItemId2)->parent_id);

  // Install the first child app.
  scoped_refptr<extensions::Extension> child_app_1 =
      MakeApp("child_item_1_name", kChildItemId1,
              extensions::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(child_app_1.get());

  // Verify the first child app is created in the model updater.
  // The second app is not in the model updater since it is not installed yet.
  AppListModelUpdater* model_updater = GetModelUpdater();
  ChromeAppListItem* child_item_1 = model_updater->FindItem(kChildItemId1);
  ASSERT_TRUE(child_item_1);
  EXPECT_EQ(kFolderId, child_item_1->folder_id());
  ASSERT_FALSE(model_updater->FindItem(kChildItemId2));

  // Move the child_item_1 out of the folder.
  model_updater->SetItemFolderId(child_item_1->id(), "");

  // Verify both child item are moved out of the folder.
  ASSERT_TRUE(GetSyncItem(kChildItemId1));
  EXPECT_EQ("", GetSyncItem(kChildItemId1)->parent_id);
  ASSERT_TRUE(GetSyncItem(kChildItemId2));
  EXPECT_EQ("", GetSyncItem(kChildItemId2)->parent_id);
  EXPECT_EQ("", model_updater->FindItem(kChildItemId1)->folder_id());

  // Install the second child app.
  scoped_refptr<extensions::Extension> child_app_2 =
      MakeApp("child_item_2_name", kChildItemId2,
              extensions::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(child_app_2.get());

  // Verify the second app item is created in the model updater,
  // and it is not in any folder.
  ChromeAppListItem* child_item_2 = model_updater->FindItem(kChildItemId2);
  ASSERT_TRUE(child_item_2);
  EXPECT_EQ("", child_item_2->folder_id());
}

TEST_F(AppListSyncableServiceTest, AddPageBreakItems) {
  RemoveAllExistingItems();

  // Populate item list with 2 items.
  const std::string kItemId1 = GenerateId("item_id1");
  const std::string kItemId2 = GenerateId("item_id2");

  syncer::SyncDataList sync_list;
  sync_list.push_back(CreateAppRemoteData(kItemId1, "item_name",
                                          "" /* parent_id */, "c" /* ordinal */,
                                          "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(kItemId2, "item_name",
                                          "" /* parent_id */, "d" /* ordinal */,
                                          "pinordinal"));

  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  ASSERT_TRUE(GetSyncItem(kItemId1));
  ASSERT_TRUE(GetSyncItem(kItemId2));

  // Add a "page break" items before 1st item, after 1st item and after 2nd
  // item.
  const std::string kPageBreakItemId1 = GenerateId("page_break_item_id1");
  const std::string kPageBreakItemId2 = GenerateId("page_break_item_id2");
  const std::string kPageBreakItemId3 = GenerateId("page_break_item_id3");
  AppListModelUpdater* model_updater = GetModelUpdater();
  std::unique_ptr<ChromeAppListItem> page_break_item1 =
      std::make_unique<ChromeAppListItem>(profile_.get(), kPageBreakItemId1,
                                          model_updater);
  std::unique_ptr<ChromeAppListItem> page_break_item2 =
      std::make_unique<ChromeAppListItem>(profile_.get(), kPageBreakItemId2,
                                          model_updater);
  std::unique_ptr<ChromeAppListItem> page_break_item3 =
      std::make_unique<ChromeAppListItem>(profile_.get(), kPageBreakItemId3,
                                          model_updater);
  ItemTestApi(page_break_item1.get()).SetPosition(syncer::StringOrdinal("bm"));
  page_break_item1->SetIsPageBreak(true);
  ItemTestApi(page_break_item2.get()).SetPosition(syncer::StringOrdinal("cm"));
  page_break_item2->SetIsPageBreak(true);
  ItemTestApi(page_break_item3.get()).SetPosition(syncer::StringOrdinal("dm"));
  page_break_item3->SetIsPageBreak(true);
  app_list_syncable_service()->AddItem(std::move(page_break_item1));
  app_list_syncable_service()->AddItem(std::move(page_break_item2));
  app_list_syncable_service()->AddItem(std::move(page_break_item3));
  content::RunAllTasksUntilIdle();

  // Only 2nd "page break" item remains.
  ASSERT_FALSE(GetSyncItem(kPageBreakItemId1));
  ASSERT_TRUE(GetSyncItem(kItemId1));
  ASSERT_TRUE(GetSyncItem(kPageBreakItemId2));
  ASSERT_TRUE(GetSyncItem(kItemId2));
  ASSERT_FALSE(GetSyncItem(kPageBreakItemId3));
}

TEST_F(AppListSyncableServiceTest, PruneRedundantPageBreakItems) {
  RemoveAllExistingItems();

  // Populate item list with items and leading, trailing and duplicate "page
  // break" items.
  const std::string kPageBreakItemId1 = GenerateId("page_break_item_id1");
  const std::string kItemId1 = GenerateId("item_id1");
  const std::string kFolderItemId = GenerateId("folder_item_id");
  const std::string kPageBreakItemId2 = GenerateId("page_break_item_id2");
  const std::string kItemInFolderId = GenerateId("item_in_folder_id");
  const std::string kPageBreakItemId3 = GenerateId("page_break_item_id3");
  const std::string kPageBreakItemId4 = GenerateId("page_break_item_id4");
  const std::string kItemId2 = GenerateId("item_id2");
  const std::string kPageBreakItemId5 = GenerateId("page_break_item_id5");

  syncer::SyncDataList sync_list;
  sync_list.push_back(CreateAppRemoteData(
      kPageBreakItemId1, "page_break_item_name", "" /* parent_id */,
      "b" /* ordinal */, "pinordinal",
      sync_pb::AppListSpecifics_AppListItemType_TYPE_PAGE_BREAK));
  sync_list.push_back(CreateAppRemoteData(kItemId1, "item_name",
                                          "" /* parent_id */, "c" /* ordinal */,
                                          "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(kFolderItemId, "folder_item_name",
                                          "" /* parent_id */, "d" /* ordinal */,
                                          "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(
      kPageBreakItemId2, "page_break_item_name", "" /* parent_id */,
      "e" /* ordinal */, "pinordinal",
      sync_pb::AppListSpecifics_AppListItemType_TYPE_PAGE_BREAK));
  sync_list.push_back(CreateAppRemoteData(
      kItemInFolderId, "item_in_folder_name", kFolderItemId /* parent_id */,
      "f" /* ordinal */, "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(
      kPageBreakItemId3, "page_break_item_name", "" /* parent_id */,
      "g" /* ordinal */, "pinordinal",
      sync_pb::AppListSpecifics_AppListItemType_TYPE_PAGE_BREAK));
  sync_list.push_back(CreateAppRemoteData(
      kPageBreakItemId4, "page_break_item_name", "" /* parent_id */,
      "h" /* ordinal */, "pinordinal",
      sync_pb::AppListSpecifics_AppListItemType_TYPE_PAGE_BREAK));
  sync_list.push_back(CreateAppRemoteData(kItemId2, "item_name",
                                          "" /* parent_id */, "i" /* ordinal */,
                                          "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(
      kPageBreakItemId5, "page_break_item_name", "" /* parent_id */,
      "j" /* ordinal */, "pinordinal",
      sync_pb::AppListSpecifics_AppListItemType_TYPE_PAGE_BREAK));

  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  ASSERT_TRUE(GetSyncItem(kPageBreakItemId1));
  ASSERT_TRUE(GetSyncItem(kItemId1));
  ASSERT_TRUE(GetSyncItem(kFolderItemId));
  ASSERT_TRUE(GetSyncItem(kPageBreakItemId2));
  ASSERT_TRUE(GetSyncItem(kItemInFolderId));
  ASSERT_TRUE(GetSyncItem(kPageBreakItemId3));
  ASSERT_TRUE(GetSyncItem(kPageBreakItemId4));
  ASSERT_TRUE(GetSyncItem(kItemId2));
  ASSERT_TRUE(GetSyncItem(kPageBreakItemId5));

  // Remove a item, which triggers removing redundant "page break" items.
  app_list_syncable_service()->RemoveItem(kItemId1);
  content::RunAllTasksUntilIdle();

  ASSERT_FALSE(GetSyncItem(kPageBreakItemId1));
  ASSERT_FALSE(GetSyncItem(kItemId1));
  ASSERT_TRUE(GetSyncItem(kFolderItemId));
  ASSERT_TRUE(GetSyncItem(kPageBreakItemId2));
  ASSERT_TRUE(GetSyncItem(kItemInFolderId));
  ASSERT_FALSE(GetSyncItem(kPageBreakItemId3));
  ASSERT_FALSE(GetSyncItem(kPageBreakItemId4));
  ASSERT_TRUE(GetSyncItem(kItemId2));
  ASSERT_FALSE(GetSyncItem(kPageBreakItemId5));
}

// This test simulates the following overflow case. Assume the maximum items
// on each page is three. Both device 1 and device 2 have the identical apps,
// the apps layout looks like the following.
// page 1: A1, A2 [page break]
// page 2: B1, B2, B3 [page break]
// page 3: C1 [page break]
// On device 1, move A1 from page 1 to page 2 and insert between B1 and B2.
// After the move, the apps layout should look like the following
// page 1: A2 [page break]
// page 2: B1, A1, B2 [page break]
// page 3: B3, C1 [page break]
// Notice that B3 is overflowed from page 2 to page 3 and placed before C1.
// After the changes are synced to device 2, it should have the same app layout
// as shown on device 1.
// This test simulates that device 2 gets the sync changes from device 1, and
// applies the changes in model updater and the apps should have the same layout
// as the ones on the device 1. It verifies the fix for the repro issue
// described in http://crbug.com/938098#c15.
TEST_F(AppListSyncableServiceTest, PageBreakWithOverflowItem) {
  RemoveAllExistingItems();

  // Create 2 apps on the page 1.
  syncer::SyncDataList sync_list;
  const std::string kItemIdA1 = extensions::kWebStoreAppId;
  const std::string kItemIdA2 = CreateNextAppId(extensions::kWebStoreAppId);
  const std::string kPageBreakItemId1 = GenerateId("page_break_item_id1");
  sync_list.push_back(CreateAppRemoteData(
      kItemIdA1, "A1", "", GetLastPositionString(), "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(
      kItemIdA2, "A2", "", GetLastPositionString(), "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(
      kPageBreakItemId1, "page_break_item1_name", "" /* parent_id */,
      GetLastPositionString(), "pinordinal",
      sync_pb::AppListSpecifics_AppListItemType_TYPE_PAGE_BREAK));

  // Create 3 apps on the page 2, which makes it a full page.
  const std::string kItemIdB1 = CreateNextAppId(kItemIdA2);
  const std::string kItemIdB2 = CreateNextAppId(kItemIdB1);
  const std::string kItemIdB3 = CreateNextAppId(kItemIdB2);
  const std::string kPageBreakItemId2 = GenerateId("page_break_item_id2");
  sync_list.push_back(CreateAppRemoteData(
      kItemIdB1, "B1", "", GetLastPositionString(), "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(
      kItemIdB2, "B2", "", GetLastPositionString(), "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(
      kItemIdB3, "B3", "", GetLastPositionString(), "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(
      kPageBreakItemId2, "page_break_item2_name", "" /* parent_id */,
      GetLastPositionString(), "pinordinal",
      sync_pb::AppListSpecifics_AppListItemType_TYPE_PAGE_BREAK));

  // Create 1 app on page 3.
  const std::string kItemIdC1 = CreateNextAppId(kItemIdB3);
  const std::string kPageBreakItemId3 = GenerateId("page_break_item_id3");
  sync_list.push_back(CreateAppRemoteData(
      kItemIdC1, "C1", "", GetLastPositionString(), "pinordinal"));
  sync_list.push_back(CreateAppRemoteData(
      kPageBreakItemId3, "page_break_item3_name", "" /* parent_id */,
      GetLastPositionString(), "pinordinal",
      sync_pb::AppListSpecifics_AppListItemType_TYPE_PAGE_BREAK));

  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  // Verify the sync items on page 1.
  ASSERT_TRUE(GetSyncItem(kItemIdA1));
  ASSERT_TRUE(GetSyncItem(kItemIdA2));
  ASSERT_TRUE(GetSyncItem(kPageBreakItemId1));

  // Verify the sync items on page 2.
  ASSERT_TRUE(GetSyncItem(kItemIdB1));
  ASSERT_TRUE(GetSyncItem(kItemIdB2));
  ASSERT_TRUE(GetSyncItem(kItemIdB3));
  ASSERT_TRUE(GetSyncItem(kPageBreakItemId2));

  // Verify the sync items on page 3.
  ASSERT_TRUE(GetSyncItem(kItemIdC1));
  ASSERT_TRUE(GetSyncItem(kPageBreakItemId3));

  // Install all the apps.
  scoped_refptr<extensions::Extension> app_A1 =
      MakeApp("app_A1_name", kItemIdA1,
              extensions::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(app_A1.get());
  scoped_refptr<extensions::Extension> app_A2 =
      MakeApp("app_A2_name", kItemIdA2,
              extensions::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(app_A2.get());
  scoped_refptr<extensions::Extension> app_B1 =
      MakeApp("app_B1_name", kItemIdB1,
              extensions::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(app_B1.get());
  scoped_refptr<extensions::Extension> app_B2 =
      MakeApp("app_B2_name", kItemIdB2,
              extensions::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(app_B2.get());
  scoped_refptr<extensions::Extension> app_B3 =
      MakeApp("app_B3_name", kItemIdB3,
              extensions::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(app_B3.get());
  scoped_refptr<extensions::Extension> app_C1 =
      MakeApp("app_C1_name", kItemIdC1,
              extensions::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(app_C1.get());

  // Verify all apps and page breaks are created and the items
  // in the model updater should look like:
  // A1 A2 [page break 1]
  // B1 B2 B3 [page break 2]
  // C1 [page break 3]
  auto ordered_items = GetOrderedItemIdsFromModelUpdater();
  EXPECT_THAT(
      ordered_items,
      ElementsAre(kItemIdA1, kItemIdA2, kPageBreakItemId1, kItemIdB1, kItemIdB2,
                  kItemIdB3, kPageBreakItemId2, kItemIdC1, kPageBreakItemId3));

  // On device 1, move A1 from page 1 to page 2 and insert it between B1 and B2.
  // Device 2 should get the following 3 sync changes from device 1:
  //    1. Remove the previous page break after B3.
  //    2. Add a new page break after B2.
  //    3. Update A1 for position change to move it between B1 and B2.
  syncer::SyncChangeList change_list;
  // Sync change for removing the previous page break after B3.
  AppListModelUpdater* model_updater = GetModelUpdater();
  ChromeAppListItem* app_item_B1 = model_updater->FindItem(kItemIdB1);
  ChromeAppListItem* pagebreak_2 = model_updater->FindItem(kPageBreakItemId2);
  ChromeAppListItem* app_item_B2 = model_updater->FindItem(kItemIdB2);
  ChromeAppListItem* app_item_B3 = model_updater->FindItem(kItemIdB3);
  change_list.push_back(syncer::SyncChange(
      FROM_HERE, syncer::SyncChange::ACTION_DELETE,
      CreateAppRemoteData(
          kPageBreakItemId2, "page_break_item2_name", "" /* parent_id */,
          pagebreak_2->position().ToDebugString(), "pinordinal")));
  // Sync change for adding a new page break after B2.
  const std::string kNewPageBreakItemId = GenerateId("new_page_break_item_id");
  change_list.push_back(syncer::SyncChange(
      FROM_HERE, syncer::SyncChange::ACTION_ADD,
      CreateAppRemoteData(
          kNewPageBreakItemId, "new_page_break_item_name", "" /* parent_id */,
          app_item_B2->position()
              .CreateBetween(app_item_B3->position())
              .ToDebugString(),
          "pinordinal",
          sync_pb::AppListSpecifics_AppListItemType_TYPE_PAGE_BREAK)));
  // Sync change for moving A1 between B1 and B2.
  change_list.push_back(syncer::SyncChange(
      FROM_HERE, syncer::SyncChange::ACTION_UPDATE,
      CreateAppRemoteData(kItemIdA1, "A1", "" /* parent_id */,
                          app_item_B1->position()
                              .CreateBetween(app_item_B2->position())
                              .ToDebugString(),
                          "pinordinal")));

  app_list_syncable_service()->ProcessSyncChanges(base::Location(),
                                                  change_list);
  content::RunAllTasksUntilIdle();

  // Verify the original page break item after B3 is removed.
  EXPECT_FALSE(GetSyncItem(kPageBreakItemId2));
  EXPECT_FALSE(model_updater->FindItem(kPageBreakItemId2));

  // Verify a new page break sync item is created.
  EXPECT_TRUE(GetSyncItem(kNewPageBreakItemId));

  // Verify the items in model updater now looks like:
  // A2 [pagebreak 1]
  // B1 A1 B2 [new pagebreak]
  // B3 C1 [pagebreak 3]
  auto ordered_items_after_sync = GetOrderedItemIdsFromModelUpdater();
  EXPECT_THAT(ordered_items_after_sync,
              ElementsAre(kItemIdA2, kPageBreakItemId1, kItemIdB1, kItemIdA1,
                          kItemIdB2, kNewPageBreakItemId, kItemIdB3, kItemIdC1,
                          kPageBreakItemId3));
}

TEST_F(AppListSyncableServiceTest, FirstAvailablePosition) {
  RemoveAllExistingItems();

  // Populate the first page with items and leave 1 empty slot at the end.
  const int max_items_in_first_page =
      ash::SharedAppListConfig::instance().GetMaxNumOfItemsPerPage();
  syncer::StringOrdinal last_app_position =
      syncer::StringOrdinal::CreateInitialOrdinal();
  AppListModelUpdater* model_updater = GetModelUpdater();
  for (int i = 0; i < max_items_in_first_page - 1; ++i) {
    std::unique_ptr<ChromeAppListItem> item =
        std::make_unique<ChromeAppListItem>(
            profile_.get(), GenerateId("item_id" + base::NumberToString(i)),
            model_updater);
    ItemTestApi(item.get()).SetPosition(last_app_position);
    model_updater->AddItem(std::move(item));
    if (i < max_items_in_first_page - 2)
      last_app_position = last_app_position.CreateAfter();
  }
  EXPECT_TRUE(last_app_position.CreateAfter().Equals(
      model_updater->GetFirstAvailablePosition()));

  // Add a "page break" item at the end of first page.
  std::unique_ptr<ChromeAppListItem> page_break_item =
      std::make_unique<ChromeAppListItem>(
          profile_.get(), GenerateId("page_break_item_id"), model_updater);
  const syncer::StringOrdinal page_break_position =
      last_app_position.CreateAfter();
  ItemTestApi(page_break_item.get()).SetPosition(page_break_position);
  page_break_item->SetIsPageBreak(true);
  model_updater->AddItem((std::move(page_break_item)));
  EXPECT_TRUE(last_app_position.CreateBetween(page_break_position)
                  .Equals(model_updater->GetFirstAvailablePosition()));

  // Fill up the first page.
  std::unique_ptr<ChromeAppListItem> app_item =
      std::make_unique<ChromeAppListItem>(
          profile_.get(),
          GenerateId("item_id" + base::NumberToString(max_items_in_first_page)),
          model_updater);
  ItemTestApi(app_item.get())
      .SetPosition(last_app_position.CreateBetween(page_break_position));
  model_updater->AddItem(std::move(app_item));
  EXPECT_TRUE(page_break_position.CreateAfter().Equals(
      model_updater->GetFirstAvailablePosition()));
}

// Test that installing an app between two items with the same position will put
// that app at next available position. The test also ensures that no crash
// occurs (See https://crbug.com/907637).
TEST_F(AppListSyncableServiceTest, FirstAvailablePositionNotExist) {
  RemoveAllExistingItems();

  // Populate the first page with items and leave 1 empty slot at the end.
  const int max_items_in_first_page =
      ash::SharedAppListConfig::instance().GetMaxNumOfItemsPerPage();
  syncer::StringOrdinal last_app_position =
      syncer::StringOrdinal::CreateInitialOrdinal();
  AppListModelUpdater* model_updater = GetModelUpdater();
  for (int i = 0; i < max_items_in_first_page - 1; ++i) {
    std::unique_ptr<ChromeAppListItem> item =
        std::make_unique<ChromeAppListItem>(
            profile_.get(), GenerateId("item_id" + base::NumberToString(i)),
            model_updater);
    ItemTestApi(item.get()).SetPosition(last_app_position);
    model_updater->AddItem(std::move(item));
    if (i < max_items_in_first_page - 2)
      last_app_position = last_app_position.CreateAfter();
  }

  // Add a "page break" item at the end of first page with the same position as
  // last app item.
  std::unique_ptr<ChromeAppListItem> page_break_item =
      std::make_unique<ChromeAppListItem>(
          profile_.get(), GenerateId("page_break_item_id"), model_updater);
  ItemTestApi(page_break_item.get()).SetPosition(last_app_position);
  page_break_item->SetIsPageBreak(true);
  model_updater->AddItem((std::move(page_break_item)));
  EXPECT_TRUE(last_app_position.CreateAfter().Equals(
      model_updater->GetFirstAvailablePosition()));
}

// Test that verifies app attributes are transferred to the existing app and to
// to the app which will be installed later.
TEST_F(AppListSyncableServiceTest, TransferItem) {
  // Webstore app in this test is source app.
  scoped_refptr<extensions::Extension> webstore =
      MakeApp(kSomeAppName, extensions::kWebStoreAppId,
              extensions::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(webstore.get());

  // Chrome is an existing app to transfer attributes to.
  scoped_refptr<extensions::Extension> chrome =
      MakeApp(kSomeAppName, extension_misc::kChromeAppId,
              extensions::Extension::WAS_INSTALLED_BY_DEFAULT);
  InstallExtension(chrome.get());

  // Youtube is a future app to be installed.
  scoped_refptr<extensions::Extension> youtube =
      MakeApp(kSomeAppName, extension_misc::kYoutubeAppId,
              extensions::Extension::WAS_INSTALLED_BY_DEFAULT);

  // Webstore and Chrome items should exist now in sync and in model but not
  // Youtube.
  const app_list::AppListSyncableService::SyncItem* webstore_sync_item =
      GetSyncItem(extensions::kWebStoreAppId);
  AppListModelUpdater* model_updater = GetModelUpdater();
  const ChromeAppListItem* webstore_item =
      model_updater->FindItem(extensions::kWebStoreAppId);
  ASSERT_TRUE(webstore_item);
  ASSERT_TRUE(webstore_sync_item);

  const app_list::AppListSyncableService::SyncItem* chrome_sync_item =
      GetSyncItem(extension_misc::kChromeAppId);
  const ChromeAppListItem* chrome_item =
      model_updater->FindItem(extension_misc::kChromeAppId);
  ASSERT_TRUE(chrome_item);
  ASSERT_TRUE(chrome_sync_item);

  EXPECT_FALSE(GetSyncItem(extension_misc::kYoutubeAppId));
  EXPECT_FALSE(model_updater->FindItem(extension_misc::kYoutubeAppId));

  // Modify Webstore app with non-default attributes.
  model_updater->SetItemPosition(extensions::kWebStoreAppId,
                                 syncer::StringOrdinal("position"));
  model_updater->SetItemFolderId(extensions::kWebStoreAppId, "folderid");
  app_list_syncable_service()->SetPinPosition(extensions::kWebStoreAppId,
                                              syncer::StringOrdinal("pin"));

  // Before transfer attributes are different in both, app item and in sync.
  EXPECT_TRUE(AreAllAppAtributesNotEqualInAppList(webstore_item, chrome_item));
  EXPECT_TRUE(
      AreAllAppAtributesNotEqualInSync(webstore_sync_item, chrome_sync_item));

  // Perform attributes transfer to existing Chrome app.
  EXPECT_TRUE(app_list_syncable_service()->TransferItemAttributes(
      extensions::kWebStoreAppId, extension_misc::kChromeAppId));
  // Perform attributes transfer to the future Youtube app.
  EXPECT_TRUE(app_list_syncable_service()->TransferItemAttributes(
      extensions::kWebStoreAppId, extension_misc::kYoutubeAppId));
  // No sync item is created due to transfer to the future app.
  EXPECT_FALSE(GetSyncItem(extension_misc::kYoutubeAppId));
  // Attributes transfer from non-existing app fails.
  EXPECT_FALSE(app_list_syncable_service()->TransferItemAttributes(
      "NonExistingId", extension_misc::kYoutubeAppId));

  // Now Chrome app attributes match Webstore app.
  EXPECT_TRUE(AreAllAppAtributesEqualInAppList(webstore_item, chrome_item));
  EXPECT_TRUE(
      AreAllAppAtributesEqualInSync(webstore_sync_item, chrome_sync_item));

  // Install Youtube now.
  InstallExtension(youtube.get());

  const app_list::AppListSyncableService::SyncItem* youtube_sync_item =
      GetSyncItem(extension_misc::kYoutubeAppId);
  const ChromeAppListItem* youtube_item =
      model_updater->FindItem(extension_misc::kYoutubeAppId);
  ASSERT_TRUE(youtube_item);
  ASSERT_TRUE(youtube_sync_item);

  EXPECT_TRUE(AreAllAppAtributesEqualInAppList(webstore_item, youtube_item));
  EXPECT_TRUE(
      AreAllAppAtributesEqualInSync(webstore_sync_item, youtube_sync_item));
}

class AppListSortUnitTest : public AppListSyncableServiceTest {
 public:
  AppListSortUnitTest() {
    feature_list_.InitWithFeatures(
        {ash::features::kLauncherAppSort, ash::features::kProductivityLauncher},
        {});
  }
  AppListSortUnitTest(const AppListSortUnitTest&) = delete;
  AppListSortUnitTest& operator=(const AppListSortUnitTest&) = delete;
  ~AppListSortUnitTest() override = default;

  // Returns the app list order stored as preference.
  ash::AppListSortOrder GetSortOrderFromPrefs() {
    return static_cast<ash::AppListSortOrder>(
        app_list_syncable_service()->profile()->GetPrefs()->GetInteger(
            prefs::kAppListPreferredOrder));
  }

  // A hacky way to change an item's name.
  void ChangeItemName(const std::string& id, const std::string& new_name) {
    app_list_syncable_service()->GetMutableSyncItemForTest(id)->item_name =
        new_name;
    app_list_syncable_service()->GetModelUpdater()->SetItemName(id, new_name);
  }

 private:
  base::test::ScopedFeatureList feature_lists_;
};

// Verifies that sorting works for the mixture of valid and invalid positions.
TEST_F(AppListSortUnitTest, SortMixedPositionValidityItems) {
  RemoveAllExistingItems();

  using SyncItem = app_list::AppListSyncableService::SyncItem;
  const std::string kItemId1 = CreateNextAppId(extensions::kWebStoreAppId);
  auto item1 =
      std::make_unique<SyncItem>(kItemId1, sync_pb::AppListSpecifics::TYPE_APP);
  item1->item_name = "a";
  item1->item_ordinal = syncer::StringOrdinal(GetLastPositionString());

  const std::string kItemId2 = CreateNextAppId(kItemId1);
  auto item2 =
      std::make_unique<SyncItem>(kItemId2, sync_pb::AppListSpecifics::TYPE_APP);
  item2->item_name = "b";

  const std::string kItemId3 = CreateNextAppId(kItemId2);
  auto item3 =
      std::make_unique<SyncItem>(kItemId3, sync_pb::AppListSpecifics::TYPE_APP);
  item3->item_name = "c";

  std::vector<std::unique_ptr<SyncItem>> sync_items;
  sync_items.push_back(std::move(item1));
  sync_items.push_back(std::move(item2));
  sync_items.push_back(std::move(item3));

  // Populate items and verify their validity. Only `item1` has the valid
  // position.
  app_list_syncable_service()->PopulateSyncItemsForTest(std::move(sync_items));
  EXPECT_TRUE(app_list_syncable_service()
                  ->GetSyncItem(kItemId1)
                  ->item_ordinal.IsValid());
  EXPECT_FALSE(app_list_syncable_service()
                   ->GetSyncItem(kItemId2)
                   ->item_ordinal.IsValid());
  EXPECT_FALSE(app_list_syncable_service()
                   ->GetSyncItem(kItemId3)
                   ->item_ordinal.IsValid());

  app_list_syncable_service()->SetSyncItemOrder(
      ash::AppListSortOrder::kNameReverseAlphabetical);
  EXPECT_EQ(GetOrderedItemIdsFromSyncableService(),
            std::vector<std::string>({kItemId3, kItemId2, kItemId1}));

  app_list_syncable_service()->SetSyncItemOrder(
      ash::AppListSortOrder::kNameAlphabetical);
  EXPECT_EQ(GetOrderedItemIdsFromSyncableService(),
            std::vector<std::string>({kItemId1, kItemId2, kItemId3}));
}

// Verifies that sorting works if all item positions are invalid.
TEST_F(AppListSortUnitTest, SortInvalidPositionItems) {
  RemoveAllExistingItems();

  using SyncItem = app_list::AppListSyncableService::SyncItem;
  const std::string kItemId1 = CreateNextAppId(extensions::kWebStoreAppId);
  auto item1 =
      std::make_unique<SyncItem>(kItemId1, sync_pb::AppListSpecifics::TYPE_APP);
  item1->item_name = "a";

  const std::string kItemId2 = CreateNextAppId(kItemId1);
  auto item2 =
      std::make_unique<SyncItem>(kItemId2, sync_pb::AppListSpecifics::TYPE_APP);
  item2->item_name = "b";

  const std::string kItemId3 = CreateNextAppId(kItemId2);
  auto item3 =
      std::make_unique<SyncItem>(kItemId3, sync_pb::AppListSpecifics::TYPE_APP);
  item3->item_name = "c";

  std::vector<std::unique_ptr<SyncItem>> sync_items;
  sync_items.push_back(std::move(item1));
  sync_items.push_back(std::move(item2));
  sync_items.push_back(std::move(item3));

  // Verify the validity of sync item positions.
  app_list_syncable_service()->PopulateSyncItemsForTest(std::move(sync_items));
  EXPECT_FALSE(app_list_syncable_service()
                   ->GetSyncItem(kItemId1)
                   ->item_ordinal.IsValid());
  EXPECT_FALSE(app_list_syncable_service()
                   ->GetSyncItem(kItemId2)
                   ->item_ordinal.IsValid());
  EXPECT_FALSE(app_list_syncable_service()
                   ->GetSyncItem(kItemId3)
                   ->item_ordinal.IsValid());

  app_list_syncable_service()->SetSyncItemOrder(
      ash::AppListSortOrder::kNameReverseAlphabetical);
  EXPECT_EQ(GetOrderedItemIdsFromSyncableService(),
            std::vector<std::string>({kItemId3, kItemId2, kItemId1}));

  app_list_syncable_service()->SetSyncItemOrder(
      ash::AppListSortOrder::kNameAlphabetical);
  EXPECT_EQ(GetOrderedItemIdsFromSyncableService(),
            std::vector<std::string>({kItemId1, kItemId2, kItemId3}));
}

// Verifies that sorting with alphateical order works as expected for both
// folder items and app items.
TEST_F(AppListSortUnitTest, VerifyAlphabeticalOrderForFolderItems) {
  RemoveAllExistingItems();
  syncer::SyncDataList sync_list;

  // Add two apps.
  const std::string kItemId1 = CreateNextAppId(extensions::kWebStoreAppId);
  sync_list.push_back(
      CreateAppRemoteData(kItemId1, "a", "", GetLastPositionString(), kUnset));
  const std::string kItemId2 = CreateNextAppId(kItemId1);
  sync_list.push_back(
      CreateAppRemoteData(kItemId2, "b", "", GetLastPositionString(), kUnset));

  // Add one folder containing two apps.
  const std::string kFolderId1 = GenerateId("FolderId1");
  const std::string kChildItemId1_1 = CreateNextAppId(kItemId2);
  const std::string kChildItemId1_2 = CreateNextAppId(kChildItemId1_1);
  sync_list.push_back(CreateAppRemoteData(
      kFolderId1, "Folder1", "", GetLastPositionString(), kUnset,
      sync_pb::AppListSpecifics_AppListItemType_TYPE_FOLDER));
  sync_list.push_back(CreateAppRemoteData(kChildItemId1_1, "folder1_child1",
                                          kFolderId1, GetLastPositionString(),
                                          kUnset));
  sync_list.push_back(CreateAppRemoteData(kChildItemId1_2, "folder1_child2",
                                          kFolderId1, GetLastPositionString(),
                                          kUnset));

  // Add one folder containing three apps.
  const std::string kFolderId2 = GenerateId("FolderId2");
  const std::string kChildItemId2_1 = CreateNextAppId(kChildItemId1_2);
  const std::string kChildItemId2_2 = CreateNextAppId(kChildItemId2_1);
  const std::string kChildItemId2_3 = CreateNextAppId(kChildItemId2_2);
  sync_list.push_back(CreateAppRemoteData(
      kFolderId2, "Folder2", "", GetLastPositionString(), kUnset,
      sync_pb::AppListSpecifics_AppListItemType_TYPE_FOLDER));
  sync_list.push_back(CreateAppRemoteData(kChildItemId2_1, "folder2_child1",
                                          kFolderId2, GetLastPositionString(),
                                          kUnset));
  sync_list.push_back(CreateAppRemoteData(kChildItemId2_2, "folder2_child2",
                                          kFolderId2, GetLastPositionString(),
                                          kUnset));
  sync_list.push_back(CreateAppRemoteData(kChildItemId2_3, "folder2_child3",
                                          kFolderId2, GetLastPositionString(),
                                          kUnset));

  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  // Check the default status before sorting.
  EXPECT_EQ(
      GetOrderedItemIdsFromSyncableService(),
      std::vector<std::string>({kItemId1, kItemId2, kFolderId1, kChildItemId1_1,
                                kChildItemId1_2, kFolderId2, kChildItemId2_1,
                                kChildItemId2_2, kChildItemId2_3}));

  app_list_syncable_service()->SetSyncItemOrder(
      ash::AppListSortOrder::kNameReverseAlphabetical);

  // Folders should be in front of apps.
  EXPECT_EQ(GetOrderedItemIdsFromSyncableService(),
            std::vector<std::string>({kFolderId2, kFolderId1, kChildItemId2_3,
                                      kChildItemId2_2, kChildItemId2_1,
                                      kChildItemId1_2, kChildItemId1_1,
                                      kItemId2, kItemId1}));
}

// Verifies that sorting app items with the alphabetical order should work as
// expected. Meanwhile, sorting should incur the minimum orinal changes.
TEST_F(AppListSortUnitTest, VerifyAlphabeticalOrderSort) {
  RemoveAllExistingItems();
  syncer::SyncDataList sync_list;
  const std::string kItemId1 = CreateNextAppId(extensions::kWebStoreAppId);
  sync_list.push_back(
      CreateAppRemoteData(kItemId1, "A", "", GetLastPositionString(), kUnset));
  const std::string kItemId2 = CreateNextAppId(kItemId1);
  sync_list.push_back(
      CreateAppRemoteData(kItemId2, "B", "", GetLastPositionString(), kUnset));
  const std::string kItemId3 = CreateNextAppId(kItemId2);
  sync_list.push_back(
      CreateAppRemoteData(kItemId3, "C", "", GetLastPositionString(), kUnset));
  const std::string kItemId4 = CreateNextAppId(kItemId3);
  sync_list.push_back(
      CreateAppRemoteData(kItemId4, "D", "", GetLastPositionString(), kUnset));
  const std::string kItemId5 = CreateNextAppId(kItemId4);
  sync_list.push_back(
      CreateAppRemoteData(kItemId5, "E", "", GetLastPositionString(), kUnset));

  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  // Record the mappings between ids and default ordinals.
  std::unordered_map<std::string, syncer::StringOrdinal> id_ordinal_mappings;
  for (const auto& id_item_pair : app_list_syncable_service()->sync_items()) {
    id_ordinal_mappings[id_item_pair.first] = id_item_pair.second->item_ordinal;
  }

  // Sorting in alphabetical order should not change any ordinal. Because apps
  // are already in order.
  EXPECT_EQ(GetOrderedItemIdsFromSyncableService(),
            std::vector<std::string>(
                {kItemId1, kItemId2, kItemId3, kItemId4, kItemId5}));
  app_list_syncable_service()->SetSyncItemOrder(
      ash::AppListSortOrder::kNameAlphabetical);
  for (const auto& id_item_pair : app_list_syncable_service()->sync_items()) {
    EXPECT_EQ(id_ordinal_mappings[id_item_pair.first],
              id_item_pair.second->item_ordinal);
  }

  // Sort in reverse alphabetical order. Verify the app order after sorting.
  app_list_syncable_service()->SetSyncItemOrder(
      ash::AppListSortOrder::kNameReverseAlphabetical);
  EXPECT_EQ(GetOrderedItemIdsFromSyncableService(),
            std::vector<std::string>(
                {kItemId5, kItemId4, kItemId3, kItemId2, kItemId1}));

  const auto* sync_item = app_list_syncable_service()->GetSyncItem(kItemId4);
  syncer::SyncChangeList change_list{syncer::SyncChange(
      FROM_HERE, syncer::SyncChange::ACTION_UPDATE,
      CreateAppRemoteData(kItemId4, sync_item->item_name, sync_item->parent_id,
                          app_list_syncable_service()
                              ->GetSyncItem(kItemId1)
                              ->item_ordinal.CreateAfter()
                              .ToDebugString(),
                          sync_item->item_pin_ordinal.ToDebugString()))};
  app_list_syncable_service()->ProcessSyncChanges(base::Location(),
                                                  change_list);
  content::RunAllTasksUntilIdle();

  // Move Item 4 to the end. Record the mappings between ids and ordinals.
  EXPECT_EQ(GetOrderedItemIdsFromSyncableService(),
            std::vector<std::string>(
                {kItemId5, kItemId3, kItemId2, kItemId1, kItemId4}));
  id_ordinal_mappings.clear();
  for (const auto& id_item_pair : app_list_syncable_service()->sync_items()) {
    id_ordinal_mappings[id_item_pair.first] = id_item_pair.second->item_ordinal;
  }

  // Sort and then verify the app order.
  app_list_syncable_service()->SetSyncItemOrder(
      ash::AppListSortOrder::kNameReverseAlphabetical);
  EXPECT_EQ(GetOrderedItemIdsFromSyncableService(),
            std::vector<std::string>(
                {kItemId5, kItemId4, kItemId3, kItemId2, kItemId1}));

  // Verify that only Item 4's ordinal changes.
  for (const auto& id_item_pair : app_list_syncable_service()->sync_items()) {
    if (id_item_pair.first == kItemId4) {
      EXPECT_NE(id_ordinal_mappings[id_item_pair.first],
                id_item_pair.second->item_ordinal);
    } else {
      EXPECT_EQ(id_ordinal_mappings[id_item_pair.first],
                id_item_pair.second->item_ordinal);
    }
  }
}

// Verifies that sorting app items with the alphabetical order should work for
// the apps with the duplicate names.
TEST_F(AppListSortUnitTest, VerifyAlphabeticalSortWithDuplicateNames) {
  RemoveAllExistingItems();
  syncer::SyncDataList sync_list;
  const std::string kItemId1 = CreateNextAppId(extensions::kWebStoreAppId);
  sync_list.push_back(
      CreateAppRemoteData(kItemId1, "B", "", GetLastPositionString(), kUnset));
  const std::string kItemId2 = CreateNextAppId(kItemId1);
  sync_list.push_back(
      CreateAppRemoteData(kItemId2, "A", "", GetLastPositionString(), kUnset));
  const std::string kItemId3 = CreateNextAppId(kItemId2);
  sync_list.push_back(
      CreateAppRemoteData(kItemId3, "C", "", GetLastPositionString(), kUnset));
  const std::string kItemId4 = CreateNextAppId(kItemId3);
  sync_list.push_back(
      CreateAppRemoteData(kItemId4, "D", "", GetLastPositionString(), kUnset));
  const std::string kItemId5 = CreateNextAppId(kItemId4);
  sync_list.push_back(
      CreateAppRemoteData(kItemId5, "C", "", GetLastPositionString(), kUnset));
  const std::string kItemId6 = CreateNextAppId(kItemId5);
  sync_list.push_back(
      CreateAppRemoteData(kItemId6, "A", "", GetLastPositionString(), kUnset));

  app_list_syncable_service()->MergeDataAndStartSyncing(
      syncer::APP_LIST, sync_list,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  content::RunAllTasksUntilIdle();

  app_list_syncable_service()->SetSyncItemOrder(
      ash::AppListSortOrder::kNameAlphabetical);
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"A", "A", "B", "C", "C", "D"}));

  app_list_syncable_service()->SetSyncItemOrder(
      ash::AppListSortOrder::kNameReverseAlphabetical);
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"D", "C", "C", "B", "A", "A"}));
}

// Verifies that a new app is placed at the correct place when the launcher is
// in (reverse) alphabetical order.
TEST_F(AppListSortUnitTest, NewAppPlacement) {
  RemoveAllExistingItems();
  EXPECT_EQ(ash::AppListSortOrder::kCustom, GetSortOrderFromPrefs());

  const std::string kItemId1 = CreateNextAppId(extensions::kWebStoreAppId);
  const std::string kItemId2 = CreateNextAppId(kItemId1);
  const std::string kItemId3 = CreateNextAppId(kItemId2);
  const std::string kItemId4 = CreateNextAppId(kItemId3);

  scoped_refptr<extensions::Extension> app1 =
      MakeApp("A", kItemId1, extensions::Extension::NO_FLAGS);
  InstallExtension(app1.get());
  scoped_refptr<extensions::Extension> app2 =
      MakeApp("B", kItemId2, extensions::Extension::NO_FLAGS);
  InstallExtension(app2.get());
  scoped_refptr<extensions::Extension> app3 =
      MakeApp("C", kItemId3, extensions::Extension::NO_FLAGS);
  InstallExtension(app3.get());
  scoped_refptr<extensions::Extension> app4 =
      MakeApp("E", kItemId4, extensions::Extension::NO_FLAGS);
  InstallExtension(app4.get());

  app_list_syncable_service()->SetSyncItemOrder(
      ash::AppListSortOrder::kNameReverseAlphabetical);
  EXPECT_EQ(ash::AppListSortOrder::kNameReverseAlphabetical,
            GetSortOrderFromPrefs());
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"E", "C", "B", "A"}));

  // Insert another app. Verify the order.
  const std::string kItemId5 = CreateNextAppId(kItemId4);
  scoped_refptr<extensions::Extension> app5 =
      MakeApp("D", kItemId5, extensions::Extension::NO_FLAGS);
  InstallExtension(app5.get());
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"E", "D", "C", "B", "A"}));

  app_list::AppListReorderDelegate::TestApi test_api(
      app_list_syncable_service()->reorder_delegate_for_test());

  // The longest subsequence in reverse alphabetical order is the whole
  // sequence. Therefore the entropy is (1 - 5/5), which is 0.
  EXPECT_TRUE(cc::MathUtil::IsWithinEpsilon(
      0.f, test_api.CalculateEntropy(
               ash::AppListSortOrder::kNameReverseAlphabetical)));

  // The longest subsequence in alphabetical order has one element so the
  // entropy is (1 - 1/5) which is 0.8.
  EXPECT_EQ(0.8f, test_api.CalculateEntropy(
                      ash::AppListSortOrder::kNameAlphabetical));

  app_list_syncable_service()->SetSyncItemOrder(
      ash::AppListSortOrder::kNameAlphabetical);
  EXPECT_EQ(ash::AppListSortOrder::kNameAlphabetical, GetSortOrderFromPrefs());
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"A", "B", "C", "D", "E"}));

  ChangeItemName(kItemId3, "Z");
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"A", "B", "Z", "D", "E"}));

  // The longest subsequence in order is ["A", "B", "D", "E"] so the entropy is
  // (1 - 4/5) which is 0.2.
  EXPECT_TRUE(cc::MathUtil::IsWithinEpsilon(
      0.2f,
      test_api.CalculateEntropy(ash::AppListSortOrder::kNameAlphabetical)));

  // Install a new app. Verify its location.
  const std::string kItemId6 = CreateNextAppId(kItemId5);
  scoped_refptr<extensions::Extension> app6 =
      MakeApp("C", kItemId6, extensions::Extension::NO_FLAGS);
  InstallExtension(app6.get());
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"A", "B", "C", "Z", "D", "E"}));

  // Change another app's name.
  ChangeItemName(kItemId2, "F");
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"A", "F", "C", "Z", "D", "E"}));

  // The longest subsequence in order is ["A", "C", "D", "E"] so the entropy is
  // (1 - 4/6).
  EXPECT_TRUE(cc::MathUtil::IsWithinEpsilon(
      1 / 3.f,
      test_api.CalculateEntropy(ash::AppListSortOrder::kNameAlphabetical)));

  // Install a new app.
  EXPECT_EQ(ash::AppListSortOrder::kNameAlphabetical, GetSortOrderFromPrefs());
  const std::string kItemId7 = CreateNextAppId(kItemId6);
  scoped_refptr<extensions::Extension> app7 =
      MakeApp("G", kItemId7, extensions::Extension::NO_FLAGS);
  InstallExtension(app7.get());

  // The entropy is too high so the new app is inserted at the front.
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"G", "A", "F", "C", "Z", "D", "E"}));

  // The sort order is reset.
  EXPECT_EQ(ash::AppListSortOrder::kCustom, GetSortOrderFromPrefs());

  // Install a new app.
  const std::string kItemId8 = CreateNextAppId(kItemId7);
  scoped_refptr<extensions::Extension> app8 =
      MakeApp("H", kItemId8, extensions::Extension::NO_FLAGS);
  InstallExtension(app8.get());

  // Because the sort order is kCustom, the new app is placed at the front.
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"H", "G", "A", "F", "C", "Z", "D", "E"}));
}

// Verifies that a new app is placed at the correct place when initially all of
// top level items are folders.
TEST_F(AppListSortUnitTest, NewAppPlacementInitiallyOnlyFolders) {
  RemoveAllExistingItems();

  // Add three folders.
  syncer::StringOrdinal position =
      syncer::StringOrdinal::CreateInitialOrdinal();
  const std::string kFolderItemId1 = GenerateId("folder_id1");
  AppListModelUpdater* model_updater = GetModelUpdater();
  std::unique_ptr<ChromeAppListItem> folder_item1 =
      std::make_unique<ChromeAppListItem>(profile_.get(), kFolderItemId1,
                                          model_updater);
  folder_item1->SetChromeIsFolder(true);
  ItemTestApi(folder_item1.get()).SetPosition(position);
  ItemTestApi(folder_item1.get()).SetName("Folder1");
  app_list_syncable_service()->AddItem(std::move(folder_item1));
  position = position.CreateBefore();

  const std::string kFolderItemId2 = GenerateId("folder_id2");
  std::unique_ptr<ChromeAppListItem> folder_item2 =
      std::make_unique<ChromeAppListItem>(profile_.get(), kFolderItemId2,
                                          model_updater);
  folder_item2->SetChromeIsFolder(true);
  ItemTestApi(folder_item2.get()).SetPosition(position);
  ItemTestApi(folder_item2.get()).SetName("Folder2");
  app_list_syncable_service()->AddItem(std::move(folder_item2));
  position = position.CreateBefore();

  const std::string kFolderItemId3 = GenerateId("folder_id3");
  std::unique_ptr<ChromeAppListItem> folder_item3 =
      std::make_unique<ChromeAppListItem>(profile_.get(), kFolderItemId3,
                                          model_updater);
  folder_item3->SetChromeIsFolder(true);
  ItemTestApi(folder_item3.get()).SetPosition(position);
  // Use an empty folder name.
  ItemTestApi(folder_item3.get()).SetName("");
  app_list_syncable_service()->AddItem(std::move(folder_item3));

  // Sort sync items then verify the item order.
  app_list_syncable_service()->SetSyncItemOrder(
      ash::AppListSortOrder::kNameAlphabetical);
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"", "Folder1", "Folder2"}));

  // Install a new app.
  const std::string kNewAppId = CreateNextAppId(GenerateId("app_id"));
  scoped_refptr<extensions::Extension> app =
      MakeApp("B", kNewAppId, extensions::Extension::NO_FLAGS);
  InstallExtension(app.get());

  // Verify that the app is placed after folders.
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"", "Folder1", "Folder2", "B"}));

  // Verify that the entropy is zero.
  EXPECT_TRUE(cc::MathUtil::IsWithinEpsilon(
      0.f, app_list::AppListReorderDelegate::TestApi(
               app_list_syncable_service()->reorder_delegate_for_test())
               .CalculateEntropy(ash::AppListSortOrder::kNameAlphabetical)));

  // Install the second app.
  const std::string kNewAppId2 = CreateNextAppId(GenerateId("app_id2"));
  scoped_refptr<extensions::Extension> app2 =
      MakeApp("C", kNewAppId2, extensions::Extension::NO_FLAGS);
  InstallExtension(app2.get());

  // Verify that the app is placed after folders.
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"", "Folder1", "Folder2", "B", "C"}));

  // Change folders' names so that folders are out of order.
  ChangeItemName(kFolderItemId1, "Folder2");
  ChangeItemName(kFolderItemId2, "Folder1");
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"", "Folder2", "Folder1", "B", "C"}));

  // There is one folder item out of order so the entropy should be 1/5 = 0.2.
  EXPECT_TRUE(cc::MathUtil::IsWithinEpsilon(
      0.2f, app_list::AppListReorderDelegate::TestApi(
                app_list_syncable_service()->reorder_delegate_for_test())
                .CalculateEntropy(ash::AppListSortOrder::kNameAlphabetical)));

  // Install the third app. Verify the item order after installation.
  const std::string kNewAppId3 = CreateNextAppId(GenerateId("app_id3"));
  scoped_refptr<extensions::Extension> app3 =
      MakeApp("D", kNewAppId3, extensions::Extension::NO_FLAGS);
  InstallExtension(app3.get());
  EXPECT_EQ(
      GetNamesOfSortedItemsFromSyncableService(),
      std::vector<std::string>({"", "Folder2", "Folder1", "B", "C", "D"}));

  // Install the forth app. Verify that the new item is inserted between a
  // folder and an app.
  const std::string kNewAppId4 = CreateNextAppId(GenerateId("app_id4"));
  scoped_refptr<extensions::Extension> app4 =
      MakeApp("A", kNewAppId4, extensions::Extension::NO_FLAGS);
  InstallExtension(app4.get());
  EXPECT_EQ(
      GetNamesOfSortedItemsFromSyncableService(),
      std::vector<std::string>({"", "Folder2", "Folder1", "A", "B", "C", "D"}));
}

// Verifies that the new app's position maintains the launcher sort order among
// sync items (including the apps not enabled on the local device).
TEST_F(AppListSortUnitTest, VerifyNewAppPositionInGlobalScope) {
  RemoveAllExistingItems();

  const std::string kItemId1 = CreateNextAppId(GenerateId("app_id1"));
  scoped_refptr<extensions::Extension> app1 =
      MakeApp("C", kItemId1, extensions::Extension::NO_FLAGS);
  InstallExtension(app1.get());

  const std::string kItemId2 = CreateNextAppId(GenerateId("app_id2"));
  scoped_refptr<extensions::Extension> app2 =
      MakeApp("A", kItemId2, extensions::Extension::NO_FLAGS);
  InstallExtension(app2.get());

  const std::string kItemId3 = CreateNextAppId(GenerateId("app_id3"));
  scoped_refptr<extensions::Extension> app3 =
      MakeApp("D", kItemId3, extensions::Extension::NO_FLAGS);
  InstallExtension(app3.get());

  // The sort order is not set. Therefore an app is always placed at the front.
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"D", "A", "C"}));

  app_list_syncable_service()->SetSyncItemOrder(
      ash::AppListSortOrder::kNameAlphabetical);
  EXPECT_EQ(ash::AppListSortOrder::kNameAlphabetical, GetSortOrderFromPrefs());
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"A", "C", "D"}));

  // A hacky way to emulate that an item is disabled locally (in other words,
  // the app's sync data exists but its app list item data is missing).
  AppListModelUpdater* model_updater = GetModelUpdater();
  model_updater->RemoveItem(kItemId1);

  // Install a new app and verify the app order.
  const std::string kItemId4 = CreateNextAppId(GenerateId("app_id4"));
  scoped_refptr<extensions::Extension> app4 =
      MakeApp("B", kItemId4, extensions::Extension::NO_FLAGS);
  InstallExtension(app4.get());
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"A", "B", "C", "D"}));

  // Remove another item from the model. Now only "A" and "B" are in the model.
  model_updater->RemoveItem(kItemId3);

  // Install a new app and verify the app order.
  const std::string kItemId5 = CreateNextAppId(GenerateId("app_id5"));
  scoped_refptr<extensions::Extension> app5 =
      MakeApp("F", kItemId5, extensions::Extension::NO_FLAGS);
  InstallExtension(app5.get());
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"A", "B", "C", "D", "F"}));

  // Remove another item from the model. Now only "A" and "B" are in the model.
  model_updater->RemoveItem(kItemId5);

  // Install a new app and verify the app order.
  const std::string kItemId6 = CreateNextAppId(GenerateId("app_id6"));
  scoped_refptr<extensions::Extension> app6 =
      MakeApp("E", kItemId6, extensions::Extension::NO_FLAGS);
  InstallExtension(app6.get());
  EXPECT_EQ(GetNamesOfSortedItemsFromSyncableService(),
            std::vector<std::string>({"A", "B", "C", "D", "E", "F"}));
}
