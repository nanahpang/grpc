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

#include <memory>
#include <string>

#include "absl/functional/any_invocable.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/port.h>  // IWYU pragma: keep
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/slice/slice_buffer.h"

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

class MockEndpoint
    : public grpc_event_engine::experimental::EventEngine::Endpoint {
 public:
  MOCK_METHOD(
      bool, Read,
      (absl::AnyInvocable<void(absl::Status)> on_read,
       grpc_event_engine::experimental::SliceBuffer* buffer,
       const grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs*
           args),
      (override));

  MOCK_METHOD(
      bool, Write,
      (absl::AnyInvocable<void(absl::Status)> on_writable,
       grpc_event_engine::experimental::SliceBuffer* data,
       const grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs*
           args),
      (override));

  MOCK_METHOD(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress&,
      GetPeerAddress, (), (const, override));
  MOCK_METHOD(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress&,
      GetLocalAddress, (), (const, override));
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
      : channel_args_(ChannelArgs()),
        control_endpoint_ptr_(new StrictMock<MockEndpoint>()),
        data_endpoint_ptr_(new StrictMock<MockEndpoint>()),
        control_endpoint_(*control_endpoint_ptr_),
        data_endpoint_(*data_endpoint_ptr_),
        control_promise_endpoint_(
            std::unique_ptr<MockEndpoint>(control_endpoint_ptr_),
            SliceBuffer()),
        data_promise_endpoint_(
            std::unique_ptr<MockEndpoint>(data_endpoint_ptr_), SliceBuffer()),
        client_transport_(channel_args_, control_promise_endpoint_,
                          data_promise_endpoint_) {}

 private:
  const ChannelArgs channel_args_;
  MockEndpoint* control_endpoint_ptr_;
  MockEndpoint* data_endpoint_ptr_;

 protected:
  MockEndpoint& control_endpoint_;
  MockEndpoint& data_endpoint_;
  PromiseEndpoint control_promise_endpoint_;
  PromiseEndpoint data_promise_endpoint_;
  ClientTransport client_transport_;
};

TEST_F(ClientTransportTest, AddOneStream) {
  MockActivity activity;
  EXPECT_CALL(control_endpoint_, Read).Times(0);
  activity.Deactivate();
}

}  // namespace testing
}  // namespace chaotic_good
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Must call to create default EventEngine.
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
