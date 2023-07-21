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
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/detail/basic_join.h"
#include "src/core/lib/promise/detail/basic_seq.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {

ClientTransport::ClientTransport(const ChannelArgs& channel_args,
                                 PromiseEndpoint control_endpoint,
                                 PromiseEndpoint data_endpoint) {
  HPackCompressor hpack_compressor;
  writer_ = MakeActivity(
      Loop(Seq(
          outgoing_frames_.Next(),
          [&hpack_compressor, &control_endpoint,
           &data_endpoint](FrameInterface* frame) {
            auto control_endpoint_buffer = frame->Serialize(&hpack_compressor);
            FrameHeader frame_header =
                FrameHeader::Parse(
                    reinterpret_cast<const uint8_t*>(grpc_slice_to_c_string(
                        control_endpoint_buffer.c_slice_buffer()->slices[0])))
                    .value();
            std::cout << "\n writer_ send next frame.";
            SliceBuffer data_endpoint_buffer;
            // Handle data endpoint buffer based on the frame type.
            switch (frame_header.type) {
              case FrameType::kSettings:
                // No data will be sent on data endpoint;
                break;
              case FrameType::kFragment: {
                uint8_t message_padding_size = frame_header.message_padding;
                std::string message_padding(message_padding_size, '0');
                Slice slice(grpc_slice_from_cpp_string(message_padding));
                data_endpoint_buffer.Append(std::move(slice));
                uint8_t message_size = frame_header.message_length;
                auto message = std::move(
                    dynamic_cast<ClientFragmentFrame*>(frame)->message);
                GPR_ASSERT(message_size == message->payload()->Length());
                message->payload()->MoveFirstNBytesIntoSliceBuffer(
                    message_size, data_endpoint_buffer);
                std::cout << "data frame message length " << message_size;
                break;
              }
              case FrameType::kCancel:
                // No data will be sent on data endpoint;
                break;
            }
            return Seq(
                Join(control_endpoint.Write(std::move(control_endpoint_buffer)),
                     data_endpoint.Write(std::move(data_endpoint_buffer))),
                [](std::tuple<absl::StatusOr<SliceBuffer>,
                              absl::StatusOr<SliceBuffer>>
                       ret) -> Poll<absl::variant<Continue, absl::Status>> {
                  if (!(std::get<0>(ret).ok() && std::get<1>(ret).ok())) {
                    // TODO(ladynana): better error handling when writes failed.
                    return absl::InternalError("Endpoint Write failed.");
                  }
                  return Continue();
                });
          })),
      EventEngineWakeupScheduler(
          grpc_event_engine::experimental::CreateEventEngine()),
      [](absl::Status status) -> absl::Status { return status; });
}

}  // namespace chaotic_good
}  // namespace grpc_core
