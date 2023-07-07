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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_TRANSPORT_H
#define GRPC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_TRANSPORT_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/promise/mpsc.h"

namespace grpc_core {
namespace chaotic_good {

class ClientTransport {
 public:
  explicit ClientTransport(const ChannelArgs& channel_args, PromiseEndpoint control_endpoint_, PromiseEndpoint data_endpoint_);
  auto AddStream(CallArgs call_args) {
    // At this point, the connection is set up. 
    // Start sending data frames. 
    ClientFragmentFrame initial_frame;
    initial_frame.headers = std::move(call_args.client_initial_metadata);
    initial_frame.end_of_stream = false;
    auto next_message = call_args.client_to_server_messages->Next();
    if (next_message.has_value()) {
      initial_frame.message = std::move(next_message);
      initial_frame.end_of_stream = false;
    } else {
      initial_frame.end_of_stream = true;
    }
    {
      MutexLock lock(&mu_);
      initial_frame.stream_id = next_stream_id_++;
    }
    bool reached_end_of_stream = initial_frame.end_of_stream;
    uint32_t stream_id = initial_frame.stream_id;
    return TryConcurrently(
              // TODO(ladynana): Add read path here.
              Never<ServerMetadataHandle>())
        .Push(
            Seq(outgoing_frames_.Push(std::move(initial_frame)),
                If(reached_end_of_stream, ImmediateOkStatus(),
                  ForEach(std::move(*call_args.client_to_server_messages),
                          [stream_id, this](NextResult<MessageHandle> message) {
                            ClientFragmentFrame frame;
                            frame.stream_id = stream_id;
                            if (message.has_value()) {
                              frame.message = std::move(message);
                              frame.end_of_stream = false;
                            } else {
                              frame.end_of_stream = true;
                            }
                            return outgoing_frames_.Push(std::move(frame));
                          }))));
  }

 private:
  MpscReceiver<ClientFrame> outgoing_frames_;
  Mutex mu_;
  uint32_t next_stream_id_ ABSL_GUARDED_BY(mu_) = 1;
  ActivityPtr writer_;
  ActivityPtr reader_;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_TRANSPORT_H