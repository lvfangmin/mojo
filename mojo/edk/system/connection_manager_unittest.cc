// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is really a unit test for |MasterConnectionManager| and
// |SlaveConnectionManager| (since they need to be tested together).

#include "mojo/edk/system/connection_manager.h"

#include <stdint.h>

#include <string>

#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/threading/thread_checker.h"
#include "mojo/edk/embedder/master_process_delegate.h"
#include "mojo/edk/embedder/platform_channel_pair.h"
#include "mojo/edk/embedder/slave_process_delegate.h"
#include "mojo/edk/system/master_connection_manager.h"
#include "mojo/edk/system/slave_connection_manager.h"
#include "mojo/edk/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace system {
namespace {

bool ArePlatformHandlesConnected(const embedder::PlatformHandle& h1,
                                 const embedder::PlatformHandle& h2) {
  const uint32_t w1 = 0xdeadbeef;
  size_t num_bytes = 0;
  if (!mojo::test::BlockingWrite(h1, &w1, sizeof(w1), &num_bytes) ||
      num_bytes != sizeof(w1))
    return false;
  uint32_t r = 0;
  num_bytes = 0;
  if (!mojo::test::BlockingRead(h2, &r, sizeof(r), &num_bytes) ||
      num_bytes != sizeof(r))
    return false;
  if (r != w1)
    return false;

  const uint32_t w2 = 0xfeedface;
  num_bytes = 0;
  if (!mojo::test::BlockingWrite(h1, &w2, sizeof(w2), &num_bytes) ||
      num_bytes != sizeof(w2))
    return false;
  r = 0;
  num_bytes = 0;
  if (!mojo::test::BlockingRead(h2, &r, sizeof(r), &num_bytes) ||
      num_bytes != sizeof(r))
    return false;
  if (r != w2)
    return false;

  return true;
}

bool IsValidSlaveProcessIdentifier(ProcessIdentifier process_identifier) {
  return process_identifier != kInvalidProcessIdentifier &&
         process_identifier != kMasterProcessIdentifier;
}

class TestSlaveInfo : public embedder::SlaveInfo {
 public:
  explicit TestSlaveInfo(const std::string& name) : name_(name) {}
  ~TestSlaveInfo() override { CHECK(thread_checker_.CalledOnValidThread()); }

  const std::string& name() const { return name_; }

 private:
  base::ThreadChecker thread_checker_;
  std::string name_;

  DISALLOW_COPY_AND_ASSIGN(TestSlaveInfo);
};

// Connects the given |slave| (with the given |slave_process_delegate| to the
// given master, creating and using a |TestSlaveInfo| with the given
// |slave_name|.
void ConnectSlave(MasterConnectionManager* master,
                  embedder::SlaveProcessDelegate* slave_process_delegate,
                  SlaveConnectionManager* slave,
                  const std::string& slave_name) {
  embedder::PlatformChannelPair platform_channel_pair;
  master->AddSlave(make_scoped_ptr(new TestSlaveInfo(slave_name)),
                   platform_channel_pair.PassServerHandle());
  slave->Init(slave_process_delegate, platform_channel_pair.PassClientHandle());
}

class MockMasterProcessDelegate : public embedder::MasterProcessDelegate {
 public:
  MockMasterProcessDelegate()
      : current_run_loop_(), on_slave_disconnect_calls_(0) {}
  ~MockMasterProcessDelegate() override {}

  void RunUntilNotified() {
    CHECK(!current_run_loop_);
    base::RunLoop run_loop;
    current_run_loop_ = &run_loop;
    run_loop.Run();
    current_run_loop_ = nullptr;
  }

  unsigned on_slave_disconnect_calls() const {
    return on_slave_disconnect_calls_;
  }
  const std::string& last_slave_disconnect_name() const {
    return last_slave_disconnect_name_;
  }

  // |embedder::MasterProcessDelegate| implementation:
  void OnSlaveDisconnect(scoped_ptr<embedder::SlaveInfo> slave_info) override {
    CHECK(thread_checker_.CalledOnValidThread());
    on_slave_disconnect_calls_++;
    last_slave_disconnect_name_ =
        static_cast<TestSlaveInfo*>(slave_info.get())->name();
    DVLOG(1) << "Disconnected from slave process "
             << last_slave_disconnect_name_;
    slave_info.reset();

    if (current_run_loop_)
      current_run_loop_->Quit();
  }

