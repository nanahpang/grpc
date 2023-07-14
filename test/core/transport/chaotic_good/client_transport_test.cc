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

#include "src/core/ext/transport/chaotic_good/client_transport.h"

// IWYU pragma: no_include <sys/socket.h>

#include <cstring>
#include <memory>
#include <string>
#include <tuple>

#include "absl/functional/any_invocable.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/port.h>  // IWYU pragma: keep
#include <grpc/event_engine/slice_buffer.h>

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/detail/basic_join.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/promise/test_wakeup_schedulers.h"

using testing::_;
using testing::MockFunction;
using testing::Return;
using testing::ReturnRef;
using testing::Sequence;
using testing::StrictMock;
using testing::WithArg;
using testing::WithArgs;

namespace grpc_core {
namespace chaotic_good {
namespace testing {

class MockPromiseEndpoint : public PromiseEndpoint {
 public:
  MOCK_METHOD(ArenaPromise<absl::StatusOr<SliceBuffer>>, Read,
              (size_t num_bytes), ());
  MOCK_METHOD(ArenaPromise<absl::Status>, Write, (SliceBuffer data), ());
};

class MockActivity : public Activity, public Wakeable {
 public:
  MOCK_METHOD(void, WakeupRequested, ());

  void ForceImmediateRepoll(WakeupMask /*mask*/) override { WakeupRequested(); }
  void Orphan() override {}
  Waker MakeOwningWaker() override { return Waker(this, 0); }
  Waker MakeNonOwningWaker() override { return Waker(this, 0); }
  void Wakeup(WakeupMask /*mask*/) override { WakeupRequested(); }
  void WakeupAsync(WakeupMask /*mask*/) override { WakeupRequested(); }
  void Drop(WakeupMask /*mask*/) override {}
  std::string DebugTag() const override { return "MockActivity"; }
  std::string ActivityDebugTag(WakeupMask /*mask*/) const override {
    return DebugTag();
  }

  void Activate() {
    if (scoped_activity_ == nullptr) {
      scoped_activity_ = std::make_unique<ScopedActivity>(this);
    }
  }

  void Deactivate() { scoped_activity_.reset(); }

 private:
  std::unique_ptr<ScopedActivity> scoped_activity_;
};

class ClientTransportTest : public ::testing::Test {
 public:
  ClientTransportTest()
      : mock_control_endpoint_(StrictMock<MockPromiseEndpoint>()),
        mock_data_endpoint_(StrictMock<MockPromiseEndpoint>()),
        client_transport_(channel_args, mock_control_endpoint_,
                          mock_data_endpoint_) {}

 protected:
  ChannelArgs channel_args_;
  MockPromiseEndpoint& mock_control_endpoint_;
  MockPromiseEndpoint& mock_data_endpoint_;
  ClientTransport client_transport_;
};

TEST_F(ClientTransportTest, AddOneStream) {
  MockActivity activity;
  activity.Deactivate();
}

}  // namespace testing
}  // namespace chaotic_good
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
