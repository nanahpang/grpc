//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef GRPC_SRC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_WINDOWS_H
#define GRPC_SRC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_WINDOWS_H

#include <grpc/support/port_platform.h>

#include <functional>

#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/util/sync.h"

namespace grpc_core {

// A DNS resolver which uses the native platform's getaddrinfo API.
class NativeDNSResolver : public DNSResolver {
 public:
  NativeDNSResolver() = default;

  TaskHandle LookupHostname(
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_resolved,
      absl::string_view name, absl::string_view default_port, Duration timeout,
      grpc_pollset_set* interested_parties,
      absl::string_view name_server) override;

  absl::StatusOr<std::vector<grpc_resolved_address>> LookupHostnameBlocking(
      absl::string_view name, absl::string_view default_port) override;

  TaskHandle LookupSRV(
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_resolved,
      absl::string_view name, Duration timeout,
      grpc_pollset_set* interested_parties,
      absl::string_view name_server) override;

  TaskHandle LookupTXT(
      std::function<void(absl::StatusOr<std::string>)> on_resolved,
      absl::string_view name, Duration timeout,
      grpc_pollset_set* interested_parties,
      absl::string_view name_server) override;

  // NativeDNSResolver does not support cancellation.
  bool Cancel(TaskHandle handle) override;

 private:
  // Lazily instantiate and return a pointer to the owned EventEngine.
  // The engine needs to be lazily instantiated to avoid a mutex reacquisition,
  // because the NativeDNSResolver is created in grpc_init, and creating an
  // EventEngine calls grpc_init.
  grpc_event_engine::experimental::EventEngine* engine();

  Mutex mu_;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine_
      ABSL_GUARDED_BY(mu_);
  std::atomic<grpc_event_engine::experimental::EventEngine*> engine_ptr_{
      nullptr};
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_WINDOWS_H
