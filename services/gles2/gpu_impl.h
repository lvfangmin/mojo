// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_GLES2_GPU_IMPL_H_
#define SERVICES_GLES2_GPU_IMPL_H_

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/threading/thread.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/services/geometry/public/interfaces/geometry.mojom.h"
#include "mojo/services/gpu/public/interfaces/command_buffer.mojom.h"
#include "mojo/services/gpu/public/interfaces/gpu.mojom.h"

namespace gfx {
class GLShareGroup;
}

namespace gpu {
class SyncPointManager;
namespace gles2 {
class MailboxManager;
}
}

namespace gles2 {

class GpuImpl : public mojo::Gpu {
 public:
  // We need to share these across all CommandBuffer instances so that contexts
  // they create can share resources with each other via mailboxes.
  class State : public base::RefCounted<State> {
   public:
    State();

    // We run the CommandBufferImpl on the control_task_runner, which forwards
    // most method class to the CommandBufferDriver, which runs on the "driver",
    // thread (i.e., the thread on which GpuImpl instances are created).
    scoped_refptr<base::SingleThreadTaskRunner> control_task_runner() {
      return control_thread_.task_runner();
    }

    // These objects are intended to be used on the "driver" thread (i.e., the
    // thread on which GpuImpl instances are created).
    gfx::GLShareGroup* share_group() const { return share_group_.get(); }
    gpu::gles2::MailboxManager* mailbox_manager() const {
      return mailbox_manager_.get();
    }
    gpu::SyncPointManager* sync_point_manager() const {
      return sync_point_manager_.get();
    }

   private:
    friend class base::RefCounted<State>;
    ~State();

    base::Thread control_thread_;
    scoped_refptr<gpu::SyncPointManager> sync_point_manager_;
    scoped_refptr<gfx::GLShareGroup> share_group_;
    scoped_refptr<gpu::gles2::MailboxManager> mailbox_manager_;
  };

  GpuImpl(mojo::InterfaceRequest<mojo::Gpu> request,
          const scoped_refptr<State>& state);
  ~GpuImpl() override;

  void CreateOnscreenGLES2Context(
      uint64_t native_viewport_id,
      mojo::SizePtr size,
      mojo::InterfaceRequest<mojo::CommandBuffer> command_buffer_request,
      mojo::ViewportParameterListenerPtr listener) override;

  void CreateOffscreenGLES2Context(mojo::InterfaceRequest<mojo::CommandBuffer>
                                       command_buffer_request) override;

 private:
  mojo::StrongBinding<Gpu> binding_;
  scoped_refptr<State> state_;

  DISALLOW_COPY_AND_ASSIGN(GpuImpl);
};

}  // namespace gles2

#endif  // SERVICES_GLES2_GPU_IMPL_H_
