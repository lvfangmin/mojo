// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/shell_test_base.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "jni/ShellTestBase_jni.h"
#include "shell/filename_util.h"
#include "url/gurl.h"

namespace mojo {
namespace shell {
namespace test {

namespace {

JNIEnv* InitEnv() {
  JNIEnv* env = base::android::AttachCurrentThread();
  static bool initialized = false;
  if (!initialized) {
    RegisterNativesImpl(env);
    initialized = true;
  }
  return env;
}

}  // namespace

void ShellTestBase::SetUpTestApplications() {
  // Extract mojo applications, and set the resolve base URL to the directory
  // containing those.
  JNIEnv* env = InitEnv();
  base::android::ScopedJavaLocalRef<jstring> service_dir(
      Java_ShellTestBase_extractMojoApplications(
          env, base::android::GetApplicationContext()));
  shell_context_.mojo_url_resolver()->SetBaseURL(
      FilePathToFileURL(base::FilePath(
          base::android::ConvertJavaStringToUTF8(env, service_dir.obj()))));
}

}  // namespace test
}  // namespace shell
}  // namespace mojo
