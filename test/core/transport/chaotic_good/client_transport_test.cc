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

#include <stdio.h>

#include <algorithm>  // IWYU pragma: keep
#include <memory>
#include <string>
#include <tuple>
#include <vector>  // IWYU pragma: keep

#include "absl/functional/any_invocable.h"
#include "absl/strings/str_format.h"  // IWYU pragma: keep
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"

using testing::MockFunction;
using testing::Return;
using testing::Sequence;
using testing::StrictMock;
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

class ClientTransportTest : public ::testing::Test {
 public:
  ClientTransportTest()
      : control_endpoint_ptr_(new StrictMock<MockEndpoint>()),
        data_endpoint_ptr_(new StrictMock<MockEndpoint>()),
        memory_allocator_(
            ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
                "test")),
        control_endpoint_(*control_endpoint_ptr_),
        data_endpoint_(*data_endpoint_ptr_),
        event_engine_(std::make_shared<
                      grpc_event_engine::experimental::FuzzingEventEngine>(
            []() {
              grpc_timer_manager_set_threading(false);
              grpc_event_engine::experimental::FuzzingEventEngine::Options
                  options;
              return options;
            }(),
            fuzzing_event_engine::Actions())),
        arena_(MakeScopedArena(initial_arena_size, &memory_allocator_)),
        pipe_client_to_server_messages_(arena_.get()),
        pipe_client_to_server_messages_second_(arena_.get()),
        pipe_server_to_client_messages_(arena_.get()),
        pipe_server_to_client_messages_second_(arena_.get()),
        pipe_server_intial_metadata_(arena_.get()),
        pipe_server_intial_metadata_second_(arena_.get()) {}

  void InitialTransport(int num_of_streams) {
    for (int i = 0; i < num_of_streams; i++) {
      EXPECT_CALL(control_endpoint_, Read)
          .InSequence(control_endpoint_sequence)
          .WillOnce(WithArgs<0, 1>(
              [this, i](absl::AnyInvocable<void(absl::Status)> on_read,
                        grpc_event_engine::experimental::SliceBuffer*
                            buffer) mutable {
                // Construct test frame for EventEngine read: headers  (15
                // bytes), message(16 bytes), message padding (48 byte),
                // trailers (15 bytes).
                const std::string frame_header = {
                    static_cast<char>(0x80),  // frame type = fragment
                    0x03,                     // flag = has header + has trailer
                    0x00,
                    0x00,
                    static_cast<char>(i + 1),  // stream id = 1
                    0x00,
                    0x00,
                    0x00,
                    0x0f,  // header length = 15
                    0x00,
                    0x00,
                    0x00,
                    0x10,  // message length = 16
                    0x00,
                    0x00,
                    0x00,
                    0x30,  // message padding =48
                    0x00,
                    0x00,
                    0x00,
                    0x0f,  // trailer length = 15
                    0x00,
                    0x00,
                    0x00};
                // Schedule mock_endpoint to read buffer.
                grpc_event_engine::experimental::Slice slice(
                    grpc_slice_from_cpp_string(frame_header));
                buffer->Append(std::move(slice));
                // Return false to mock EventEngine read not finish.
                //   if(i==0) {read_callback = std::move(on_read); return
                //   false;} else {return true;}
                read_callback.push_back(std::move(on_read));
                return false;
              }));
      EXPECT_CALL(control_endpoint_, Read)
          .InSequence(control_endpoint_sequence)
          .WillOnce(WithArgs<1>(
              [](grpc_event_engine::experimental::SliceBuffer* buffer) {
                const std::string header = {0x10, 0x0b, 0x67, 0x72, 0x70,
                                            0x63, 0x2d, 0x73, 0x74, 0x61,
                                            0x74, 0x75, 0x73, 0x01, 0x30};
                const std::string trailers = {0x10, 0x0b, 0x67, 0x72, 0x70,
                                              0x63, 0x2d, 0x73, 0x74, 0x61,
                                              0x74, 0x75, 0x73, 0x01, 0x30};
                // Schedule mock_endpoint to read buffer.
                grpc_event_engine::experimental::Slice slice(
                    grpc_slice_from_cpp_string(header + trailers));
                buffer->Append(std::move(slice));
                return true;
              }));
    }
    EXPECT_CALL(control_endpoint_, Read)
        .InSequence(control_endpoint_sequence)
        .WillOnce(Return(false));
    for (int i = 0; i < num_of_streams; i++) {
      EXPECT_CALL(data_endpoint_, Read)
          .InSequence(data_endpoint_sequence)
          .WillOnce(WithArgs<1>(
              [](grpc_event_engine::experimental::SliceBuffer* buffer) {
                const std::string message_padding = {
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                };
                // Schedule mock_endpoint to read buffer.
                grpc_event_engine::experimental::Slice slice(
                    grpc_slice_from_cpp_string(message_padding));
                buffer->Append(std::move(slice));
                return true;
              }));
      EXPECT_CALL(data_endpoint_, Read)
          .InSequence(data_endpoint_sequence)
          .WillOnce(WithArgs<1>(
              [](grpc_event_engine::experimental::SliceBuffer* buffer) {
                const std::string messages = {
                    0x10, 0x0b, 0x67, 0x72, 0x70, 0x63, 0x2d, 0x73,
                    0x74, 0x61, 0x74, 0x75, 0x73, 0x01, 0x30, 0x01};
                // Schedule mock_endpoint to read buffer.
                grpc_event_engine::experimental::Slice slice(
                    grpc_slice_from_cpp_string(messages));
                buffer->Append(std::move(slice));
                return true;
              }));
    }
    client_transport_ = std::make_unique<ClientTransport>(
        std::make_unique<PromiseEndpoint>(
            std::unique_ptr<MockEndpoint>(control_endpoint_ptr_),
            SliceBuffer()),
        std::make_unique<PromiseEndpoint>(
            std::unique_ptr<MockEndpoint>(data_endpoint_ptr_), SliceBuffer()),
        std::static_pointer_cast<grpc_event_engine::experimental::EventEngine>(
            event_engine_));
  }
  std::vector<MessageHandle> CreateMessages(int num_of_messages) {
    std::vector<MessageHandle> messages;
    for (int i = 0; i < num_of_messages; i++) {
      SliceBuffer buffer;
      buffer.Append(
          Slice::FromCopiedString(absl::StrFormat("test message %d", i)));
      auto message = arena_->MakePooled<Message>(std::move(buffer), 0);
      messages.push_back(std::move(message));
    }
    return messages;
  }

  struct Result {};

  auto Wait() {
    std::cout << "\n start wait";
    fflush(stdout);
    return [this]() mutable -> Poll<Result> {
      MutexLock lock(&mu_);
      if (cleared_) {
        std::cout << "\n done wait";
        fflush(stdout);
        return Result{};
      } else {
        std::cout << "\n start wait";
        fflush(stdout);
        waker_ = Activity::current()->MakeNonOwningWaker();
        return Pending();
      }
    };
  }

  void Clear() {
    MutexLock lock(&mu_);
    cleared_ = true;
    waker_.Wakeup();
    std::cout << "\n wait up done";
    fflush(stdout);
  }

 private:
  Mutex mu_;
  Waker waker_ ABSL_GUARDED_BY(mu_);
  bool cleared_ ABSL_GUARDED_BY(mu_) = false;

  MockEndpoint* control_endpoint_ptr_;
  MockEndpoint* data_endpoint_ptr_;
  size_t initial_arena_size = 1024;
  MemoryAllocator memory_allocator_;

 protected:
  MockEndpoint& control_endpoint_;
  MockEndpoint& data_endpoint_;
  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_;
  std::unique_ptr<ClientTransport> client_transport_;
  ScopedArenaPtr arena_;
  Pipe<MessageHandle> pipe_client_to_server_messages_;
  // Added for mutliple streams tests.
  Pipe<MessageHandle> pipe_client_to_server_messages_second_;
  Pipe<MessageHandle> pipe_server_to_client_messages_;
  // Added for mutliple streams tests.
  Pipe<MessageHandle> pipe_server_to_client_messages_second_;
  Pipe<ServerMetadataHandle> pipe_server_intial_metadata_;
  // Added for mutliple streams tests.
  Pipe<ServerMetadataHandle> pipe_server_intial_metadata_second_;

  std::vector<absl::AnyInvocable<void(absl::Status)>> read_callback;
  Sequence control_endpoint_sequence;
  Sequence data_endpoint_sequence;
  const absl::Status kDummyErrorStatus =
      absl::ErrnoToStatus(5566, "just an error");
  static constexpr size_t kDummyRequestSize = 5566u;
};

