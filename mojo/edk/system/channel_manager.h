// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_SYSTEM_CHANNEL_MANAGER_H_
#define MOJO_EDK_SYSTEM_CHANNEL_MANAGER_H_

#include <stdint.h>

#include "base/containers/hash_tables.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "mojo/edk/system/channel.h"
#include "mojo/edk/system/channel_info.h"

namespace mojo {

namespace embedder {
class PlatformSupport;
}

namespace system {

// IDs for |Channel|s managed by a |ChannelManager|. (IDs should be thought of
// as specific to a given |ChannelManager|.) 0 is never a valid ID.
typedef uint64_t ChannelId;

const ChannelId kInvalidChannelId = 0;

// This class manages and "owns" |Channel|s (which typically connect to other
// processes) for a given process. This class is thread-safe, except as
// specifically noted.
class MOJO_SYSTEM_IMPL_EXPORT ChannelManager {
 public:
  // |*platform_support| must remain alive longer than this object.
  explicit ChannelManager(embedder::PlatformSupport* platform_support);
  ~ChannelManager();

  // Creates a |Channel| and adds it to the set of channels managed by this
  // |ChannelManager|. |channel_id| should be a valid |ChannelId| (i.e.,
  // nonzero) not "assigned" to any other |Channel| being managed by this
  // |ChannelManager|.
  // TODO(vtl): Currently, this should be called on any I/O thread (which will
  // become the new channel's "channel thread"). Eventually, the channel manager
  // will have an assigned I/O thread, on which this must be called.
  // TODO(vtl): Maybe this should return a message pipe dispatcher (for the
  // bootstrap message pipe) instead.
  void CreateChannelOnIOThread(
      ChannelId channel_id,
      embedder::ScopedPlatformHandle platform_handle,
      scoped_refptr<system::ChannelEndpoint> bootstrap_channel_endpoint);

  // Gets the |Channel| with the given ID (which must exist).
  scoped_refptr<Channel> GetChannel(ChannelId channel_id) const;

  // Informs the channel manager (and thus channel) that it will be shutdown
  // soon (by calling |ShutdownChannel()|). Calling this is optional (and may in
  // fact be called multiple times) but it will suppress certain warnings (e.g.,
  // for the channel being broken) and enable others (if messages are written to
  // the channel).
  void WillShutdownChannel(ChannelId channel_id);

  // Shuts down the channel specified by the given ID. It is up to the caller to
  // guarantee that this is only called once per channel (that was added using
  // |CreateChannelOnIOThread()|). If called from the channel's creation thread
  // (i.e., |base::MessageLoopProxy::current()| is the channel thread's
  // |TaskRunner|), this will complete synchronously.
  void ShutdownChannel(ChannelId channel_id);

 private:
  embedder::PlatformSupport* const platform_support_;

  // Note: |Channel| methods should not be called under |lock_|.
  mutable base::Lock lock_;  // Protects the members below.

  // TODO(vtl): Once we give the |ChannelManager| one single I/O thread, we can
  // get rid of |ChannelInfo| (and just have ref pointers to |Channel|s).
  base::hash_map<ChannelId, ChannelInfo> channel_infos_;

  DISALLOW_COPY_AND_ASSIGN(ChannelManager);
};

}  // namespace system
}  // namespace mojo

#endif  // MOJO_EDK_SYSTEM_CHANNEL_MANAGER_H_
