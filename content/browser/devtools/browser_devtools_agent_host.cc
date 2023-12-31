// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/devtools/browser_devtools_agent_host.h"

#include "base/bind.h"
#include "base/guid.h"
#include "base/json/json_reader.h"
#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "components/viz/common/buildflags.h"
#include "content/browser/devtools/devtools_session.h"
#include "content/browser/devtools/protocol/browser_handler.h"
#include "content/browser/devtools/protocol/fetch_handler.h"
#include "content/browser/devtools/protocol/io_handler.h"
#include "content/browser/devtools/protocol/memory_handler.h"
#include "content/browser/devtools/protocol/protocol.h"
#include "content/browser/devtools/protocol/security_handler.h"
#include "content/browser/devtools/protocol/storage_handler.h"
#include "content/browser/devtools/protocol/system_info_handler.h"
#include "content/browser/devtools/protocol/target_handler.h"
#include "content/browser/devtools/protocol/tethering_handler.h"
#include "content/browser/devtools/protocol/tracing_handler.h"
#include "content/browser/devtools/render_frame_devtools_agent_host.h"
#include "content/browser/devtools/service_worker_devtools_agent_host.h"
#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/browser/web_contents/web_contents_impl.h"

#if BUILDFLAG(USE_VIZ_DEBUGGER)
#include "content/browser/devtools/protocol/visual_debugger_handler.h"
#endif

