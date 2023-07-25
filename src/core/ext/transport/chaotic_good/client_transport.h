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
#include <stdio.h>

#include <initializer_list>
#include <iostream>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "frame.h"

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/join.h"
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
    const uint32_t stream_id = initial_frame.stream_id;
    bool reach_end_of_stream = initial_frame.end_of_stream;
    MpscSender<FrameInterface*> outgoing_frames =
        this->outgoing_frames_.MakeSender();
    return Seq(call_args.client_to_server_messages->Next(), 
    [&initial_frame, reach_end_of_stream, &outgoing_frames, this](NextResult<MessageHandle> message) mutable { 
            std::cout << "\n get next message " << message.has_value();
            fflush(stdout);
            // return std::move(*message);
            if (message.has_value()) {
                // initial_frame.message = std::move(message.value());
                initial_frame.end_of_stream = false;
            } else {
                initial_frame.end_of_stream = true;
            }
            reach_end_of_stream = initial_frame.end_of_stream;
            // ClientFrame cast_frame = ClientFrame(std::move(initial_frame));
            return Join(outgoing_frames.Send(&initial_frame), [this](){
                this->writer_->ForceWakeup();
                return absl::OkStatus();
            });
        // return std::move(*message);
        });
  }
    // return Seq(Map(call_args.client_to_server_messages->Next(),
    //     [&initial_frame, reach_end_of_stream, &outgoing_frames](NextResult<MessageHandle> message) mutable {
    //         std::cout << "\n get next message";
    //         if (message.has_value()) {
    //             initial_frame.message = std::move(message.value());
    //             initial_frame.end_of_stream = false;
    //         } else {
    //             initial_frame.end_of_stream = true;
    //         }
    //         reach_end_of_stream = initial_frame.end_of_stream;
    //         return outgoing_frames.Send(&initial_frame);
    //     }),
    //     Loop(Seq(
    //         [reach_end_of_stream, stream_id, &outgoing_frames,
    //          &call_args]() mutable {
    //           // Poll next frame from client_to_server_messages.
    //           ClientFragmentFrame frame;
    //           frame.stream_id = stream_id;
    //           auto next_message = call_args.client_to_server_messages->Next()();
    //           if (next_message.ready()) {
    //             auto message = std::move(next_message.value());
    //             if (message.has_value()) {
    //               frame.message = std::move(message.value());
    //               frame.end_of_stream = false;
    //             } else {
    //               frame.end_of_stream = true;
    //             }
    //           } else {
    //             reach_end_of_stream = true;
    //           }
    //           return outgoing_frames.Send(&frame);
    //         },
    //         [reach_end_of_stream]() -> LoopCtl<absl::Status> {
    //           std::cout << "\n reach end of steram " << reach_end_of_stream;
    //           fflush(stdout);
    //           if (reach_end_of_stream) {
    //             return absl::OkStatus();
    //           }
    //           return Continue();
    //         })));
  

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