 private:
  base::ThreadChecker thread_checker_;
  base::RunLoop* current_run_loop_;

  unsigned on_slave_disconnect_calls_;
  std::string last_slave_disconnect_name_;

  DISALLOW_COPY_AND_ASSIGN(MockMasterProcessDelegate);
};

class MockSlaveProcessDelegate : public embedder::SlaveProcessDelegate {
 public:
  MockSlaveProcessDelegate()
      : current_run_loop_(), on_master_disconnect_calls_(0) {}
  ~MockSlaveProcessDelegate() override {}

  void RunUntilNotified() {
    CHECK(!current_run_loop_);
    base::RunLoop run_loop;
    current_run_loop_ = &run_loop;
    run_loop.Run();
    current_run_loop_ = nullptr;
  }

  unsigned on_master_disconnect_calls() const {
    return on_master_disconnect_calls_;
  }

  // |embedder::SlaveProcessDelegate| implementation:
  void OnMasterDisconnect() override {
    CHECK(thread_checker_.CalledOnValidThread());
    on_master_disconnect_calls_++;
    DVLOG(1) << "Disconnected from master process";

    if (current_run_loop_)
      current_run_loop_->Quit();
  }

 private:
  base::ThreadChecker thread_checker_;
  base::RunLoop* current_run_loop_;

  unsigned on_master_disconnect_calls_;

  DISALLOW_COPY_AND_ASSIGN(MockSlaveProcessDelegate);
};

class ConnectionManagerTest : public testing::Test {
 protected:
  ConnectionManagerTest() {}
  ~ConnectionManagerTest() override {}

  base::MessageLoop& message_loop() { return message_loop_; }
  MockMasterProcessDelegate& master_process_delegate() {
    return master_process_delegate_;
  }

 private:
  base::MessageLoop message_loop_;
  MockMasterProcessDelegate master_process_delegate_;

  DISALLOW_COPY_AND_ASSIGN(ConnectionManagerTest);
};

TEST_F(ConnectionManagerTest, BasicConnectSlaves) {
  MasterConnectionManager master;
  master.Init(&master_process_delegate());

  MockSlaveProcessDelegate slave1_process_delegate;
  SlaveConnectionManager slave1;
  ConnectSlave(&master, &slave1_process_delegate, &slave1, "slave1");

  MockSlaveProcessDelegate slave2_process_delegate;
  SlaveConnectionManager slave2;
  ConnectSlave(&master, &slave2_process_delegate, &slave2, "slave2");

  ConnectionIdentifier connection_id = ConnectionIdentifier::Generate();
  EXPECT_TRUE(slave1.AllowConnect(connection_id));
  EXPECT_TRUE(slave2.AllowConnect(connection_id));

  ProcessIdentifier peer1 = kInvalidProcessIdentifier;
  embedder::ScopedPlatformHandle h1;
  EXPECT_TRUE(slave1.Connect(connection_id, &peer1, &h1));
  EXPECT_TRUE(IsValidSlaveProcessIdentifier(peer1));
  EXPECT_TRUE(h1.is_valid());
  ProcessIdentifier peer2 = kInvalidProcessIdentifier;
  embedder::ScopedPlatformHandle h2;
  EXPECT_TRUE(slave2.Connect(connection_id, &peer2, &h2));
  EXPECT_TRUE(IsValidSlaveProcessIdentifier(peer2));
  EXPECT_TRUE(h2.is_valid());

  // TODO(vtl): If/when I add the ability to get one's own process identifier,
  // there'll be more we can check.
  EXPECT_NE(peer1, peer2);
  EXPECT_TRUE(ArePlatformHandlesConnected(h1.get(), h2.get()));

  // The process manager shouldn't have gotten any notifications yet. (Spin the
  // message loop to make sure none were enqueued.)
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0u, master_process_delegate().on_slave_disconnect_calls());

  slave1.Shutdown();

  // |OnSlaveDisconnect()| should be called once.
  master_process_delegate().RunUntilNotified();
  EXPECT_EQ(1u, master_process_delegate().on_slave_disconnect_calls());
  EXPECT_EQ("slave1", master_process_delegate().last_slave_disconnect_name());