namespace content {

scoped_refptr<DevToolsAgentHost> DevToolsAgentHost::CreateForBrowser(
    scoped_refptr<base::SingleThreadTaskRunner> tethering_task_runner,
    const CreateServerSocketCallback& socket_callback) {
  return new BrowserDevToolsAgentHost(
      tethering_task_runner, socket_callback, false);
}

scoped_refptr<DevToolsAgentHost> DevToolsAgentHost::CreateForDiscovery() {
  CreateServerSocketCallback null_callback;
  return new BrowserDevToolsAgentHost(nullptr, std::move(null_callback), true);
}

namespace {
std::set<BrowserDevToolsAgentHost*>& BrowserDevToolsAgentHostInstances() {
  static base::NoDestructor<std::set<BrowserDevToolsAgentHost*>> instances;
  return *instances;
}

}  // namespace

class BrowserDevToolsAgentHost::BrowserAutoAttacher final
    : public protocol::TargetAutoAttacher,
      public ServiceWorkerDevToolsManager::Observer,
      public DevToolsAgentHostObserver {
 public:
  BrowserAutoAttacher() = default;
  ~BrowserAutoAttacher() override = default;

 protected:
  // ServiceWorkerDevToolsManager::Observer implementation.
  void WorkerCreated(ServiceWorkerDevToolsAgentHost* host,
                     bool* should_pause_on_start) override {
    *should_pause_on_start = wait_for_debugger_on_start();
    DispatchAutoAttach(host, *should_pause_on_start);
  }

  void WorkerDestroyed(ServiceWorkerDevToolsAgentHost* host) override {
    DispatchAutoDetach(host);
  }

  void ReattachServiceWorkers() {
    DCHECK(auto_attach());
    ServiceWorkerDevToolsAgentHost::List agent_hosts;
    ServiceWorkerDevToolsManager::GetInstance()->AddAllAgentHosts(&agent_hosts);
    Hosts new_hosts(agent_hosts.begin(), agent_hosts.end());
    DispatchSetAttachedTargetsOfType(new_hosts,
                                     DevToolsAgentHost::kTypeServiceWorker);
  }

  void UpdateAutoAttach(base::OnceClosure callback) override {
    if (have_observers_ == auto_attach()) {
      std::move(callback).Run();
      return;
    }
    if (auto_attach()) {
      base::AutoReset<bool> auto_reset(&processing_existent_targets_, true);
      ServiceWorkerDevToolsManager::GetInstance()->AddObserver(this);
      DevToolsAgentHost::AddObserver(this);
      ReattachServiceWorkers();
    } else {
      DevToolsAgentHost::RemoveObserver(this);
      ServiceWorkerDevToolsManager::GetInstance()->RemoveObserver(this);
    }
    have_observers_ = auto_attach();
    std::move(callback).Run();
  }

  // DevToolsAgentHostObserver overrides.
  void DevToolsAgentHostCreated(DevToolsAgentHost* host) override {
    DCHECK(auto_attach());
    // In the top level target handler auto-attach to pages as soon as they
    // are created, otherwise if they don't incur any network activity we'll
    // never get a chance to throttle them (and auto-attach there).

    if (IsMainFrameHost(host)) {
      DispatchAutoAttach(
          host, wait_for_debugger_on_start() && !processing_existent_targets_);
    }
  }

  bool ShouldForceDevToolsAgentHostCreation() override { return true; }

  static bool IsMainFrameHost(DevToolsAgentHost* host) {
    WebContentsImpl* web_contents =
        static_cast<WebContentsImpl*>(host->GetWebContents());
    if (!web_contents)
      return false;
    // TODO(https://crbug.com/1264031): With MPArch a WebContents might have
    // multiple FrameTrees. Make sure this code really just needs the
    // primary one.
    FrameTreeNode* frame_tree_node = web_contents->GetPrimaryFrameTree().root();
    if (!frame_tree_node)
      return false;
    return host == RenderFrameDevToolsAgentHost::GetFor(frame_tree_node);
  }

  bool processing_existent_targets_ = false;
  bool have_observers_ = false;
};

// static
const std::set<BrowserDevToolsAgentHost*>&
BrowserDevToolsAgentHost::Instances() {
  return BrowserDevToolsAgentHostInstances();
}

BrowserDevToolsAgentHost::BrowserDevToolsAgentHost(
    scoped_refptr<base::SingleThreadTaskRunner> tethering_task_runner,
    const CreateServerSocketCallback& socket_callback,
    bool only_discovery)
    : DevToolsAgentHostImpl(base::GenerateGUID()),
      auto_attacher_(std::make_unique<BrowserAutoAttacher>()),
      tethering_task_runner_(tethering_task_runner),
      socket_callback_(socket_callback),
      only_discovery_(only_discovery) {
  NotifyCreated();
  BrowserDevToolsAgentHostInstances().insert(this);
}

BrowserDevToolsAgentHost::~BrowserDevToolsAgentHost() {
  BrowserDevToolsAgentHostInstances().erase(this);
}

bool BrowserDevToolsAgentHost::AttachSession(DevToolsSession* session,
                                             bool acquire_wake_lock) {
  if (!session->GetClient()->MayAttachToBrowser())
    return false;

  session->SetBrowserOnly(true);
  session->AddHandler(std::make_unique<protocol::TargetHandler>(
      protocol::TargetHandler::AccessMode::kBrowser, GetId(),
      auto_attacher_.get(), session->GetRootSession()));
  if (only_discovery_)
    return true;

  session->AddHandler(std::make_unique<protocol::BrowserHandler>(
      session->GetClient()->MayWriteLocalFiles()));
#if BUILDFLAG(USE_VIZ_DEBUGGER)
  session->AddHandler(std::make_unique<protocol::VisualDebuggerHandler>());
#endif
  session->AddHandler(std::make_unique<protocol::IOHandler>(GetIOContext()));
  session->AddHandler(std::make_unique<protocol::FetchHandler>(
      GetIOContext(),
      base::BindRepeating([](base::OnceClosure cb) { std::move(cb).Run(); })));
  session->AddHandler(std::make_unique<protocol::MemoryHandler>());
  session->AddHandler(std::make_unique<protocol::SecurityHandler>());
  session->AddHandler(std::make_unique<protocol::StorageHandler>());
  session->AddHandler(std::make_unique<protocol::SystemInfoHandler>());
  if (tethering_task_runner_) {
    session->AddHandler(std::make_unique<protocol::TetheringHandler>(
        socket_callback_, tethering_task_runner_));
  }
  session->AddHandler(
      std::make_unique<protocol::TracingHandler>(GetIOContext()));
  return true;
}

void BrowserDevToolsAgentHost::DetachSession(DevToolsSession* session) {
}

protocol::TargetAutoAttacher* BrowserDevToolsAgentHost::auto_attacher() {
  return auto_attacher_.get();
}

std::string BrowserDevToolsAgentHost::GetType() {
  return kTypeBrowser;
}

std::string BrowserDevToolsAgentHost::GetTitle() {
  return "";
}

GURL BrowserDevToolsAgentHost::GetURL() {
  return GURL();
}

bool BrowserDevToolsAgentHost::Activate() {
  return false;
}

bool BrowserDevToolsAgentHost::Close() {
  return false;
}

void BrowserDevToolsAgentHost::Reload() {
}

}  // content
