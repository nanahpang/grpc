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

#include <stdlib.h>

#include <memory>
#include <string>
#include <tuple>

#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/detail/basic_seq.h"
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
    std::unique_ptr<PromiseEndpoint> control_endpoint,
    std::unique_ptr<PromiseEndpoint> data_endpoint,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine)
    : outgoing_frames_(MpscReceiver<ClientFrame>(4)),
      incoming_frames_(MpscReceiver<ServerFrame>(4)),
      control_endpoint_(std::move(control_endpoint)),
      data_endpoint_(std::move(data_endpoint)),
      control_endpoint_write_buffer_(SliceBuffer()),
      data_endpoint_write_buffer_(SliceBuffer()),
      event_engine_(event_engine) {
  auto hpack_compressor = std::make_shared<HPackCompressor>();
  auto hpack_parser = std::make_shared<HPackParser>();
  auto write_loop = Loop(Seq(
      // Get next outgoing frame.
      this->outgoing_frames_.Next(),
      // Construct data buffers that will be sent to the endpoints.
      [hpack_compressor, this](ClientFrame client_frame) {
        MatchMutable(
            &client_frame,
            [hpack_compressor, this](ClientFragmentFrame* frame) mutable {
              control_endpoint_write_buffer_.Append(
                  frame->Serialize(hpack_compressor.get()));
              if (frame->message != nullptr) {
                char* header_string = grpc_slice_to_c_string(
                    control_endpoint_write_buffer_.c_slice_buffer()->slices[0]);
                std::cout << "\n parse frame header" << header_string;
                fflush(stdout);
                auto frame_header =
                    FrameHeader::Parse(
                        reinterpret_cast<const uint8_t*>(header_string))
                        .value();
                free(header_string);
                std::string message_padding(frame_header.message_padding, '0');
                Slice slice(grpc_slice_from_cpp_string(message_padding));
                // Append message payload to data_endpoint_buffer.
                data_endpoint_write_buffer_.Append(std::move(slice));
                // Append message payload to data_endpoint_buffer.
                frame->message->payload()->MoveFirstNBytesIntoSliceBuffer(
                    frame->message->payload()->Length(),
                    data_endpoint_write_buffer_);
              }
            },
            [hpack_compressor, this](CancelFrame* frame) mutable {
              control_endpoint_write_buffer_.Append(
                  frame->Serialize(hpack_compressor.get()));
            });
        return absl::OkStatus();
      },
      // Write buffers to corresponding endpoints concurrently.
      [this]() {
        return Join(this->control_endpoint_->Write(
                        std::move(control_endpoint_write_buffer_)),
                    this->data_endpoint_->Write(
                        std::move(data_endpoint_write_buffer_)));
      },
      // Finish writes and return status.
      [](std::tuple<absl::Status, absl::Status> ret) -> LoopCtl<absl::Status> {
        // If writes failed, return failure status.
        if (!(std::get<0>(ret).ok() || std::get<1>(ret).ok())) {
          // TODO(ladynana): handle the promise endpoint write failures with
          // closing the transport.
          return absl::InternalError("Promise endpoint writes failed.");
        }
        return Continue();
      }));
  writer_ = MakeActivity(
      // Continuously write next outgoing frames to promise endpoints.
      std::move(write_loop), EventEngineWakeupScheduler(event_engine_),
      [](absl::Status status) {
        GPR_ASSERT(status.code() == absl::StatusCode::kCancelled ||
                   status.code() == absl::StatusCode::kInternal);
      });
  auto read_loop = Loop(Seq(
      // Read frame header from control endpoint.
      this->control_endpoint_->Read(frame_header_size),
      // Parse frame header and return.
      [this](absl::StatusOr<SliceBuffer> read_buffer) mutable {
        // TODO(ladynana): handle read failure here.
        GPR_ASSERT(read_buffer.ok());
        char* header_string = grpc_slice_to_c_string(
                    read_buffer->c_slice_buffer()->slices[0]);
        std::cout << "\n parse frame header" << header_string;
        fflush(stdout);
        frame_header_ =
            FrameHeader::Parse(
                reinterpret_cast<const uint8_t*>(header_string))
                .value();
        free(header_string);
        std::cout << "\n frame header " << frame_header_.DebugString();
        fflush(stdout);
        // Read header and trailers from control endpoint.
        // Read message padding and message from data endpoint.
        return Join(control_endpoint_->Read(frame_header_.GetFrameLength()),
                    Seq(data_endpoint_->Read(frame_header_.message_padding),
                        [this]{ 
                          return data_endpoint_->Read(frame_header_.message_length);}));
      },
      // Finish reads and send receive frame to incoming_frames.
      [this, hpack_parser](
          std::tuple<absl::StatusOr<SliceBuffer>, absl::StatusOr<SliceBuffer>>
              ret) mutable {
        // TODO(ladynana): handle read failure here.
        control_endpoint_read_buffer_ = std::move(*std::get<1>(ret));
        data_endpoint_read_buffer_ = std::move(*std::get<1>(ret));
        ServerFragmentFrame frame;
        // Deserialize frame from read buffer.
        ExecCtx exec_ctx;  // Initialized to get this_cpu() info in global_stat().
        auto status = frame.Deserialize(hpack_parser.get(), frame_header_,
                                        control_endpoint_read_buffer_);
        frame.message->payload()->Append(data_endpoint_read_buffer_);
        // auto incoming_frames = incoming_frames_.MakeSender();
                std::cout << "\n read finish ";
        fflush(stdout);
        return [frame = std::move(frame), incoming_frames = incoming_frames_.MakeSender()]() mutable{ 
          return incoming_frames.Send(ServerFrame(std::move(frame)));};
      },
      // Check if read_loop_ should continue.
      []() -> LoopCtl<absl::Status> {
        return absl::InternalError("Send incoming frames failed.");
        // if (ret) {
        //   // Send incoming frames successfully.
        //   return Continue();
        // } else {
          
        // }
      }
      ));
  reader_ = MakeActivity(
      // Continuously read next incoming frames from promise endpoints.
      std::move(read_loop), EventEngineWakeupScheduler(event_engine_),
      [](absl::Status status) {
        GPR_ASSERT(status.code() == absl::StatusCode::kCancelled ||
                   status.code() == absl::StatusCode::kInternal);
      });
}

}  // namespace chaotic_good
}  // namespace grpc_core
