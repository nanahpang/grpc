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

#include <initializer_list>  // IWYU pragma: keep
#include <iostream>
#include <map>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace chaotic_good {

class ClientTransport {
 public:
  ClientTransport(std::unique_ptr<PromiseEndpoint> control_endpoint,
                  std::unique_ptr<PromiseEndpoint> data_endpoint,
                  std::shared_ptr<grpc_event_engine::experimental::EventEngine>
                      event_engine);
  ~ClientTransport() {
    if (writer_ != nullptr) {
      writer_.reset();
    }
    if (reader_ != nullptr) {
      reader_.reset();
    }
  }
  auto AddStream(CallArgs call_args) {
    // At this point, the connection is set up.
    // Start sending data frames.
    uint64_t stream_id;
    // Max buffer is set to 4, so that for stream reads each time it will queue
    // at most 2 frames.
    std::shared_ptr<MpscReceiver<ServerFrame>> server_frames =
        std::make_shared<MpscReceiver<ServerFrame>>(4);
    {
      MutexLock lock(&mu_);
      stream_id = next_stream_id_++;
      stream_map_.insert(
          std::pair<uint32_t, std::shared_ptr<MpscSender<ServerFrame>>>(
              stream_id, std::make_shared<MpscSender<ServerFrame>>(
                             server_frames->MakeSender())));
    }
    return Join(
        // Continuously send data frame with client to server messages.
        ForEach(std::move(*call_args.client_to_server_messages),
                [stream_id, initial_frame = true,
                 client_initial_metadata =
                     std::move(call_args.client_initial_metadata),
                 outgoing_frames = outgoing_frames_.MakeSender()](
                    MessageHandle result) mutable {
                  std::cout << "\n AddStream " << stream_id
                            << " write client frame";
                  fflush(stdout);
                  ClientFragmentFrame frame;
                  frame.stream_id = stream_id;
                  frame.message = std::move(result);
                  if (initial_frame) {
                    // Send initial frame with client intial metadata.
                    frame.headers = std::move(client_initial_metadata);
                    initial_frame = false;
                  }
                  return Seq(
                      outgoing_frames.Send(ClientFrame(std::move(frame))),
                      [](bool success) -> absl::Status {
                        std::cout << "\n AddStream write client frame done";
                        fflush(stdout);
                        if (!success) {
                          return absl::InternalError(
                              "Send frame to outgoing_frames failed.");
                        }
                        return absl::OkStatus();
                      });
                }),
        // Continuously receive incoming frames and save results to call_args.
        Loop([call_args = std::move(call_args),
              server_frame = std::move(server_frames), stream_id]() mutable {
          return Seq(
              // Receive incoming frame.
              server_frame->Next(),
              // Save incomming frame results to call_args.
              [server_initial_metadata = call_args.server_initial_metadata,
               server_to_client_message = call_args.server_to_client_messages,
               stream_id](ServerFrame server_frame) mutable {
                std::cout << "\n AddStream " << stream_id
                          << " receive server frame";
                fflush(stdout);
                auto frame = std::make_shared<ServerFragmentFrame>(
                    std::move(absl::get<ServerFragmentFrame>(server_frame)));
                return Seq(
                    If((frame->headers != nullptr),
                       [server_initial_metadata,
                        headers = std::move(frame->headers)]() mutable {
                         return server_initial_metadata->Push(
                             std::move(headers));
                       },
                       [] { return false; }),
                    If((frame->message != nullptr),
                       [server_to_client_message,
                        message = std::move(frame->message)]() mutable {
                         return server_to_client_message->Push(
                             std::move(message));
                       },
                       [] { return false; }),
                    If((frame->trailers != nullptr),
                       [trailers = std::move(frame->trailers)]() mutable
                       -> LoopCtl<absl::Status> {
                         // TODO(ladynana): return ServerMetadataHandler
                         return absl::OkStatus();
                       },
                       []() -> LoopCtl<absl::Status> { return Continue(); }));
              });
        })

    );
  }

 private:
  // Max buffer is set to 4, so that for stream writes each time it will queue
  // at most 2 frames.
  MpscReceiver<ClientFrame> outgoing_frames_;
  Mutex mu_;
  uint32_t next_stream_id_ ABSL_GUARDED_BY(mu_) = 1;
  std::map<uint32_t, std::shared_ptr<MpscSender<ServerFrame>>> stream_map_
      ABSL_GUARDED_BY(mu_);
  ActivityPtr writer_;
  ActivityPtr reader_;
  std::unique_ptr<PromiseEndpoint> control_endpoint_;
  std::unique_ptr<PromiseEndpoint> data_endpoint_;
  SliceBuffer control_endpoint_write_buffer_;
  SliceBuffer data_endpoint_write_buffer_;
  SliceBuffer control_endpoint_read_buffer_;
  SliceBuffer data_endpoint_read_buffer_;
  std::unique_ptr<HPackCompressor> hpack_compressor_;
  std::unique_ptr<HPackParser> hpack_parser_;
  FrameHeader frame_header_;
  const size_t frame_header_size_ = 24;
  MemoryAllocator memory_allocator_;
  ScopedArenaPtr arena_;
  // Use to synchronize writer_ and reader_ activity with outside activities;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
  // Pipe<ServerFrame> incoming_server_frames_;
  // PipeReceiver<ServerFrame> incoming_server_frames_receiver_;
  // PipeSender<ServerFrame> incoming_server_frames_sender_;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_TRANSPORT_H