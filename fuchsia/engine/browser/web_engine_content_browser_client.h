// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FUCHSIA_ENGINE_BROWSER_WEB_ENGINE_CONTENT_BROWSER_CLIENT_H_
#define FUCHSIA_ENGINE_BROWSER_WEB_ENGINE_CONTENT_BROWSER_CLIENT_H_

#include <memory>
#include <string>
#include <vector>

#include "content/public/browser/content_browser_client.h"
#include "fuchsia/engine/browser/content_directory_loader_factory.h"
#include "mojo/public/cpp/bindings/binder_map.h"
#include "services/metrics/public/cpp/ukm_source_id.h"

class WebEngineBrowserMainParts;

class WebEngineContentBrowserClient final
    : public content::ContentBrowserClient {
 public:
  WebEngineContentBrowserClient();
  ~WebEngineContentBrowserClient() override;

  WebEngineContentBrowserClient(const WebEngineContentBrowserClient&) = delete;
  WebEngineContentBrowserClient& operator=(
      const WebEngineContentBrowserClient&) = delete;

  // ContentBrowserClient overrides.
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      content::MainFunctionParams parameters) override;
  std::unique_ptr<content::DevToolsManagerDelegate>
  CreateDevToolsManagerDelegate() override;
  std::string GetProduct() override;
  std::string GetUserAgent() override;
  blink::UserAgentMetadata GetUserAgentMetadata() override;
  void OverrideWebkitPrefs(content::WebContents* web_contents,
                           blink::web_pref::WebPreferences* web_prefs) override;
  void RegisterBrowserInterfaceBindersForFrame(
      content::RenderFrameHost* render_frame_host,
      mojo::BinderMapWithContext<content::RenderFrameHost*>* map) override;
  void RegisterNonNetworkNavigationURLLoaderFactories(
      int frame_tree_node_id,
      ukm::SourceIdObj ukm_source_id,
      NonNetworkURLLoaderFactoryMap* factories) override;
  void RegisterNonNetworkSubresourceURLLoaderFactories(
      int render_process_id,
      int render_frame_id,
      NonNetworkURLLoaderFactoryMap* factories) override;
  bool ShouldEnableStrictSiteIsolation() override;
  void AppendExtraCommandLineSwitches(base::CommandLine* command_line,
                                      int child_process_id) override;
  std::string GetApplicationLocale() override;
  std::string GetAcceptLangs(content::BrowserContext* context) override;
  base::OnceClosure SelectClientCertificate(
      content::WebContents* web_contents,
      net::SSLCertRequestInfo* cert_request_info,
      net::ClientCertIdentityList client_certs,
      std::unique_ptr<content::ClientCertificateDelegate> delegate) override;
  std::vector<std::unique_ptr<content::NavigationThrottle>>
  CreateThrottlesForNavigation(
      content::NavigationHandle* navigation_handle) override;
  std::vector<std::unique_ptr<blink::URLLoaderThrottle>>
  CreateURLLoaderThrottles(
      const network::ResourceRequest& request,
      content::BrowserContext* browser_context,
      const base::RepeatingCallback<content::WebContents*()>& wc_getter,
      content::NavigationUIData* navigation_ui_data,
      int frame_tree_node_id) override;
  void ConfigureNetworkContextParams(
      content::BrowserContext* context,
      bool in_memory,
      const base::FilePath& relative_partition_path,
      network::mojom::NetworkContextParams* network_context_params,
      cert_verifier::mojom::CertVerifierCreationParams*
          cert_verifier_creation_params) override;
  std::vector<url::Origin> GetOriginsRequiringDedicatedProcess() override;

  WebEngineBrowserMainParts* main_parts_for_test() const { return main_parts_; }

 private:
  const std::vector<std::string> cors_exempt_headers_;
  const bool allow_insecure_content_;

  // Owned by content::BrowserMainLoop.
  WebEngineBrowserMainParts* main_parts_;
};

#endif  // FUCHSIA_ENGINE_BROWSER_WEB_ENGINE_CONTENT_BROWSER_CLIENT_H_
