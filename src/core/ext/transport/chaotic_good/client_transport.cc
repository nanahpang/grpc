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

#include <grpc/event_engine/event_engine.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {

ClientTransport::ClientTransport(const ChannelArgs& channel_args,
                                 PromiseEndpoint control_endpoint,
                                 PromiseEndpoint data_endpoint) {
  HPackCompressor hpack_compressor;
  writer_ = MakeActivity(
      ForEach(std::move(outgoing_frames_),
              [&hpack_compressor, &control_endpoint,
               &data_endpoint](ClientFrame frame) {
                Match(
                    frame,
                    // ClientFragmentFrame.
                    [&hpack_compressor, &control_endpoint,
                     &data_endpoint](ClientFragmentFrame frame) {
                      auto control_endpoint_buffer =
                          frame.Serialize(&hpack_compressor);
                      auto frame_header =
                          FrameHeader::Parse(reinterpret_cast<const uint8_t*>(
                              grpc_slice_to_c_string(
                                  control_endpoint_buffer.c_slice_buffer()
                                      ->slices[0])));
                      // TODO(ladynana): calculate message_padding from
                      // accumulated write bytes on data endpoint.
                      uint8_t message_padding_size =
                          frame_header.value().message_padding;
                      std::string message_padding(message_padding_size, '0');
                      Slice slice(grpc_slice_from_cpp_string(message_padding));
                      SliceBuffer data_endpoint_buffer;
                      data_endpoint_buffer.Append(std::move(slice));
                      return Join(control_endpoint.Write(
                                      std::move(control_endpoint_buffer)),
                                  data_endpoint.Write(SliceBuffer()));
                    },
                    // CancelFrame.
                    [&hpack_compressor, &control_endpoint](CancelFrame frame) {
                      auto control_endpoint_buffer =
                          frame.Serialize(&hpack_compressor);
                      return control_endpoint.Write(
                          std::move(control_endpoint_buffer));
                    });
              }),
      EventEngineWakeupScheduler(
          grpc_event_engine::experimental::CreateEventEngine()),
      []() {

      });
}

}  // namespace chaotic_good
}  // namespace grpc_core
