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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_TRANSPORT_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <initializer_list>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace chaotic_good {

class ClientTransport {
 public:
  ClientTransport(const ChannelArgs& channel_args,
                  PromiseEndpoint control_endpoint_,
                  PromiseEndpoint data_endpoint_);
  auto AddStream(CallArgs call_args) {
    // At this point, the connection is set up.
    // Start sending data frames.
    ClientFragmentFrame initial_frame;
    initial_frame.headers = std::move(call_args.client_initial_metadata);
    initial_frame.end_of_stream = false;
    {
      MutexLock lock(&mu_);
      initial_frame.stream_id = next_stream_id_++;
    }
    uint32_t stream_id = initial_frame.stream_id;
    MpscSender<FrameInterface*> outgoing_frames =
        this->outgoing_frames_.MakeSender();
    outgoing_frames.Send(dynamic_cast<FrameInterface*>(&initial_frame));
    return Loop([&call_args, &stream_id, this]() -> LoopCtl<absl::Status> {
      if (call_args.client_to_server_messages != nullptr) {
        ClientFragmentFrame frame;
        frame.stream_id = stream_id;
        MpscSender<FrameInterface*> outgoing_frames =
            this->outgoing_frames_.MakeSender();
        auto next_message = call_args.client_to_server_messages->Next()();
        auto message = std::move(next_message.value());
        if (message.has_value()) {
          frame.message = std::move(message.value());
          frame.end_of_stream = false;
        } else {
          frame.end_of_stream = true;
        }
        bool reached_end_of_stream = frame.end_of_stream;
        if (reached_end_of_stream) {
          return absl::OkStatus();
        }
        outgoing_frames.Send(dynamic_cast<FrameInterface*>(&frame));
        return Continue();
      }
      return absl::OkStatus();
    });
  }

 private:
  // Max buffer is set to 4, so that for stream writes each time it will queue
  // at most 2 frames.
  MpscReceiver<FrameInterface*> outgoing_frames_ =
      MpscReceiver<FrameInterface*>(4);
  Mutex mu_;
  uint32_t next_stream_id_ ABSL_GUARDED_BY(mu_) = 1;
  ActivityPtr writer_;
  ActivityPtr reader_;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_TRANSPORT_H