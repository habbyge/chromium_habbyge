// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/cursor/cursor_loader.h"

#include "base/memory/scoped_refptr.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/cursor/mojom/cursor_type.mojom-shared.h"
#include "ui/base/cursor/platform_cursor.h"

#if defined(OS_WIN)
#include "ui/base/cursor/win/win_cursor.h"
#include "ui/base/cursor/win/win_cursor_factory.h"
#endif

#if defined(USE_OZONE)
#include "ui/base/cursor/ozone/bitmap_cursor_factory_ozone.h"
#endif

namespace ui {
namespace {

using mojom::CursorType;

scoped_refptr<PlatformCursor> LoadInvisibleCursor() {
  CursorLoader cursor_loader;
  Cursor cursor(CursorType::kNone);
  cursor_loader.SetPlatformCursor(&cursor);
  return cursor.platform();
}

}  // namespace

#if defined(OS_WIN)
TEST(CursorLoaderTest, InvisibleCursor) {
  WinCursorFactory cursor_factory;
  auto invisible_cursor = LoadInvisibleCursor();
  ASSERT_NE(invisible_cursor, nullptr);
  EXPECT_EQ(WinCursor::FromPlatformCursor(invisible_cursor)->hcursor(),
            nullptr);
}
#endif

#if defined(USE_OZONE)
TEST(CursorLoaderTest, InvisibleCursor) {
  BitmapCursorFactoryOzone cursor_factory;
  auto invisible_cursor = LoadInvisibleCursor();
  ASSERT_NE(invisible_cursor, nullptr);
  EXPECT_EQ(BitmapCursorOzone::FromPlatformCursor(invisible_cursor)->type(),
            CursorType::kNone);
}
#endif

}  // namespace ui
