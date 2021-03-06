// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_TEST_SERVICE_TEST_TIME_SERVICE_IMPL_H_
#define SERVICES_TEST_SERVICE_TEST_TIME_SERVICE_IMPL_H_

#include "base/memory/scoped_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/system/macros.h"
#include "services/test_service/test_service.mojom.h"

namespace mojo {

class ApplicationConnection;

namespace test {

class TestRequestTrackerClientImpl;

class TestTimeServiceImpl : public TestTimeService {
 public:
  TestTimeServiceImpl(ApplicationConnection* application,
                      InterfaceRequest<TestTimeService> request);
  ~TestTimeServiceImpl() override;

  // |TestTimeService| methods:
  void GetPartyTime(
      const mojo::Callback<void(int64_t time_usec)>& callback) override;
  void StartTrackingRequests(const mojo::Callback<void()>& callback) override;

 private:
  ApplicationConnection* application_;
  scoped_ptr<TestRequestTrackerClientImpl> tracking_;
  StrongBinding<TestTimeService> binding_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(TestTimeServiceImpl);
};

}  // namespace test
}  // namespace mojo

#endif  // SERVICES_TEST_SERVICE_TEST_TIME_SERVICE_IMPL_H_