  slave2.Shutdown();

  // |OnSlaveDisconnect()| should be called again.
  master_process_delegate().RunUntilNotified();
  EXPECT_EQ(2u, master_process_delegate().on_slave_disconnect_calls());
  EXPECT_EQ("slave2", master_process_delegate().last_slave_disconnect_name());

  master.Shutdown();

  // None of the above should result in |OnMasterDisconnect()| being called.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0u, slave1_process_delegate.on_master_disconnect_calls());
  EXPECT_EQ(0u, slave2_process_delegate.on_master_disconnect_calls());
}

TEST_F(ConnectionManagerTest, ShutdownMasterBeforeSlave) {
  MasterConnectionManager master;
  master.Init(&master_process_delegate());

  MockSlaveProcessDelegate slave_process_delegate;
  SlaveConnectionManager slave;
  ConnectSlave(&master, &slave_process_delegate, &slave, "slave");

  // The process manager shouldn't have gotten any notifications yet. (Spin the
  // message loop to make sure none were enqueued.)
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0u, master_process_delegate().on_slave_disconnect_calls());

  master.Shutdown();

  // |OnSlaveDisconnect()| should be called.
  master_process_delegate().RunUntilNotified();
  EXPECT_EQ(1u, master_process_delegate().on_slave_disconnect_calls());
  EXPECT_EQ("slave", master_process_delegate().last_slave_disconnect_name());

  // |OnMasterDisconnect()| should also be (or have been) called.
  slave_process_delegate.RunUntilNotified();
  EXPECT_EQ(1u, slave_process_delegate.on_master_disconnect_calls());

  slave.Shutdown();
}

TEST_F(ConnectionManagerTest, SlaveCancelConnect) {
  MasterConnectionManager master;
  master.Init(&master_process_delegate());

  MockSlaveProcessDelegate slave1_process_delegate;
  SlaveConnectionManager slave1;
  ConnectSlave(&master, &slave1_process_delegate, &slave1, "slave1");

  MockSlaveProcessDelegate slave2_process_delegate;
  SlaveConnectionManager slave2;
  ConnectSlave(&master, &slave2_process_delegate, &slave2, "slave2");

  ConnectionIdentifier connection_id = ConnectionIdentifier::Generate();
  EXPECT_TRUE(slave1.AllowConnect(connection_id));
  EXPECT_TRUE(slave2.AllowConnect(connection_id));

  EXPECT_TRUE(slave1.CancelConnect(connection_id));
  ProcessIdentifier peer2 = kInvalidProcessIdentifier;
  embedder::ScopedPlatformHandle h2;
  EXPECT_FALSE(slave2.Connect(connection_id, &peer2, &h2));
  EXPECT_EQ(kInvalidProcessIdentifier, peer2);
  EXPECT_FALSE(h2.is_valid());

  slave1.Shutdown();
  slave2.Shutdown();
  master.Shutdown();
}

// Tests that pending connections are removed on error.
TEST_F(ConnectionManagerTest, ErrorRemovePending) {
  MasterConnectionManager master;
  master.Init(&master_process_delegate());

  MockSlaveProcessDelegate slave1_process_delegate;
  SlaveConnectionManager slave1;
  ConnectSlave(&master, &slave1_process_delegate, &slave1, "slave1");

  MockSlaveProcessDelegate slave2_process_delegate;
  SlaveConnectionManager slave2;
  ConnectSlave(&master, &slave2_process_delegate, &slave2, "slave2");

  ConnectionIdentifier connection_id = ConnectionIdentifier::Generate();
  EXPECT_TRUE(slave1.AllowConnect(connection_id));
  EXPECT_TRUE(slave2.AllowConnect(connection_id));

  slave1.Shutdown();

  // |OnSlaveDisconnect()| should be called. After it's called, this means that
  // the disconnect has been detected and handled, including the removal of the
  // pending connection.
  master_process_delegate().RunUntilNotified();
  EXPECT_EQ(1u, master_process_delegate().on_slave_disconnect_calls());

  ProcessIdentifier peer2 = kInvalidProcessIdentifier;
  embedder::ScopedPlatformHandle h2;
  EXPECT_FALSE(slave2.Connect(connection_id, &peer2, &h2));
  EXPECT_EQ(kInvalidProcessIdentifier, peer2);
  EXPECT_FALSE(h2.is_valid());

  slave2.Shutdown();
  master.Shutdown();
}

