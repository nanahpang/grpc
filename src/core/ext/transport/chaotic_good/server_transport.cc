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

#include "src/core/ext/transport/chaotic_good/server_transport.h"

#include <memory>
#include <string>
#include <tuple>

#include "absl/random/bit_gen_ref.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {

ServerTransport::ServerTransport(
    std::unique_ptr<PromiseEndpoint> control_endpoint,
    std::unique_ptr<PromiseEndpoint> data_endpoint,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine,
    AcceptFn accept_fn)
    : accept_fn_(std::move(accept_fn)),
      outgoing_frames_(MpscReceiver<ServerFrame>(4)),
      control_endpoint_(std::move(control_endpoint)),
      data_endpoint_(std::move(data_endpoint)),
      control_endpoint_write_buffer_(SliceBuffer()),
      data_endpoint_write_buffer_(SliceBuffer()),
      hpack_compressor_(std::make_unique<HPackCompressor>()),
      hpack_parser_(std::make_unique<HPackParser>()),
      memory_allocator_(
          ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
              "server_transport")),
      arena_(MakeScopedArena(1024, &memory_allocator_)),
      context_(arena_.get()),
      event_engine_(event_engine) {
  // Server write.
  auto write_loop = Loop([this] {
    return TrySeq(
        // Get next outgoing frame.
        this->outgoing_frames_.Next(),
        // Construct data buffers that will be sent to the endpoints.
        [this](ServerFrame server_frame) {
          ServerFragmentFrame frame =
              std::move(absl::get<ServerFragmentFrame>(server_frame));
          control_endpoint_write_buffer_.Append(
              frame.Serialize(hpack_compressor_.get()));
          if (frame.message != nullptr) {
            auto frame_header =
                FrameHeader::Parse(
                    reinterpret_cast<const uint8_t*>(GRPC_SLICE_START_PTR(
                        control_endpoint_write_buffer_.c_slice_buffer()
                            ->slices[0])))
                    .value();
            // TODO(ladynana): add message_padding calculation by
            // accumulating bytes sent.
            std::string message_padding(frame_header.message_padding, '0');
            Slice slice(grpc_slice_from_cpp_string(message_padding));
            // Append message payload to data_endpoint_buffer.
            data_endpoint_write_buffer_.Append(std::move(slice));
            // Append message payload to data_endpoint_buffer.
            frame.message->payload()->MoveFirstNBytesIntoSliceBuffer(
                frame.message->payload()->Length(),
                data_endpoint_write_buffer_);
          }
          return absl::OkStatus();
        },
        // Write buffers to corresponding endpoints concurrently.
        [this]() {
          return TryJoin(
              control_endpoint_->Write(
                  std::move(control_endpoint_write_buffer_)),
              data_endpoint_->Write(std::move(data_endpoint_write_buffer_)));
        },
        // Finish writes to difference endpoints and continue the loop.
        []() -> LoopCtl<absl::Status> {
          // The write failures will be caught in TrySeq and exit loop.
          // Therefore, only need to return Continue() in the last lambda
          // function.
          return Continue();
        });
  });
  writer_ = MakeActivity(
      // Continuously write next outgoing frames to promise endpoints.
      std::move(write_loop), EventEngineWakeupScheduler(event_engine_),
      [this](absl::Status status) {
        if (!(status.ok() || status.code() == absl::StatusCode::kCancelled)) {
          this->AbortWithError();
        }
      },
      // Hold Arena in activity for GetContext<Arena> usage.
      arena_.get());
  auto read_loop = Loop([this] {
    return TrySeq(
        // Read frame header from control endpoint.
        // TODO(ladynana): remove memcpy in ReadSlice.
        this->control_endpoint_->ReadSlice(FrameHeader::frame_header_size_),
        // Read different parts of the server frame from control/data endpoints
        // based on frame header.
        [this](Slice read_buffer) mutable {
          frame_header_ = std::make_shared<FrameHeader>(
              FrameHeader::Parse(
                  reinterpret_cast<const uint8_t*>(
                      GRPC_SLICE_START_PTR(read_buffer.c_slice())))
                  .value());
          // Read header and trailers from control endpoint.
          // Read message padding and message from data endpoint.
          return TryJoin(
              control_endpoint_->Read(frame_header_->GetFrameLength()),
              data_endpoint_->Read(frame_header_->message_padding +
                                   frame_header_->message_length));
        },
        // Construct and send the client frame to corresponding stream.
        [this](std::tuple<SliceBuffer, SliceBuffer> ret) mutable {
          control_endpoint_read_buffer_ = std::move(std::get<0>(ret));
          // Discard message padding and only keep message in data read buffer.
          std::get<1>(ret).MoveLastNBytesIntoSliceBuffer(
              frame_header_->message_length, data_endpoint_read_buffer_);
          // TODO(ladynana): match client frame type.
          ClientFragmentFrame frame;
          // Initialized to get this_cpu() info in global_stat().
          ExecCtx exec_ctx;
          // Deserialize frame from read buffer.
          absl::BitGen bitgen;
          auto status = frame.Deserialize(hpack_parser_.get(), *frame_header_,
                                          absl::BitGenRef(bitgen),
                                          control_endpoint_read_buffer_);
          GPR_ASSERT(status.ok());
          auto message = arena_->MakePooled<Message>(
              std::move(data_endpoint_read_buffer_), 0);
          // Initialize call.
          auto call_initiator = accept_fn_(*frame.headers);
          AddCall(call_initiator);
          return call_initiator.PushClientToServerMessage(std::move(message));
        },
        [](bool ret) -> LoopCtl<absl::Status> {
          if (ret) {
            std::cout << "reader continue "
                      << "\n";
            fflush(stdout);
            return Continue();
          } else {
            std::cout << "reader failed "
                      << "\n";
            fflush(stdout);
            return absl::InternalError("Send message to pipe failed.");
          }
        });
  });
  reader_ = MakeActivity(
      // Continuously read next incoming frames from promise endpoints.
      std::move(read_loop), EventEngineWakeupScheduler(event_engine_),
      [this](absl::Status status) {
        if (!(status.ok() || status.code() == absl::StatusCode::kCancelled)) {
          this->AbortWithError();
        }
      },
      // Hold Arena in activity for GetContext<Arena> usage.
      arena_.get());
}

}  // namespace chaotic_good
}  // namespace grpc_core
