// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/exo/wayland/server.h"

#include <stdlib.h>

#include <wayland-client-core.h>

#include <memory>

#include "base/atomic_sequence_num.h"
#include "base/bind.h"
#include "base/files/file_enumerator.h"
#include "base/files/scoped_temp_dir.h"
#include "base/process/process_handle.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread.h"
#include "build/chromeos_buildflags.h"
#include "components/exo/capabilities.h"
#include "components/exo/display.h"
#include "components/exo/test/exo_test_base_views.h"
#include "components/exo/wayland/server_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "components/exo/test/exo_test_base.h"
#endif

namespace exo {
namespace wayland {
namespace {

base::AtomicSequenceNumber g_next_socket_id;

std::string GetUniqueSocketName() {
  return base::StringPrintf("wayland-test-%d-%d", base::GetCurrentProcId(),
                            g_next_socket_id.GetNext());
}

// Use ExoTestBase on Chrome OS because Server starts to depends on ash::Shell,
// which is unavailable on other platforms so then ExoTestBaseViews instead.
using TestBase =
#if BUILDFLAG(IS_CHROMEOS_ASH)
    test::ExoTestBase
#else
    test::ExoTestBaseViews
#endif
    ;

class ServerTest : public TestBase {
 public:
  ServerTest() = default;
  ServerTest(const ServerTest&) = delete;
  ServerTest& operator=(const ServerTest&) = delete;
  ~ServerTest() override = default;

  void SetUp() override {
    ASSERT_TRUE(xdg_temp_dir_.CreateUniqueTempDir());
    setenv("XDG_RUNTIME_DIR", xdg_temp_dir_.GetPath().MaybeAsASCII().c_str(),
           1 /* overwrite */);
    TestBase::SetUp();
  }

 protected:
  base::FilePath GetUniqueSocketPath() const {
    return xdg_temp_dir_.GetPath().Append(GetUniqueSocketName());
  }

 private:
  base::ScopedTempDir xdg_temp_dir_;
};

TEST_F(ServerTest, AddSocket) {
  std::unique_ptr<Display> display(new Display);
  std::unique_ptr<Server> server(
      new Server(display.get(), Capabilities::GetDefaultCapabilities()));
  server->Initialize();
  // Check that calling AddSocket() with a unique socket name succeeds.
  bool rv = server->AddSocket(GetUniqueSocketName());
  EXPECT_TRUE(rv);
}

TEST_F(ServerTest, GetFileDescriptor) {
  std::unique_ptr<Display> display(new Display);
  std::unique_ptr<Server> server(
      new Server(display.get(), Capabilities::GetDefaultCapabilities()));
  server->Initialize();
  bool rv = server->AddSocket(GetUniqueSocketName());
  EXPECT_TRUE(rv);

  // Check that the returned file descriptor is valid.
  int fd = server->GetFileDescriptor();
  DCHECK_GE(fd, 0);
}

TEST_F(ServerTest, CustomSocketPath) {
  std::unique_ptr<Display> display(new Display);

  base::ScopedTempDir non_xdg_dir;
  ASSERT_TRUE(non_xdg_dir.CreateUniqueTempDir());

  std::unique_ptr<Server> server =
      Server::Create(display.get(), Capabilities::GetDefaultCapabilities(),
                     non_xdg_dir.GetPath().Append("custom-socket"));
  EXPECT_TRUE(server);

  // Check that Create() has put the socket in the directory. Actually two files
  // will be created (the socket and its .lock), so we use StartsWith().
  base::FileEnumerator enumerator(non_xdg_dir.GetPath(), /*recursive=*/false,
                                  base::FileEnumerator::FILES);
  EXPECT_TRUE(base::StartsWith(enumerator.Next().BaseName().MaybeAsASCII(),
                               "custom-socket"));
}

TEST_F(ServerTest, CapabilityAssociation) {
  std::unique_ptr<Capabilities> capabilities =
      Capabilities::GetDefaultCapabilities();
  Capabilities* capability_ptr = capabilities.get();

  Display display;
  Server server(&display, std::move(capabilities));
  server.Initialize();

  EXPECT_EQ(GetCapabilities(server.GetWaylandDisplayForTesting()),
            capability_ptr);
}

TEST_F(ServerTest, CreateAsync) {
  std::unique_ptr<Display> display(new Display);

  base::ScopedTempDir non_xdg_dir;
  ASSERT_TRUE(non_xdg_dir.CreateUniqueTempDir());

  base::RunLoop run_loop;
  testing::MockFunction<void(std::unique_ptr<Server>)> server_callback;
  EXPECT_CALL(server_callback, Call(testing::_))
      .WillOnce(testing::Invoke([&run_loop](std::unique_ptr<Server> server) {
        EXPECT_TRUE(server);
        run_loop.Quit();
      }));

  Server::CreateAsync(
      display.get(), Capabilities::GetDefaultCapabilities(),
      GetUniqueSocketPath(),
      base::BindOnce(
          &testing::MockFunction<void(std::unique_ptr<Server>)>::Call,
          base::Unretained(&server_callback)));
  run_loop.Run();
}

void ConnectToServer(const std::string socket_name,
                     bool* connected_to_server,
                     base::WaitableEvent* event) {
  wl_display* display = wl_display_connect(socket_name.c_str());
  *connected_to_server = !!display;
  event->Signal();
  wl_display_disconnect(display);
}

TEST_F(ServerTest, Dispatch) {
  std::unique_ptr<Display> display(new Display);
  std::unique_ptr<Server> server(
      new Server(display.get(), Capabilities::GetDefaultCapabilities()));
  server->Initialize();

  std::string socket_name = GetUniqueSocketName();
  bool rv = server->AddSocket(socket_name);
  EXPECT_TRUE(rv);

  base::Thread client("client-" + socket_name);
  ASSERT_TRUE(client.Start());

  // Post a task that connects server on the created thread.
  bool connected_to_server = false;
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  client.task_runner()->PostTask(FROM_HERE,
                                 base::BindOnce(&ConnectToServer, socket_name,
                                                &connected_to_server, &event));

  // Call Dispatch() with a 5 second timeout.
  server->Dispatch(base::Seconds(5));

  // Check if client thread managed to connect to server.
  event.Wait();
  EXPECT_TRUE(connected_to_server);
}

TEST_F(ServerTest, Flush) {
  std::unique_ptr<Display> display(new Display);
  std::unique_ptr<Server> server(
      new Server(display.get(), Capabilities::GetDefaultCapabilities()));
  server->Initialize();

  bool rv = server->AddSocket(GetUniqueSocketName());
  EXPECT_TRUE(rv);

  // Just call Flush to check that it doesn't have any bad side-effects.
  server->Flush();
}

}  // namespace
}  // namespace wayland
}  // namespace exo
