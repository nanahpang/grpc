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

#include <memory>
#include <string>
#include <tuple>

#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/detail/basic_join.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {

ClientTransport::ClientTransport(
    const ChannelArgs& channel_args,
    std::unique_ptr<PromiseEndpoint> control_endpoint,
    std::unique_ptr<PromiseEndpoint> data_endpoint)
    : control_endpoint_(std::move(control_endpoint)),
      data_endpoint_(std::move(data_endpoint)) {
  auto hpack_compressor = std::make_shared<HPackCompressor>();
  writer_ = MakeActivity(
      Loop([hpack_compressor, this]() mutable {
        return Seq(this->outgoing_frames_->Next(), [hpack_compressor,
                                                    this](ClientFrame
                                                              client_frame) {
          auto control_endpoint_buffer = std::make_shared<SliceBuffer>();
          auto data_endpoint_buffer = std::make_shared<SliceBuffer>();
          MatchMutable(
              &client_frame,
              [hpack_compressor,
               control_endpoint_buffer = control_endpoint_buffer.get(),
               data_endpoint_buffer = data_endpoint_buffer.get()](
                  ClientFragmentFrame* frame) mutable {
                control_endpoint_buffer->Append(
                    frame->Serialize(hpack_compressor.get()));
                FrameHeader frame_header =
                    FrameHeader::Parse(
                        reinterpret_cast<const uint8_t*>(grpc_slice_to_c_string(
                            control_endpoint_buffer->c_slice_buffer()
                                ->slices[0])))
                        .value();
                std::string message_padding(frame_header.message_padding, '0');
                Slice slice(grpc_slice_from_cpp_string(message_padding));
                // Append message payload to data_endpoint_buffer.
                data_endpoint_buffer->Append(std::move(slice));
                // Append message payload to data_endpoint_buffer.
                frame->message->payload()->MoveFirstNBytesIntoSliceBuffer(
                    frame->message->payload()->Length(), *data_endpoint_buffer);
              },
              [hpack_compressor,
               control_endpoint_buffer =
                   control_endpoint_buffer.get()](CancelFrame* frame) mutable {
                control_endpoint_buffer->Append(
                    frame->Serialize(hpack_compressor.get()));
              });
          return Seq(Join(this->control_endpoint_->Write(
                              std::move(*control_endpoint_buffer)),
                          this->data_endpoint_->Write(
                              std::move(*data_endpoint_buffer))),
                     [](std::tuple<absl::Status, absl::Status> ret)
                         -> LoopCtl<absl::Status> {
                       std::cout << "\n get next frame. ";
                       fflush(stdout);
                       if (!(std::get<0>(ret).ok() || std::get<1>(ret).ok())) {
                         // TODO(ladynana): better error handling when
                         // writes failed.

                         return absl::InternalError("Endpoint Write failed.");
                       }
                       return Continue();
                     });
        });
      }),
      EventEngineWakeupScheduler(
          grpc_event_engine::experimental::CreateEventEngine()),
      [](absl::Status status) -> absl::Status { return status; });
}

}  // namespace chaotic_good
}  // namespace grpc_core
