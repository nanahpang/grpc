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
#include "frame.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {

ClientTransport::ClientTransport(const ChannelArgs& channel_args,
                                 PromiseEndpoint&& control_endpoint,
                                 PromiseEndpoint&& data_endpoint) {
  auto hpack_compressor = std::make_shared<HPackCompressor>();
  writer_ = MakeActivity(
      Loop(Seq(
          outgoing_frames_.Next(),
          [hpack_compressor, &control_endpoint, &data_endpoint](FrameInterface* frame){
            std::cout << "\n frame address " << frame;
            fflush(stdout);
            auto control_endpoint_buffer = frame->Serialize(hpack_compressor.get());
            std::cout << "\n frame serialize length: " << control_endpoint_buffer.Length();
            fflush(stdout);
            FrameHeader frame_header =
                FrameHeader::Parse(
                    reinterpret_cast<const uint8_t*>(grpc_slice_to_c_string(
                        control_endpoint_buffer.c_slice_buffer()->slices[0])))
                    .value();
            SliceBuffer data_endpoint_buffer;
            std::cout << "\n frame header parse stream: " << frame_header.stream_id;
            fflush(stdout);
            // Handle data endpoint buffer based on the frame type.
            switch (frame_header.type) {
              case FrameType::kSettings:
                // No data will be sent on data endpoint;
                break;
              case FrameType::kFragment: {
                std::cout << "\n message padding size: " << frame_header.message_padding;
                fflush(stdout);
                uint8_t message_padding_size = frame_header.message_padding;
                std::string message_padding(message_padding_size, '0');
                Slice slice(grpc_slice_from_cpp_string(message_padding));
                data_endpoint_buffer.Append(std::move(slice));
                
                auto message = std::move(
                    static_cast<ClientFragmentFrame*>(frame)->message);
                    std::cout << "\n data buffer append: " << message->payload()->Length();
                fflush(stdout);
                frame_header.message_length = message->payload()->Length();
                message->payload()->MoveFirstNBytesIntoSliceBuffer(
                    frame_header.message_length, data_endpoint_buffer);
                std::cout << "\n data frame message length " << frame_header.message_length << " / " << message->payload()->Length();
                break;
              }
              case FrameType::kCancel:
                // No data will be sent on data endpoint;
                break;
            }
            std::cout << "\n writer_ exit loop.";
            fflush(stdout);
            return Seq(
                Join(control_endpoint.Write(std::move(control_endpoint_buffer)),
                     data_endpoint.Write(std::move(data_endpoint_buffer))),
                [](std::tuple<absl::StatusOr<SliceBuffer>,
                              absl::StatusOr<SliceBuffer>>
                       ret) -> LoopCtl<absl::Status>{
                  if (!(std::get<0>(ret).ok() && std::get<1>(ret).ok())) {
                    // TODO(ladynana): better error handling when writes failed.
                    std::cout << "\n writer_ failed.";
                    fflush(stdout);
                    return absl::InternalError("Endpoint Write failed.");
                  }
                  return Continue();
                });
            // return absl::OkStatus();
            })),
      EventEngineWakeupScheduler(
          grpc_event_engine::experimental::CreateEventEngine()),
      [](absl::Status status) { 
        std::cout << "\n writer_ on done." << status.ok();
            fflush(stdout);

            });
}

}  // namespace chaotic_good
}  // namespace grpc_core
