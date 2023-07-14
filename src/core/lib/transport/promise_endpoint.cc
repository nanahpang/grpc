// Copyright 2023 gRPC authors.
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

#include "src/core/lib/transport/promise_endpoint.h"

#include <functional>
#include <memory>

#include "absl/status/status.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"

namespace grpc_core {

PromiseEndpoint::~PromiseEndpoint() {
  // Last write result has not been polled.
  GPR_ASSERT(!write_result_.has_value());
  // Last read result has not been polled.
  GPR_ASSERT(!read_result_.has_value());
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
PromiseEndpoint::GetPeerAddress() const {
  return endpoint_->GetPeerAddress();
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
PromiseEndpoint::GetLocalAddress() const {
  return endpoint_->GetLocalAddress();
}

void PromiseEndpoint::WriteCallback(absl::Status status) {
  MutexLock lock(&write_mutex_);
  write_result_ = status;
  write_waker_.Wakeup();
}

void PromiseEndpoint::ReadCallback(
    absl::Status status, size_t num_bytes_requested,
    absl::optional<
        struct grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs>
        requested_read_args) {
  if (!status.ok()) {
    // Invalidates all previous reads.
    pending_read_buffer_.Clear();
    read_buffer_.Clear();
    MutexLock lock(&read_mutex_);
    read_result_ = status;
    read_waker_.Wakeup();
  } else {
    // Appends `pending_read_buffer_` to `read_buffer_`.
    pending_read_buffer_.MoveFirstNBytesIntoSliceBuffer(
        pending_read_buffer_.Length(), read_buffer_);
    GPR_DEBUG_ASSERT(pending_read_buffer_.Count() == 0u);
    if (read_buffer_.Length() < num_bytes_requested) {
      // A further read is needed.
      // Set read args with number of bytes needed as hint.
      requested_read_args = {
          static_cast<int64_t>(num_bytes_requested - read_buffer_.Length())};
      // If `Read()` returns true immediately, the callback will not be
      // called. We still need to call our callback to pick up the result and
      // maybe do further reads.
      if (endpoint_->Read(std::bind(&PromiseEndpoint::ReadCallback, this,
                                    std::placeholders::_1, num_bytes_requested,
                                    requested_read_args),
                          &pending_read_buffer_,
                          &(requested_read_args.value()))) {
        ReadCallback(absl::OkStatus(), num_bytes_requested,
                     requested_read_args);
      }
    } else {
      MutexLock lock(&read_mutex_);
      read_result_ = status;
      read_waker_.Wakeup();
    }
  }
}

void PromiseEndpoint::ReadByteCallback(absl::Status status) {
  if (!status.ok()) {
    // invalidates all previous reads
    pending_read_buffer_.Clear();
    read_buffer_.Clear();
  } else {
    pending_read_buffer_.MoveFirstNBytesIntoSliceBuffer(
        pending_read_buffer_.Length(), read_buffer_);
  }
  MutexLock lock(&read_mutex_);
  read_result_ = status;
  read_waker_.Wakeup();
}

}  // namespace grpc_core
