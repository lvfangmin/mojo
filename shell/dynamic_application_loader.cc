// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/dynamic_application_loader.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/format_macros.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/process/process.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "crypto/secure_hash.h"
#include "crypto/sha2.h"
#include "mojo/common/common_type_converters.h"
#include "mojo/common/data_pipe_utils.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "mojo/services/network/public/interfaces/url_loader.mojom.h"
#include "shell/context.h"
#include "shell/data_pipe_peek.h"
#include "shell/filename_util.h"
#include "shell/switches.h"
#include "url/url_util.h"

namespace mojo {
namespace shell {

namespace {

static const char kMojoMagic[] = "#!mojo ";
static const size_t kMaxShebangLength = 2048;

void IgnoreResult(bool result) {
}

}  // namespace

// Encapsulates loading and running one individual application.
//
// Loaders are owned by DynamicApplicationLoader. DynamicApplicationLoader must
// ensure that all the parameters passed to Loader subclasses stay valid through
// Loader's lifetime.
//
// Async operations are done with WeakPtr to protect against
// DynamicApplicationLoader going away (and taking all the Loaders with it)
// while the async operation is outstanding.
class DynamicApplicationLoader::Loader {
 public:
  Loader(DynamicServiceRunner::CleanupBehavior cleanup_behavior,
         MimeTypeToURLMap* mime_type_to_url,
         Context* context,
         DynamicServiceRunnerFactory* runner_factory,
         InterfaceRequest<Application> application_request,
         ApplicationLoader::LoadCallback load_callback,
         const LoaderCompleteCallback& loader_complete_callback)
      : cleanup_behavior_(cleanup_behavior),
        application_request_(application_request.Pass()),
        load_callback_(load_callback),
        loader_complete_callback_(loader_complete_callback),
        context_(context),
        mime_type_to_url_(mime_type_to_url),
        runner_factory_(runner_factory),
        weak_ptr_factory_(this) {}

  virtual ~Loader() {}

 protected:
  virtual URLResponsePtr AsURLResponse(base::TaskRunner* task_runner,
                                       uint32_t skip) = 0;

  virtual void AsPath(
      base::TaskRunner* task_runner,
      base::Callback<void(const base::FilePath&, bool)> callback) = 0;

  virtual std::string MimeType() = 0;

  virtual bool HasMojoMagic() = 0;

  virtual bool PeekFirstLine(std::string* line) = 0;

  void Load() {
    // If the response begins with a #!mojo <content-handler-url>, use it.
    GURL url;
    std::string shebang;
    if (PeekContentHandler(&shebang, &url)) {
      load_callback_.Run(
          url, application_request_.Pass(),
          AsURLResponse(context_->task_runners()->blocking_pool(),
                        static_cast<int>(shebang.size())));
      return;
    }

    MimeTypeToURLMap::iterator iter = mime_type_to_url_->find(MimeType());
    if (iter != mime_type_to_url_->end()) {
      load_callback_.Run(
          iter->second, application_request_.Pass(),
          AsURLResponse(context_->task_runners()->blocking_pool(), 0));
      return;
    }

    // TODO(aa): Sanity check that the thing we got looks vaguely like a mojo
    // application. That could either mean looking for the platform-specific dll
    // header, or looking for some specific mojo signature prepended to the
    // library.

    AsPath(context_->task_runners()->blocking_pool(),
           base::Bind(&Loader::RunLibrary, weak_ptr_factory_.GetWeakPtr()));
  }

  void ReportComplete() { loader_complete_callback_.Run(this); }

 private:
  bool PeekContentHandler(std::string* mojo_shebang,
                          GURL* mojo_content_handler_url) {
    std::string shebang;
    if (HasMojoMagic() && PeekFirstLine(&shebang)) {
      GURL url(shebang.substr(arraysize(kMojoMagic) - 1, std::string::npos));
      if (url.is_valid()) {
        *mojo_shebang = shebang;
        *mojo_content_handler_url = url;
        return true;
      }
    }
    return false;
  }

  void RunLibrary(const base::FilePath& path, bool path_exists) {
    DCHECK(application_request_.is_pending());

    if (!path_exists) {
      LOG(ERROR) << "Library not started because library path '" << path.value()
                 << "' does not exist.";
      ReportComplete();
      return;
    }

    runner_ = runner_factory_->Create(context_);
    runner_->Start(
        path, cleanup_behavior_, application_request_.Pass(),
        base::Bind(&Loader::ReportComplete, weak_ptr_factory_.GetWeakPtr()));
  }