TEST_F(ConnectionManagerTest, ConnectSlaveToSelf) {
  MasterConnectionManager master;
  master.Init(&master_process_delegate());

  MockSlaveProcessDelegate slave_process_delegate;
  SlaveConnectionManager slave;
  ConnectSlave(&master, &slave_process_delegate, &slave, "slave");

  ConnectionIdentifier connection_id = ConnectionIdentifier::Generate();
  EXPECT_TRUE(slave.AllowConnect(connection_id));
  EXPECT_TRUE(slave.AllowConnect(connection_id));

  // Currently, the connect-to-self case is signalled by the master not sending
  // back a handle.
  ProcessIdentifier peer1 = kInvalidProcessIdentifier;
  embedder::ScopedPlatformHandle h1;
  EXPECT_TRUE(slave.Connect(connection_id, &peer1, &h1));
  EXPECT_TRUE(IsValidSlaveProcessIdentifier(peer1));
  EXPECT_FALSE(h1.is_valid());
  ProcessIdentifier peer2 = kInvalidProcessIdentifier;
  embedder::ScopedPlatformHandle h2;
  EXPECT_TRUE(slave.Connect(connection_id, &peer2, &h2));
  EXPECT_TRUE(IsValidSlaveProcessIdentifier(peer2));
  EXPECT_FALSE(h2.is_valid());

  EXPECT_EQ(peer1, peer2);

  slave.Shutdown();
  master.Shutdown();
}

TEST_F(ConnectionManagerTest, ConnectSlavesTwice) {
  MasterConnectionManager master;
  master.Init(&master_process_delegate());

  MockSlaveProcessDelegate slave1_process_delegate;
  SlaveConnectionManager slave1;
  ConnectSlave(&master, &slave1_process_delegate, &slave1, "slave1");

  MockSlaveProcessDelegate slave2_process_delegate;
  SlaveConnectionManager slave2;
  ConnectSlave(&master, &slave2_process_delegate, &slave2, "slave2");

  ConnectionIdentifier connection_id = ConnectionIdentifier::Generate();
  EXPECT_TRUE(slave1.AllowConnect(connection_id));
  EXPECT_TRUE(slave2.AllowConnect(connection_id));

  ProcessIdentifier peer1 = kInvalidProcessIdentifier;
  embedder::ScopedPlatformHandle h1;
  EXPECT_TRUE(slave1.Connect(connection_id, &peer1, &h1));
  ProcessIdentifier peer2 = kInvalidProcessIdentifier;
  embedder::ScopedPlatformHandle h2;
  EXPECT_TRUE(slave2.Connect(connection_id, &peer2, &h2));

  EXPECT_NE(peer1, peer2);
  EXPECT_TRUE(ArePlatformHandlesConnected(h1.get(), h2.get()));

  // Currently, the master doesn't detect the case of connecting a pair of
  // slaves that are already connected. (Doing so would require more careful
  // tracking and is prone to races -- especially if we want slaves to be able
  // to tear down no-longer-needed connections.) But the slaves should be able
  // to do the tracking themselves (using the peer process identifiers).
  connection_id = ConnectionIdentifier::Generate();
  EXPECT_TRUE(slave1.AllowConnect(connection_id));
  EXPECT_TRUE(slave2.AllowConnect(connection_id));

  h1.reset();
  h2.reset();
  ProcessIdentifier second_peer2 = kInvalidProcessIdentifier;
  EXPECT_TRUE(slave2.Connect(connection_id, &second_peer2, &h2));
  ProcessIdentifier second_peer1 = kInvalidProcessIdentifier;
  EXPECT_TRUE(slave1.Connect(connection_id, &second_peer1, &h1));

  EXPECT_EQ(peer1, second_peer1);
  EXPECT_EQ(peer2, second_peer2);
  EXPECT_TRUE(ArePlatformHandlesConnected(h1.get(), h2.get()));

  slave2.Shutdown();
  slave1.Shutdown();
  master.Shutdown();
}

