// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_PUBLIC_CPP_APP_LIST_APP_LIST_MODEL_DELEGATE_H_
#define ASH_PUBLIC_CPP_APP_LIST_APP_LIST_MODEL_DELEGATE_H_

#include <string>

#include "ash/public/cpp/app_list/app_list_types.h"
#include "components/sync/model/string_ordinal.h"

namespace ash {

// The interface used to update app list items from ash. The browser side owns
// app list item data while the ash side is the consumer of app list item data.
// Ash classes should utilize this interface to update app list items.
// TODO(https://crbug.com/1257605): refactor the code so that the browser side
// owns app list item data.
class AppListModelDelegate {
 public:
  // Requests the owner to set the item indexed by `id` with `new_position`.
  // `id` is passed by value instead of a string reference. Because if `id`
  // is a reference to string, the method user may be misled to pass the item id
  // fetched from `AppListItem` as the parameter. It is risky because `id`
  // may be invalid if `AppListItem::SetMetadata()` is triggered.
  virtual void RequestPositionUpdate(std::string id,
                                     const syncer::StringOrdinal& new_position,
                                     RequestPositionUpdateReason reason) = 0;

  // Requests the owner to move the item indexed by `id` into the specified
  // folder.
  virtual void RequestMoveItemToFolder(std::string id,
                                       const std::string& folder_id,
                                       RequestMoveToFolderReason reason) = 0;

  // Requests the owner to move the item indexed by `id` out of its parent
  // folder. `target_position` is the item position after move.
  // `target_position` is copied in case it refers to the containing folder
  // which may get deleted.
  // TODO(https://crbug.com/1257605): In the long run, the browser side should
  // own the app list item meta data. `syncer::StringOrdinal` should be the
  // implementation detail of item position so it should not be seen in the Ash
  // side. Ideally `RequestMoveItemOutOfOrder()` should pass the item index
  // instead of item position to the browser side.
  virtual void RequestMoveItemToRoot(std::string id,
                                     syncer::StringOrdinal target_position) = 0;

 protected:
  virtual ~AppListModelDelegate() = default;
};

}  // namespace ash

#endif  // ASH_PUBLIC_CPP_APP_LIST_APP_LIST_MODEL_DELEGATE_H_
