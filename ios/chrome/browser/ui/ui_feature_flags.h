// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_UI_FEATURE_FLAGS_H_
#define IOS_CHROME_BROWSER_UI_UI_FEATURE_FLAGS_H_

#include "Availability.h"
#include "base/feature_list.h"

// Allows the user to track product prices through Chrome.
// Use IsPriceAlertsEnabled in price_alert_util rather than depending
// on this directly.
extern const base::Feature kCommercePriceTracking;

// Feature to open tab switcher after sliding down the toolbar.
extern const base::Feature kExpandedTabStrip;

// Test-only: Feature flag used to verify that EG2 can trigger flags. Must be
// always disabled by default, because it is used to verify that enabling
// features in tests works.
extern const base::Feature kTestFeature;

// Feature flag to enable Shared Highlighting (Link to Text).
extern const base::Feature kSharedHighlightingIOS;

// Feature flag for testing the 'default browser' screen in FRE and different
// experiments to suggest the users to update the default browser in the
// Settings.app.
extern const base::Feature kEnableFREDefaultBrowserScreenTesting;

// Feature flag that enables using the FRE UI module to show first run screens.
extern const base::Feature kEnableFREUIModuleIOS;

// Feature flag that enables using the strings of the previous sync screen in
// the current FRE.
extern const base::Feature kOldSyncStringFRE;

// TODO(crbug.com/1128242): Remove this flag after the refactoring work is
// finished. Flag to modernize the tabstrip without disturbing the existing one.
extern const base::Feature kModernTabStrip;

// Enables the usage of dark mode color while in Incognito mode.
extern const base::Feature kIncognitoBrandConsistencyForIOS;

// Feature flag to enable revamped Incognito NTP page.
extern const base::Feature kIncognitoNtpRevamp;

// Feature flag that experiments with the default browser fullscreen promo UI.
extern const base::Feature kDefaultBrowserFullscreenPromoExperiment;

// Feature flag that swaps the omnibox textfield implementation.
extern const base::Feature kIOSNewOmniboxImplementation;

// Feature flag that fixes omnibox behavior when using iOS native dictation
extern const base::Feature kIOSOmniboxAllowEditsDuringDictation;

// Feature flag that enables persisting the Crash Restore Infobar across
// navigations.
extern const base::Feature kIOSPersistCrashRestore;

// Enables the Search History Link in Clear Browsing Data for iOS.
extern const base::Feature kSearchHistoryLinkIOS;

// Feature flag to enable removing any entry points to the history UI from
// Incognito mode.
extern const base::Feature kUpdateHistoryEntryPointsInIncognito;

// Feature to update context menu actions.
extern const base::Feature kContextMenuActionsRefresh;

// Feature flag to enable using Lens to search for images.
extern const base::Feature kUseLensToSearchForImage;

// Feature flag to enable promotional view for Passwords In Other Apps in
// Settings.
extern const base::Feature kCredentialProviderExtensionPromo;

// Feature flag to enable duplicate NTP cleanup.
extern const base::Feature kRemoveExcessNTPs;

// Whether the ContextMenuActionsRefresh flag is enabled.
bool IsContextMenuActionsRefreshEnabled();

#endif  // IOS_CHROME_BROWSER_UI_UI_FEATURE_FLAGS_H_