  DynamicServiceRunner::CleanupBehavior cleanup_behavior_;
  InterfaceRequest<Application> application_request_;
  ApplicationLoader::LoadCallback load_callback_;
  LoaderCompleteCallback loader_complete_callback_;
  Context* context_;
  MimeTypeToURLMap* mime_type_to_url_;
  DynamicServiceRunnerFactory* runner_factory_;
  scoped_ptr<DynamicServiceRunner> runner_;
  base::WeakPtrFactory<Loader> weak_ptr_factory_;
};

// A loader for local files.
class DynamicApplicationLoader::LocalLoader : public Loader {
 public:
  LocalLoader(const GURL& url,
              MimeTypeToURLMap* mime_type_to_url,
              Context* context,
              DynamicServiceRunnerFactory* runner_factory,
              InterfaceRequest<Application> application_request,
              ApplicationLoader::LoadCallback load_callback,
              const LoaderCompleteCallback& loader_complete_callback)
      : Loader(DynamicServiceRunner::DontDeleteAppPath,
               mime_type_to_url,
               context,
               runner_factory,
               application_request.Pass(),
               load_callback,
               loader_complete_callback),
        url_(url),
        path_(UrlToFile(url)) {
    Load();
  }

 private:
  static base::FilePath UrlToFile(const GURL& url) {
    DCHECK(url.SchemeIsFile());
    url::RawCanonOutputW<1024> output;
    url::DecodeURLEscapeSequences(
        url.path().data(), static_cast<int>(url.path().length()), &output);
    base::string16 decoded_path =
        base::string16(output.data(), output.length());
#if defined(OS_WIN)
    base::TrimString(decoded_path, L"/", &decoded_path);
    base::FilePath path(decoded_path);
#else
    base::FilePath path(base::UTF16ToUTF8(decoded_path));
#endif
    return path;
  }

  URLResponsePtr AsURLResponse(base::TaskRunner* task_runner,
                               uint32_t skip) override {
    URLResponsePtr response(URLResponse::New());
    response->url = String::From(url_);
    DataPipe data_pipe;
    response->body = data_pipe.consumer_handle.Pass();
    int64 file_size;
    if (base::GetFileSize(path_, &file_size)) {
      response->headers = Array<String>(1);
      response->headers[0] =
          base::StringPrintf("Content-Length: %" PRId64, file_size);
    }
    common::CopyFromFile(path_, data_pipe.producer_handle.Pass(), skip,
                         task_runner, base::Bind(&IgnoreResult));
    return response.Pass();
  }

  void AsPath(
      base::TaskRunner* task_runner,
      base::Callback<void(const base::FilePath&, bool)> callback) override {
    // Async for consistency with network case.
    base::MessageLoop::current()->PostTask(
        FROM_HERE, base::Bind(callback, path_, base::PathExists(path_)));
  }

  std::string MimeType() override { return ""; }

  bool HasMojoMagic() override {
    std::string magic;
    ReadFileToString(path_, &magic, strlen(kMojoMagic));
    return magic == kMojoMagic;
  }

  bool PeekFirstLine(std::string* line) override {
    std::string start_of_file;
    ReadFileToString(path_, &start_of_file, kMaxShebangLength);
    size_t return_position = start_of_file.find('\n');
    if (return_position == std::string::npos)
      return false;
    *line = start_of_file.substr(0, return_position + 1);
    return true;
  }

  GURL url_;
  base::FilePath path_;

  DISALLOW_COPY_AND_ASSIGN(LocalLoader);
};

// A loader for network files.
class DynamicApplicationLoader::NetworkLoader : public Loader {
 public:
  NetworkLoader(const GURL& url,
                NetworkService* network_service,
                MimeTypeToURLMap* mime_type_to_url,
                Context* context,
                DynamicServiceRunnerFactory* runner_factory,
                InterfaceRequest<Application> application_request,
                ApplicationLoader::LoadCallback load_callback,
                const LoaderCompleteCallback& loader_complete_callback)
      : Loader(DynamicServiceRunner::DeleteAppPath,
               mime_type_to_url,
               context,
               runner_factory,
               application_request.Pass(),
               load_callback,
               loader_complete_callback),
        url_(url),
        weak_ptr_factory_(this) {
    StartNetworkRequest(url, network_service);
  }

