// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {GooglePhotosTest} from './google_photos_element_test.js';
import {LocalImagesTest} from './local_images_element_test.js';
import {PersonalizationRouterTest} from './personalization_router_element_test.js';
import {PersonalizationToastTest} from './personalization_toast_element_test.js';
import {WallpaperBreadcrumbTest} from './wallpaper_breadcrumb_element_test.js';
import {WallpaperCollectionsTest} from './wallpaper_collections_element_test.js';
import {WallpaperFullscreenTest} from './wallpaper_fullscreen_element_test.js';
import {WallpaperImagesTest} from './wallpaper_images_element_test.js';
import {WallpaperSelectedTest} from './wallpaper_selected_element_test.js';

// Mute console.warn during tests. Several tests intentionally hit asserts to
// verify errors are thrown, and fill test logs with misleading stacktraces.
window.console.warn = () => {};

const testCases = [
  GooglePhotosTest,
  LocalImagesTest,
  PersonalizationRouterTest,
  PersonalizationToastTest,
  WallpaperBreadcrumbTest,
  WallpaperCollectionsTest,
  WallpaperFullscreenTest,
  WallpaperImagesTest,
  WallpaperSelectedTest,
];

for (const testCase of testCases) {
  suite(testCase.name, testCase);
}
