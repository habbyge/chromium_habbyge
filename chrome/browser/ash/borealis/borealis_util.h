// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_BOREALIS_BOREALIS_UTIL_H_
#define CHROME_BROWSER_ASH_BOREALIS_BOREALIS_UTIL_H_

#include <string>

#include "base/callback_forward.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/views/widget/widget.h"

class Profile;

namespace guest_os {
class GuestOsRegistryService;
}

namespace borealis {

// This is used by the Borealis app and the Borealis installer.
// Generated by crx_file::id_util::GenerateId("org.chromium.borealis");
extern const char kBorealisAppId[];
// This is the id of the main application which borealis runs.
extern const char kBorealisMainAppId[];
// This is used to install the Borealis DLC component.
extern const char kBorealisDlcName[];
// The regex used for extracting the Borealis app ID of an application.
extern const char kBorealisAppIdRegex[];

// Shows the Borealis installer (borealis_installer_view).
void ShowBorealisInstallerView(Profile* profile);

// Returns a Borealis app ID parsed from |exec|, or nullopt on failure.
// TODO(b/173547790): This should probably be moved when we've decided
// the details of how/where it will be used.
absl::optional<int> GetBorealisAppId(std::string exec);

// Returns the Borealis app ID of the |window|, or nullopt on failure.
absl::optional<int> GetBorealisAppId(const aura::Window* window);

// Shows the splash screen (borealis_splash_screen_view).
void ShowBorealisSplashScreenView(Profile* profile);
// Closes the splash screen (borealis_splash_screen_view).
void CloseBorealisSplashScreenView();

// Creates a URL for a feedback form with prefilled app/device info, or an
// invalid URL if we don't want to collect feedback for the given |app_id|. Will
// invoke |url_callback| when the url is ready.
void FeedbackFormUrl(const guest_os::GuestOsRegistryService* registry_service,
                     const std::string& app_id,
                     const std::string& window_title,
                     base::OnceCallback<void(GURL)> url_callback);

}  // namespace borealis

#endif  // CHROME_BROWSER_ASH_BOREALIS_BOREALIS_UTIL_H_