TEST_F(ClientTransportTest, AddOneStream) {
  InitialTransport(1);
  auto messages = CreateMessages(1);
  ClientMetadataHandle md;
  auto args = CallArgs{std::move(md),
                       ClientInitialMetadataOutstandingToken::Empty(),
                       nullptr,
                       &pipe_server_intial_metadata_.sender,
                       &pipe_client_to_server_messages_.receiver,
                       &pipe_server_to_client_messages_.sender};
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(control_endpoint_, Write).WillOnce(Return(true));
  EXPECT_CALL(data_endpoint_, Write).WillOnce(Return(true));
  auto activity = MakeActivity(
      Seq(
          // Concurrently: send message into the pipe, and receive from the
          // pipe.
          Join(Seq(pipe_client_to_server_messages_.sender.Push(
                       std::move(messages[0])),
                   [this] {
                     this->pipe_client_to_server_messages_.sender.Close();
                     return absl::OkStatus();
                   }),
               Seq(Join(client_transport_->AddStream(std::move(args)),
                        [this]() {
                          // Start read.
                          std::cout << "\n read callback 1 call";
                          fflush(stdout);
                          read_callback[0](absl::OkStatus());
                          return absl::OkStatus();
                        }),
                   [this]() {
                     this->pipe_server_to_client_messages_.sender.Close();
                     this->pipe_server_intial_metadata_.sender.Close();
                     return absl::OkStatus();
                   }),
               Seq(
                   pipe_server_intial_metadata_.receiver.Next(),
                   [this](NextResult<ServerMetadataHandle> ret) {
                     return pipe_server_to_client_messages_.receiver.Next();
                   },
                   [](NextResult<MessageHandle> ret) {
                     return absl::OkStatus();
                   })),
          // Once complete, verify successful sending and the received value.
          [](const std::tuple<absl::Status, absl::Status, absl::Status>& ret) {
            EXPECT_TRUE(std::get<0>(ret).ok());
            EXPECT_TRUE(std::get<1>(ret).ok());
            EXPECT_TRUE(std::get<2>(ret).ok());
            return absl::OkStatus();
          }),
      EventEngineWakeupScheduler(
          std::static_pointer_cast<
              grpc_event_engine::experimental::EventEngine>(event_engine_)),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
  // Wait until ClientTransport's internal activities to finish.
  event_engine_->TickUntilIdle();
  event_engine_->UnsetGlobalHooks();
}

TEST_F(ClientTransportTest, AddOneStreamWithEEFailed) {
  InitialTransport(1);
  auto messages = CreateMessages(1);
  ClientMetadataHandle md;
  auto args = CallArgs{std::move(md),
                       ClientInitialMetadataOutstandingToken::Empty(),
                       nullptr,
                       &pipe_server_intial_metadata_.sender,
                       &pipe_client_to_server_messages_.receiver,
                       &pipe_server_to_client_messages_.sender};
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(control_endpoint_, Write)
      .WillOnce(
          WithArgs<0>([this](absl::AnyInvocable<void(absl::Status)> on_write) {
            on_write(this->kDummyErrorStatus);
            return false;
          }));
  EXPECT_CALL(data_endpoint_, Write)
      .WillOnce(
          WithArgs<0>([this](absl::AnyInvocable<void(absl::Status)> on_write) {
            on_write(this->kDummyErrorStatus);
            return false;
          }));
  auto activity = MakeActivity(
      Seq(
          // Concurrently: send message into the pipe, and receive from the
          // pipe.
          Join(Seq(pipe_client_to_server_messages_.sender.Push(
                       std::move(messages[0])),
                   [this] {
                     this->pipe_client_to_server_messages_.sender.Close();
                     return absl::OkStatus();
                   }),
               Seq(Join(client_transport_->AddStream(std::move(args)),
                        [this]() {
                          // Start read.
                          std::cout << "\n read callback 1 call";
                          fflush(stdout);
                          read_callback[0](absl::OkStatus());
                          return absl::OkStatus();
                        }),
                   [this]() {
                     this->pipe_server_to_client_messages_.sender.Close();
                     this->pipe_server_intial_metadata_.sender.Close();
                     return absl::OkStatus();
                   }),
               Seq(
                   pipe_server_intial_metadata_.receiver.Next(),
                   [this](NextResult<ServerMetadataHandle> ret) {
                     return pipe_server_to_client_messages_.receiver.Next();
                   },
                   [](NextResult<MessageHandle> ret) {
                     return absl::OkStatus();
                   })),
          // Once complete, verify successful sending and the received value.
          [](const std::tuple<absl::Status, absl::Status, absl::Status>& ret) {
            // TODO(ladynana): change these expectations to errors after the
            // writer activity closes transport for EE failures.
            EXPECT_TRUE(std::get<0>(ret).ok());
            EXPECT_TRUE(std::get<1>(ret).ok());
            EXPECT_TRUE(std::get<2>(ret).ok());
            return absl::OkStatus();
          }),
      EventEngineWakeupScheduler(
          std::static_pointer_cast<
              grpc_event_engine::experimental::EventEngine>(event_engine_)),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
  // Wait until ClientTransport's internal activities to finish.
  event_engine_->TickUntilIdle();
  event_engine_->UnsetGlobalHooks();
}

TEST_F(ClientTransportTest, AddOneStreamMultipleMessages) {
  InitialTransport(1);
  auto messages = CreateMessages(3);
  ClientMetadataHandle md;
  auto args = CallArgs{std::move(md),
                       ClientInitialMetadataOutstandingToken::Empty(),
                       nullptr,
                       &pipe_server_intial_metadata_.sender,
                       &pipe_client_to_server_messages_.receiver,
                       &pipe_server_to_client_messages_.sender};
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(control_endpoint_, Write).Times(3).WillRepeatedly(Return(true));
  EXPECT_CALL(data_endpoint_, Write).Times(3).WillRepeatedly(Return(true));
  auto activity = MakeActivity(
      Seq(
          // Concurrently: send messages into the pipe, and receive from the
          // pipe.
          Join(Seq(pipe_client_to_server_messages_.sender.Push(
                       std::move(messages[0])),
                   pipe_client_to_server_messages_.sender.Push(
                       std::move(messages[1])),
                   pipe_client_to_server_messages_.sender.Push(
                       std::move(messages[2])),
                   [this] {
                     this->pipe_client_to_server_messages_.sender.Close();
                     return absl::OkStatus();
                   }),
               Seq(Join(client_transport_->AddStream(std::move(args)),
                        [this]() {
                          // Start read.
                          std::cout << "\n read callback 1 call";
                          fflush(stdout);
                          read_callback[0](absl::OkStatus());
                          return absl::OkStatus();
                        }),
                   [this]() {
                     this->pipe_server_to_client_messages_.sender.Close();
                     this->pipe_server_intial_metadata_.sender.Close();
                     return absl::OkStatus();
                   }),
               Seq(
                   pipe_server_intial_metadata_.receiver.Next(),
                   [this](NextResult<ServerMetadataHandle> ret) {
                     return pipe_server_to_client_messages_.receiver.Next();
                   },
                   [](NextResult<MessageHandle> ret) {
                     return absl::OkStatus();
                   })),
          // Once complete, verify successful sending and the received value.
          [](const std::tuple<absl::Status, absl::Status, absl::Status>& ret) {
            EXPECT_TRUE(std::get<0>(ret).ok());
            EXPECT_TRUE(std::get<1>(ret).ok());
            EXPECT_TRUE(std::get<2>(ret).ok());
            return absl::OkStatus();
          }),
      EventEngineWakeupScheduler(
          std::static_pointer_cast<
              grpc_event_engine::experimental::EventEngine>(event_engine_)),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
  // Wait until ClientTransport's internal activities to finish.
  event_engine_->TickUntilIdle();
  event_engine_->UnsetGlobalHooks();
}

TEST_F(ClientTransportTest, AddMultipleStreams) {
  InitialTransport(2);
  auto messages = CreateMessages(2);
  ClientMetadataHandle md;
  auto first_stream_args =
      CallArgs{std::move(md),
               ClientInitialMetadataOutstandingToken::Empty(),
               nullptr,
               &pipe_server_intial_metadata_.sender,
               &pipe_client_to_server_messages_.receiver,
               &pipe_server_to_client_messages_.sender};
  auto second_stream_args =
      CallArgs{std::move(md),
               ClientInitialMetadataOutstandingToken::Empty(),
               nullptr,
               &pipe_server_intial_metadata_second_.sender,
               &pipe_client_to_server_messages_second_.receiver,
               &pipe_server_to_client_messages_second_.sender};
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(control_endpoint_, Write).Times(2).WillRepeatedly(Return(true));
  EXPECT_CALL(data_endpoint_, Write).Times(2).WillRepeatedly(Return(true));
  auto activity = MakeActivity(
      Seq(
          // Concurrently: send messages into the pipe, and receive from the
          // pipe.
          Join(
              // Send message to first stream pipe.
              Seq(pipe_client_to_server_messages_.sender.Push(
                      std::move(messages[0])),
                  [this] {
                    pipe_client_to_server_messages_.sender.Close();
                    return absl::OkStatus();
                  }),
              // Send message to second stream pipe.
              Seq(pipe_client_to_server_messages_second_.sender.Push(
                      std::move(messages[1])),
                  [this] {
                    pipe_client_to_server_messages_second_.sender.Close();
                    return absl::OkStatus();
                  }),
              // Receive message from first stream pipe.
              Seq(Join(client_transport_->AddStream(
                           std::move(first_stream_args)),
                       [this] {
                         // Start read.
                         std::cout << "\n read callback 1 call";
                         fflush(stdout);
                         read_callback[0](absl::OkStatus());
                         return absl::OkStatus();
                       }),
                  [this]() {
                    this->pipe_server_to_client_messages_.sender.Close();
                    this->pipe_server_intial_metadata_.sender.Close();
                    return absl::OkStatus();
                  }),
              // Receive message from second stream pipe.
              Seq(Join(client_transport_->AddStream(
                           std::move(second_stream_args)),
                       Seq(Wait(),
                           [this] {
                             // Wait until first read finished.
                             std::cout << "\n read callback 2 call";
                             fflush(stdout);
                             read_callback[1](absl::OkStatus());
                             return absl::OkStatus();
                           })),
                  [this]() {
                    this->pipe_server_to_client_messages_second_.sender.Close();
                    this->pipe_server_intial_metadata_second_.sender.Close();
                    return absl::OkStatus();
                  }),
              Seq(
                  pipe_server_intial_metadata_.receiver.Next(),
                  [this](NextResult<ServerMetadataHandle> ret) {
                    return pipe_server_to_client_messages_.receiver.Next();
                  },
                  [this](NextResult<MessageHandle> ret) {
                    Clear();
                    Activity::current()->ForceWakeup();
                    return absl::OkStatus();
                  }),
              Seq(
                  pipe_server_intial_metadata_second_.receiver.Next(),
                  [this](NextResult<ServerMetadataHandle> ret) {
                    return pipe_server_to_client_messages_second_.receiver
                        .Next();
                  },
                  [](NextResult<MessageHandle> ret) {
                    return absl::OkStatus();
                  })),
          // Once complete, verify successful sending and the received value.
          [](const std::tuple<absl::Status, absl::Status, absl::Status,
                              absl::Status, absl::Status, absl::Status>& ret) {
            EXPECT_TRUE(std::get<0>(ret).ok());
            EXPECT_TRUE(std::get<1>(ret).ok());
            EXPECT_TRUE(std::get<2>(ret).ok());
            EXPECT_TRUE(std::get<3>(ret).ok());
            EXPECT_TRUE(std::get<4>(ret).ok());
            EXPECT_TRUE(std::get<5>(ret).ok());
            return absl::OkStatus();
          }),
      EventEngineWakeupScheduler(
          std::static_pointer_cast<
              grpc_event_engine::experimental::EventEngine>(event_engine_)),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
  // Wait until ClientTransport's internal activities to finish.
  event_engine_->TickUntilIdle();
  event_engine_->UnsetGlobalHooks();
}

TEST_F(ClientTransportTest, AddMultipleStreamsMultipleMessages) {
  InitialTransport(2);
  auto messages = CreateMessages(6);
  ClientMetadataHandle md;
  auto first_stream_args =
      CallArgs{std::move(md),
               ClientInitialMetadataOutstandingToken::Empty(),
               nullptr,
               &pipe_server_intial_metadata_.sender,
               &pipe_client_to_server_messages_.receiver,
               &pipe_server_to_client_messages_.sender};
  auto second_stream_args =
      CallArgs{std::move(md),
               ClientInitialMetadataOutstandingToken::Empty(),
               nullptr,
               &pipe_server_intial_metadata_second_.sender,
               &pipe_client_to_server_messages_second_.receiver,
               &pipe_server_to_client_messages_second_.sender};
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(control_endpoint_, Write).Times(6).WillRepeatedly(Return(true));
  EXPECT_CALL(data_endpoint_, Write).Times(6).WillRepeatedly(Return(true));
  auto activity = MakeActivity(
      Seq(
          // Concurrently: send messages into the pipe, and receive from the
          // pipe.
          Join(
              // Send messages to first stream pipe.
              Seq(pipe_client_to_server_messages_.sender.Push(
                      std::move(messages[0])),
                  pipe_client_to_server_messages_.sender.Push(
                      std::move(messages[1])),
                  pipe_client_to_server_messages_.sender.Push(
                      std::move(messages[2])),
                  [this] {
                    pipe_client_to_server_messages_.sender.Close();
                    return absl::OkStatus();
                  }),
              // Send messages to second stream pipe.
              Seq(pipe_client_to_server_messages_second_.sender.Push(
                      std::move(messages[3])),
                  pipe_client_to_server_messages_second_.sender.Push(
                      std::move(messages[4])),
                  pipe_client_to_server_messages_second_.sender.Push(
                      std::move(messages[5])),
                  [this] {
                    pipe_client_to_server_messages_second_.sender.Close();
                    return absl::OkStatus();
                  }),
              // Receive message from first stream pipe.
              Seq(Join(client_transport_->AddStream(
                           std::move(first_stream_args)),
                       [this]() {
                         // Start read.
                         read_callback[0](absl::OkStatus());
                         return absl::OkStatus();
                       }),
                  [this]() {
                    this->pipe_server_to_client_messages_.sender.Close();
                    this->pipe_server_intial_metadata_.sender.Close();
                    return absl::OkStatus();
                  }),
              // Receive message from second stream pipe.
              Seq(Join(client_transport_->AddStream(
                           std::move(second_stream_args)),
                       Seq(Wait(),
                           [this]() {
                             // Start read.
                             read_callback[1](absl::OkStatus());
                             return absl::OkStatus();
                           })),
                  [this]() {
                    this->pipe_server_to_client_messages_second_.sender.Close();
                    this->pipe_server_intial_metadata_second_.sender.Close();
                    return absl::OkStatus();
                  }),
              Seq(
                  pipe_server_intial_metadata_.receiver.Next(),
                  [this](NextResult<ServerMetadataHandle> ret) {
                    return pipe_server_to_client_messages_.receiver.Next();
                  },
                  [this](NextResult<MessageHandle> ret) {
                    Clear();
                    return absl::OkStatus();
                  }),
              Seq(
                  pipe_server_intial_metadata_second_.receiver.Next(),
                  [this](NextResult<ServerMetadataHandle> ret) {
                    return pipe_server_to_client_messages_second_.receiver
                        .Next();
                  },
                  [](NextResult<MessageHandle> ret) {
                    return absl::OkStatus();
                  })),
          // Once complete, verify successful sending and the received value.
          [](const std::tuple<absl::Status, absl::Status, absl::Status,
                              absl::Status, absl::Status, absl::Status>& ret) {
            EXPECT_TRUE(std::get<0>(ret).ok());
            EXPECT_TRUE(std::get<1>(ret).ok());
            EXPECT_TRUE(std::get<2>(ret).ok());
            EXPECT_TRUE(std::get<3>(ret).ok());
            EXPECT_TRUE(std::get<4>(ret).ok());
            EXPECT_TRUE(std::get<5>(ret).ok());
            return absl::OkStatus();
          }),
      EventEngineWakeupScheduler(
          std::static_pointer_cast<
              grpc_event_engine::experimental::EventEngine>(event_engine_)),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
  // Wait until ClientTransport's internal activities to finish.
  event_engine_->TickUntilIdle();
  event_engine_->UnsetGlobalHooks();
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
