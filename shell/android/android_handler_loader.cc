// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/android/android_handler_loader.h"

namespace mojo {
namespace shell {

AndroidHandlerLoader::AndroidHandlerLoader() {
}

AndroidHandlerLoader::~AndroidHandlerLoader() {
}

void AndroidHandlerLoader::Load(
    ApplicationManager* manager,
    const GURL& url,
    InterfaceRequest<Application> application_request,
    LoadCallback callback) {
  DCHECK(application_request.is_pending());
  application_.reset(
      new ApplicationImpl(&android_handler_, application_request.Pass()));
}

void AndroidHandlerLoader::OnApplicationError(ApplicationManager* manager,
                                              const GURL& url) {
}

}  // namespace shell
}  // namespace mojo