TEST_F(ConnectionManagerTest, ConnectMasterToSlave) {
  MasterConnectionManager master;
  master.Init(&master_process_delegate());

  MockSlaveProcessDelegate slave_process_delegate;
  SlaveConnectionManager slave;
  ConnectSlave(&master, &slave_process_delegate, &slave, "slave");

  ConnectionIdentifier connection_id = ConnectionIdentifier::Generate();
  EXPECT_TRUE(master.AllowConnect(connection_id));
  EXPECT_TRUE(slave.AllowConnect(connection_id));

  ProcessIdentifier master_peer = kInvalidProcessIdentifier;
  embedder::ScopedPlatformHandle master_h;
  EXPECT_TRUE(master.Connect(connection_id, &master_peer, &master_h));
  EXPECT_TRUE(IsValidSlaveProcessIdentifier(master_peer));
  EXPECT_TRUE(master_h.is_valid());
  ProcessIdentifier slave_peer = kInvalidProcessIdentifier;
  embedder::ScopedPlatformHandle slave_h;
  EXPECT_TRUE(slave.Connect(connection_id, &slave_peer, &slave_h));
  EXPECT_EQ(kMasterProcessIdentifier, slave_peer);
  EXPECT_TRUE(slave_h.is_valid());

  EXPECT_NE(master_peer, slave_peer);
  EXPECT_TRUE(ArePlatformHandlesConnected(master_h.get(), slave_h.get()));

  slave.Shutdown();
  master.Shutdown();
}

TEST_F(ConnectionManagerTest, ConnectMasterToSelf) {
  MasterConnectionManager master;
  master.Init(&master_process_delegate());

  ConnectionIdentifier connection_id = ConnectionIdentifier::Generate();
  EXPECT_TRUE(master.AllowConnect(connection_id));
  EXPECT_TRUE(master.AllowConnect(connection_id));

  // Currently, the connect-to-self case is signalled by the master not sending
  // back a handle.
  ProcessIdentifier peer1 = kInvalidProcessIdentifier;
  embedder::ScopedPlatformHandle h1;
  EXPECT_TRUE(master.Connect(connection_id, &peer1, &h1));
  EXPECT_EQ(kMasterProcessIdentifier, peer1);
  EXPECT_FALSE(h1.is_valid());
  ProcessIdentifier peer2 = kInvalidProcessIdentifier;
  embedder::ScopedPlatformHandle h2;
  EXPECT_TRUE(master.Connect(connection_id, &peer2, &h2));
  EXPECT_EQ(kMasterProcessIdentifier, peer2);
  EXPECT_FALSE(h2.is_valid());

  EXPECT_EQ(peer1, peer2);

  master.Shutdown();
}

TEST_F(ConnectionManagerTest, MasterCancelConnect) {
  MasterConnectionManager master;
  master.Init(&master_process_delegate());

  MockSlaveProcessDelegate slave_process_delegate;
  SlaveConnectionManager slave;
  ConnectSlave(&master, &slave_process_delegate, &slave, "slave");

  ConnectionIdentifier connection_id = ConnectionIdentifier::Generate();
  EXPECT_TRUE(master.AllowConnect(connection_id));
  EXPECT_TRUE(slave.AllowConnect(connection_id));

  EXPECT_TRUE(master.CancelConnect(connection_id));
  ProcessIdentifier peer = kInvalidProcessIdentifier;
  embedder::ScopedPlatformHandle h;
  EXPECT_FALSE(slave.Connect(connection_id, &peer, &h));
  EXPECT_EQ(kInvalidProcessIdentifier, peer);
  EXPECT_FALSE(h.is_valid());

  slave.Shutdown();
  master.Shutdown();
}

TEST_F(ConnectionManagerTest, AddSlaveThenImmediateShutdown) {
  MasterConnectionManager master;
  master.Init(&master_process_delegate());

  MockSlaveProcessDelegate slave_process_delegate;
  SlaveConnectionManager slave;
  embedder::PlatformChannelPair platform_channel_pair;
  master.AddSlave(make_scoped_ptr(new TestSlaveInfo("slave")),
                  platform_channel_pair.PassServerHandle());
  master.Shutdown();
  // Since we never initialized |slave|, we don't have to shut it down.
}

}  // namespace
}  // namespace system
}  // namespace mojo
