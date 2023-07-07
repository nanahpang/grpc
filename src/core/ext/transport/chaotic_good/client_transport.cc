// Copyright 2022 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chaotic_good/client_transport.h"

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/try_concurrently.h"

namespace grpc_core {
namespace chaotic_good {

ClientTransport::ClientTransport(const ChannelArgs& channel_args, PromiseEndpoint control_ep, PromiseEndpoint data_ep) {
  auto hpack_compressor = std::make_shared<HPackCompressor>();
  auto control_endpoint = std::make_shared<PromiseEndpoint>(std::move(control_ep));
  auto data_endpoint = std::make_shared<PromiseEndpoint>(std::move(data_ep));
  writer_ = MakeActivity(
      ForEach(outgoing_frames_,
              [hpack_compressor, control_endpoint, data_endpoint](ClientFrame frame) {
                auto write_buffer = frame.Serialize(hpack_compressor);
                // TODO(ladynana) split write buffer and write with correct endpoint.
                return control_endpoint->Write(std::move(write_buffer));
              }),
      EventEngineWakeupScheduler(), [] {
        // TODO(ladynana): Figure this out
        abort();
      });
}

}  // namespace chaotic_good
}  // namespace grpc_core