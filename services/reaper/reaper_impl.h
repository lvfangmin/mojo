// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_REAPER_REAPER_IMPL_H_
#define SERVICES_REAPER_REAPER_IMPL_H_

#include <map>

#include "base/macros.h"
#include "mojo/common/weak_binding_set.h"
#include "mojo/public/cpp/application/application_delegate.h"
#include "mojo/public/cpp/application/interface_factory.h"
#include "mojo/public/cpp/bindings/array.h"
#include "mojo/public/cpp/bindings/callback.h"
#include "services/reaper/diagnostics.mojom.h"
#include "services/reaper/reaper.mojom.h"
#include "url/gurl.h"

namespace mojo {
class ApplicationConnection;
}  // namespace mojo

namespace reaper {

class ReaperImpl : public Diagnostics,
                   public mojo::ApplicationDelegate,
                   public mojo::InterfaceFactory<Reaper>,
                   public mojo::InterfaceFactory<Diagnostics> {
 public:
  typedef uint64 AppSecret;

  ReaperImpl();
  ~ReaperImpl();

  void GetApplicationSecret(const GURL& caller_app,
                            const mojo::Callback<void(AppSecret)>&);
  void CreateReference(const GURL& caller_app,
                       uint32 source_node_id,
                       uint32 target_node_id);
  void DropNode(const GURL& caller_app, uint32 node);
  void StartTransfer(const GURL& caller_app,
                     uint32 node,
                     mojo::InterfaceRequest<Transfer> request);
  void CompleteTransfer(uint32 source_node_id,
                        uint64 dest_app_secret,
                        uint32 dest_node_id);

 private:
  typedef int AppId;

  struct NodeLocator;
  struct NodeInfo;

  AppId GetAppId(const GURL& app_url);
  const GURL& GetAppURLSlowly(const AppId app_id) const;
  bool MoveNode(const NodeLocator& source, const NodeLocator& dest);

  // mojo::ApplicationDelegate
  bool ConfigureIncomingConnection(
      mojo::ApplicationConnection* connection) override;

  // mojo::InterfaceFactory<Reaper>
  void Create(mojo::ApplicationConnection* connection,
              mojo::InterfaceRequest<Reaper> request) override;

  // mojo::InterfaceFactory<Diagnostics>
  void Create(mojo::ApplicationConnection* connection,
              mojo::InterfaceRequest<Diagnostics> request) override;

  // Diagnostics
  void DumpNodes(
      const mojo::Callback<void(mojo::Array<NodePtr>)>& callback) override;
  void Reset() override;
  void GetReaperForApp(const mojo::String& url,
                       mojo::InterfaceRequest<Reaper> request) override;
  void Ping(const mojo::Closure& callback) override;

  GURL reaper_url_;

  // There will be a lot of nodes in a running system, so we intern app urls.
  std::map<GURL, AppId> app_ids_;
  AppId next_app_id_;

  // These are the ids assigned to nodes while they are being transferred.
  AppId next_transfer_id_;

  std::map<AppId, AppSecret> app_id_to_secret_;
  std::map<AppSecret, AppId> app_secret_to_id_;

  std::map<NodeLocator, NodeInfo> nodes_;
  mojo::WeakBindingSet<Diagnostics> diagnostics_bindings_;

  DISALLOW_COPY_AND_ASSIGN(ReaperImpl);
};

#endif  // SERVICES_REAPER_REAPER_IMPL_H_

}  // namespace reaper
