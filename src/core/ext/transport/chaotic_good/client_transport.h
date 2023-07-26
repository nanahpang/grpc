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
#include <memory>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace chaotic_good {

class ClientTransport {
 public:
  ClientTransport(const ChannelArgs& channel_args,
                  PromiseEndpoint&& control_endpoint_,
                  PromiseEndpoint&& data_endpoint_);
  ~ClientTransport() {
    std::cout << "\n destruct client transport";
    fflush(stdout);
    if (writer_ != nullptr) {
      std::cout << "\n destruct writer ";
      fflush(stdout);
      writer_.get_deleter();
    }
  }
  auto AddStream(CallArgs call_args) {
    // At this point, the connection is set up.
    // Start sending data frames.
    auto initial_frame = std::make_shared<ClientFragmentFrame>();
    initial_frame->headers = std::move(call_args.client_initial_metadata);
    initial_frame->end_of_stream = false;
    {
      MutexLock lock(&mu_);
      initial_frame->stream_id = next_stream_id_++;
    }
    // const uint32_t stream_id = initial_frame->stream_id;
    // bool reach_end_of_stream = initial_frame->end_of_stream;

    // TODO(): change to Loop() or ForEach() to send all messages in
    // client_to_server_message.
    return ForEach(std::move(*call_args.client_to_server_messages),
                   [this, initial_frame](MessageHandle result) {
                     MpscSender<FrameInterface*> outgoing_frames =
                         this->outgoing_frames_.MakeSender();
                     std::cout << "\n for each get message: "
                               << result->payload()->JoinIntoString();
                     fflush(stdout);
                     initial_frame->message = std::move(result);
                     std::cout << "\n move result length: "
                               << initial_frame->message->payload()->Length();
                     fflush(stdout);
                     // if (result.has_value()) {
                     //     std::cout << "\n for each get message: " <<
                     //     result.value()->payload()->JoinIntoString();
                     //     fflush(stdout);
                     //     initial_frame->message = std::move(*result);
                     //     std::cout << "\n move result length: " <<
                     //     initial_frame->message->payload()->Length();
                     //     fflush(stdout);
                     //     initial_frame->end_of_stream = false;
                     // } else {
                     //     initial_frame->end_of_stream = true;
                     // }
                     // initial_frame.message = std::move(result);
                     // reach_end_of_stream = initial_frame.end_of_stream;
                     // // // ClientFrame cast_frame =
                     // ClientFrame(std::move(initial_frame));
                     return Seq(outgoing_frames.Send(initial_frame.get()), [] {
                       std::cout << "\n outgoing_frames send finish: ";
                       fflush(stdout);
                       absl::SleepFor(absl::Seconds(5));
                       return absl::OkStatus();
                     });
                     // return outgoing_frames.Send(&initial_frame);
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