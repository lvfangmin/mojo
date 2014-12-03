// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/surfaces/surfaces_service_application.h"

#include "cc/surfaces/display.h"
#include "mojo/application/application_runner_chromium.h"
#include "mojo/common/tracing_impl.h"
#include "mojo/public/c/system/main.h"
#include "services/surfaces/surfaces_service_impl.h"

namespace mojo {

SurfacesServiceApplication::SurfacesServiceApplication()
    : tick_interval_(base::TimeDelta::FromMilliseconds(17)),
      next_id_namespace_(1u),
      display_(NULL),
      draw_timer_(false, false) {
}

SurfacesServiceApplication::~SurfacesServiceApplication() {
}

void SurfacesServiceApplication::Initialize(ApplicationImpl* app) {
  TracingImpl::Create(app);
}

bool SurfacesServiceApplication::ConfigureIncomingConnection(
    ApplicationConnection* connection) {
  connection->AddService(this);
  return true;
}

void SurfacesServiceApplication::Create(
    ApplicationConnection* connection,
    InterfaceRequest<SurfacesService> request) {
  new SurfacesServiceImpl(&manager_, &next_id_namespace_, this, request.Pass());
}

void SurfacesServiceApplication::OnVSyncParametersUpdated(
    base::TimeTicks timebase,
    base::TimeDelta interval) {
  tick_interval_ = interval;
}

void SurfacesServiceApplication::FrameSubmitted() {
  if (!draw_timer_.IsRunning() && display_) {
    draw_timer_.Start(FROM_HERE, tick_interval_,
                      base::Bind(base::IgnoreResult(&cc::Display::Draw),
                                 base::Unretained(display_)));
  }
}

void SurfacesServiceApplication::SetDisplay(cc::Display* display) {
  draw_timer_.Stop();
  display_ = display;
}

void SurfacesServiceApplication::OnDisplayBeingDestroyed(cc::Display* display) {
  if (display_ == display)
    SetDisplay(nullptr);
}

}  // namespace mojo

MojoResult MojoMain(MojoHandle shell_handle) {
  mojo::ApplicationRunnerChromium runner(new mojo::SurfacesServiceApplication);
  return runner.Run(shell_handle);
}