  ~NetworkLoader() override {
    if (!path_.empty())
      base::DeleteFile(path_, false);
  }

 private:
  // TODO(hansmuller): Revisit this when a real peek operation is available.
  static const MojoDeadline kPeekTimeout = MOJO_DEADLINE_INDEFINITE;

  URLResponsePtr AsURLResponse(base::TaskRunner* task_runner,
                               uint32_t skip) override {
    if (skip != 0) {
      MojoResult result = ReadDataRaw(
          response_->body.get(), nullptr, &skip,
          MOJO_READ_DATA_FLAG_ALL_OR_NONE | MOJO_READ_DATA_FLAG_DISCARD);
      DCHECK_EQ(result, MOJO_RESULT_OK);
    }
    return response_.Pass();
  }

  static void RecordCacheToURLMapping(const base::FilePath& path,
                                      const GURL& url) {
    // This is used to extract symbols on android.
    // TODO(eseidel): All users of this log should move to using the map file.
    LOG(INFO) << "Caching mojo app " << url << " at " << path.value();

    base::FilePath temp_dir;
    base::GetTempDir(&temp_dir);
    base::ProcessId pid = base::Process::Current().Pid();
    std::string map_name = base::StringPrintf("mojo_shell.%d.maps", pid);
    base::FilePath map_path = temp_dir.Append(map_name);

    // TODO(eseidel): Paths or URLs with spaces will need quoting.
    std::string map_entry =
        base::StringPrintf("%s %s\n", path.value().c_str(), url.spec().c_str());
    // TODO(eseidel): AppendToFile is missing O_CREAT, crbug.com/450696
    if (!PathExists(map_path))
      base::WriteFile(map_path, map_entry.data(), map_entry.length());
    else
      base::AppendToFile(map_path, map_entry.data(), map_entry.length());
  }

  // AppIds should be be both predictable and unique, but any hash would work.
  // Currently we use sha256 from crypto/secure_hash.h
  static bool ComputeAppId(const base::FilePath& path,
                           std::string* digest_string) {
    scoped_ptr<crypto::SecureHash> ctx(
        crypto::SecureHash::Create(crypto::SecureHash::SHA256));
    base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
    if (!file.IsValid()) {
      LOG(ERROR) << "Failed to open " << path.value() << " for computing AppId";
      return false;
    }
    char buf[1024];
    while (file.IsValid()) {
      int bytes_read = file.ReadAtCurrentPos(buf, sizeof(buf));
      if (bytes_read == 0)
        break;
      ctx->Update(buf, bytes_read);
    }
    if (!file.IsValid()) {
      LOG(ERROR) << "Error reading " << path.value();
      return false;
    }
    // The output is really a vector of unit8, we're cheating by using a string.
    std::string output(crypto::kSHA256Length, 0);
    ctx->Finish(string_as_array(&output), output.size());
    output = base::HexEncode(output.c_str(), output.size());
    // Using lowercase for compatiblity with sha256sum output.
    *digest_string = base::StringToLowerASCII(output);
    return true;
  }

  static bool RenameToAppId(const base::FilePath& old_path,
                            base::FilePath* new_path) {
    std::string app_id;
    if (!ComputeAppId(old_path, &app_id))
      return false;

    base::FilePath temp_dir;
    base::GetTempDir(&temp_dir);
    std::string unique_name = base::StringPrintf("%s.mojo", app_id.c_str());
    *new_path = temp_dir.Append(unique_name);
    return base::Move(old_path, *new_path);
  }

  void CopyCompleted(base::Callback<void(const base::FilePath&, bool)> callback,
                     bool success) {
    // The copy completed, now move to $TMP/$APP_ID.mojo before the dlopen.
    if (success) {
      success = false;
      base::FilePath new_path;
      if (RenameToAppId(path_, &new_path)) {
        if (base::PathExists(new_path)) {
          path_ = new_path;
          success = true;
          RecordCacheToURLMapping(path_, url_);
        }
      }
    }

    base::MessageLoop::current()->PostTask(
        FROM_HERE, base::Bind(callback, path_, success));
  }

