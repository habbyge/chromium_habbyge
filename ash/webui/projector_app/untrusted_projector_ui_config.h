// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WEBUI_PROJECTOR_APP_UNTRUSTED_PROJECTOR_UI_CONFIG_H_
#define ASH_WEBUI_PROJECTOR_APP_UNTRUSTED_PROJECTOR_UI_CONFIG_H_

#include "ui/webui/untrusted_web_ui_controller.h"
#include "ui/webui/webui_config.h"

namespace content {
class WebUI;
}  // namespace content

namespace ash {

// TODO(b/193670945): Migrate to ash/components and ash/webui.
class UntrustedProjectorUIConfig : public ui::WebUIConfig {
 public:
  UntrustedProjectorUIConfig();
  ~UntrustedProjectorUIConfig() override;

  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui) override;
};

}  // namespace ash

#endif  // ASH_WEBUI_PROJECTOR_APP_UNTRUSTED_PROJECTOR_UI_CONFIG_H_