  void AsPath(
      base::TaskRunner* task_runner,
      base::Callback<void(const base::FilePath&, bool)> callback) override {
    if (!path_.empty() || !response_) {
      base::MessageLoop::current()->PostTask(
          FROM_HERE, base::Bind(callback, path_, base::PathExists(path_)));
      return;
    }

    base::CreateTemporaryFile(&path_);
    common::CopyToFile(response_->body.Pass(), path_, task_runner,
                       base::Bind(&NetworkLoader::CopyCompleted,
                                  weak_ptr_factory_.GetWeakPtr(), callback));
  }

  std::string MimeType() override {
    DCHECK(response_);
    return response_->mime_type;
  }

  bool HasMojoMagic() override {
    std::string magic;
    return BlockingPeekNBytes(response_->body.get(), &magic, strlen(kMojoMagic),
                              kPeekTimeout) &&
           magic == kMojoMagic;
  }

  bool PeekFirstLine(std::string* line) override {
    return BlockingPeekLine(response_->body.get(), line, kMaxShebangLength,
                            kPeekTimeout);
  }

  void StartNetworkRequest(const GURL& url, NetworkService* network_service) {
    URLRequestPtr request(URLRequest::New());
    request->url = String::From(url);
    request->auto_follow_redirects = true;

    if (base::CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kDisableCache)) {
      request->bypass_cache = true;
    }

    network_service->CreateURLLoader(GetProxy(&url_loader_));
    url_loader_->Start(request.Pass(),
                       base::Bind(&NetworkLoader::OnLoadComplete,
                                  weak_ptr_factory_.GetWeakPtr()));
  }

  void OnLoadComplete(URLResponsePtr response) {
    if (response->error) {
      LOG(ERROR) << "Error (" << response->error->code << ": "
                 << response->error->description << ") while fetching "
                 << response->url;
      ReportComplete();
      return;
    }
    response_ = response.Pass();
    Load();
  }

  const GURL url_;
  URLLoaderPtr url_loader_;
  URLResponsePtr response_;
  base::FilePath path_;
  base::WeakPtrFactory<NetworkLoader> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(NetworkLoader);
};

DynamicApplicationLoader::DynamicApplicationLoader(
    Context* context,
    scoped_ptr<DynamicServiceRunnerFactory> runner_factory)
    : context_(context),
      runner_factory_(runner_factory.Pass()),

      // Unretained() is correct here because DynamicApplicationLoader owns the
      // loaders that we pass this callback to.
      loader_complete_callback_(
          base::Bind(&DynamicApplicationLoader::LoaderComplete,
                     base::Unretained(this))) {
}

DynamicApplicationLoader::~DynamicApplicationLoader() {
}

void DynamicApplicationLoader::RegisterContentHandler(
    const std::string& mime_type,
    const GURL& content_handler_url) {
  DCHECK(content_handler_url.is_valid())
      << "Content handler URL is invalid for mime type " << mime_type;
  mime_type_to_url_[mime_type] = content_handler_url;
}

void DynamicApplicationLoader::Load(
    ApplicationManager* manager,
    const GURL& url,
    InterfaceRequest<Application> application_request,
    LoadCallback load_callback) {
  if (url.SchemeIsFile()) {
    loaders_.push_back(new LocalLoader(
        url, &mime_type_to_url_, context_, runner_factory_.get(),
        application_request.Pass(), load_callback, loader_complete_callback_));
    return;
  }

  if (!network_service_) {
    context_->application_manager()->ConnectToService(
        GURL("mojo:network_service"), &network_service_);
  }

  loaders_.push_back(new NetworkLoader(
      url, network_service_.get(), &mime_type_to_url_, context_,
      runner_factory_.get(), application_request.Pass(), load_callback,
      loader_complete_callback_));
}

void DynamicApplicationLoader::OnApplicationError(ApplicationManager* manager,
                                                  const GURL& url) {
  // TODO(darin): What should we do about service errors? This implies that
  // the app closed its handle to the service manager. Maybe we don't care?
}

void DynamicApplicationLoader::LoaderComplete(Loader* loader) {
  loaders_.erase(std::find(loaders_.begin(), loaders_.end(), loader));
}

}  // namespace shell
}  // namespace mojo
