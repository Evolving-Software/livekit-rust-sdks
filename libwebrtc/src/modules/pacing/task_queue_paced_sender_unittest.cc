/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/pacing/task_queue_paced_sender.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "api/sequence_checker.h"
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/pacing/pacing_controller.h"
#include "modules/pacing/packet_router.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/scoped_key_value_config.h"
#include "test/time_controller/simulated_time_controller.h"

using ::testing::_;
using ::testing::AtLeast;
using ::testing::AtMost;
using ::testing::NiceMock;

namespace webrtc {
namespace {
constexpr uint32_t kAudioSsrc = 12345;
constexpr uint32_t kVideoSsrc = 234565;
constexpr uint32_t kVideoRtxSsrc = 34567;
constexpr uint32_t kFlexFecSsrc = 45678;
constexpr size_t kDefaultPacketSize = 1234;

class MockPacketRouter : public PacketRouter {
 public:
  MOCK_METHOD(void,
              SendPacket,
              (std::unique_ptr<RtpPacketToSend> packet,
               const PacedPacketInfo& cluster_info),
              (override));
  MOCK_METHOD(std::vector<std::unique_ptr<RtpPacketToSend>>,
              FetchFec,
              (),
              (override));
  MOCK_METHOD(std::vector<std::unique_ptr<RtpPacketToSend>>,
              GeneratePadding,
              (DataSize target_size),
              (override));
};

std::vector<std::unique_ptr<RtpPacketToSend>> GeneratePadding(
    DataSize target_size) {
  // 224 bytes is the max padding size for plain padding packets generated by
  // RTPSender::GeneratePadding().
  const DataSize kMaxPaddingPacketSize = DataSize::Bytes(224);
  DataSize padding_generated = DataSize::Zero();
  std::vector<std::unique_ptr<RtpPacketToSend>> padding_packets;
  while (padding_generated < target_size) {
    DataSize packet_size =
        std::min(target_size - padding_generated, kMaxPaddingPacketSize);
    padding_generated += packet_size;
    auto padding_packet =
        std::make_unique<RtpPacketToSend>(/*extensions=*/nullptr);
    padding_packet->set_packet_type(RtpPacketMediaType::kPadding);
    padding_packet->SetPadding(packet_size.bytes());
    padding_packets.push_back(std::move(padding_packet));
  }
  return padding_packets;
}

}  // namespace

namespace test {

std::unique_ptr<RtpPacketToSend> BuildRtpPacket(RtpPacketMediaType type) {
  auto packet = std::make_unique<RtpPacketToSend>(nullptr);
  packet->set_packet_type(type);
  switch (type) {
    case RtpPacketMediaType::kAudio:
      packet->SetSsrc(kAudioSsrc);
      break;
    case RtpPacketMediaType::kVideo:
      packet->SetSsrc(kVideoSsrc);
      break;
    case RtpPacketMediaType::kRetransmission:
    case RtpPacketMediaType::kPadding:
      packet->SetSsrc(kVideoRtxSsrc);
      break;
    case RtpPacketMediaType::kForwardErrorCorrection:
      packet->SetSsrc(kFlexFecSsrc);
      break;
  }

  packet->SetPayloadSize(kDefaultPacketSize);
  return packet;
}

std::vector<std::unique_ptr<RtpPacketToSend>> GeneratePackets(
    RtpPacketMediaType type,
    size_t num_packets) {
  std::vector<std::unique_ptr<RtpPacketToSend>> packets;
  for (size_t i = 0; i < num_packets; ++i) {
    packets.push_back(BuildRtpPacket(type));
  }
  return packets;
}

TEST(TaskQueuePacedSenderTest, PacesPackets) {
  GlobalSimulatedTimeController time_controller(Timestamp::Millis(1234));
  MockPacketRouter packet_router;
  ScopedKeyValueConfig trials;
  TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router, trials,
                             PacingController::kMinSleepTime,
                             TaskQueuePacedSender::kNoPacketHoldback);

  // Insert a number of packets, covering one second.
  static constexpr size_t kPacketsToSend = 42;
  SequenceChecker sequence_checker;
  pacer.SetPacingRates(
      DataRate::BitsPerSec(kDefaultPacketSize * 8 * kPacketsToSend),
      DataRate::Zero());
  pacer.EnsureStarted();
  pacer.EnqueuePackets(
      GeneratePackets(RtpPacketMediaType::kVideo, kPacketsToSend));

  // Expect all of them to be sent.
  size_t packets_sent = 0;
  Timestamp end_time = Timestamp::PlusInfinity();
  EXPECT_CALL(packet_router, SendPacket)
      .WillRepeatedly([&](std::unique_ptr<RtpPacketToSend> /* packet */,
                          const PacedPacketInfo& /* cluster_info */) {
        ++packets_sent;
        if (packets_sent == kPacketsToSend) {
          end_time = time_controller.GetClock()->CurrentTime();
        }
        EXPECT_TRUE(sequence_checker.IsCurrent());
      });

  const Timestamp start_time = time_controller.GetClock()->CurrentTime();

  // Packets should be sent over a period of close to 1s. Expect a little
  // lower than this since initial probing is a bit quicker.
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
  EXPECT_EQ(packets_sent, kPacketsToSend);
  ASSERT_TRUE(end_time.IsFinite());
  EXPECT_NEAR((end_time - start_time).ms<double>(), 1000.0, 50.0);
}

// Same test as above, but with 0.5s of burst applied.
TEST(TaskQueuePacedSenderTest, PacesPacketsWithBurst) {
  GlobalSimulatedTimeController time_controller(Timestamp::Millis(1234));
  MockPacketRouter packet_router;
  ScopedKeyValueConfig trials;
  TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router, trials,

                             PacingController::kMinSleepTime,
                             TaskQueuePacedSender::kNoPacketHoldback);
  pacer.SetSendBurstInterval(
      // Half a second of bursting.
      TimeDelta::Seconds(0.5));

  // Insert a number of packets, covering one second.
  static constexpr size_t kPacketsToSend = 42;
  SequenceChecker sequence_checker;
  pacer.SetPacingRates(
      DataRate::BitsPerSec(kDefaultPacketSize * 8 * kPacketsToSend),
      DataRate::Zero());
  pacer.EnsureStarted();
  pacer.EnqueuePackets(
      GeneratePackets(RtpPacketMediaType::kVideo, kPacketsToSend));

  // Expect all of them to be sent.
  size_t packets_sent = 0;
  Timestamp end_time = Timestamp::PlusInfinity();
  EXPECT_CALL(packet_router, SendPacket)
      .WillRepeatedly([&](std::unique_ptr<RtpPacketToSend> /* packet */,
                          const PacedPacketInfo& /* cluster_info */) {
        ++packets_sent;
        if (packets_sent == kPacketsToSend) {
          end_time = time_controller.GetClock()->CurrentTime();
        }
        EXPECT_TRUE(sequence_checker.IsCurrent());
      });

  const Timestamp start_time = time_controller.GetClock()->CurrentTime();

  // Packets should be sent over a period of close to 1s. Expect a little
  // lower than this since initial probing is a bit quicker.
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
  EXPECT_EQ(packets_sent, kPacketsToSend);
  ASSERT_TRUE(end_time.IsFinite());
  // Because of half a second of burst, what would normally have been paced over
  // ~1 second now takes ~0.5 seconds.
  EXPECT_NEAR((end_time - start_time).ms<double>(), 500.0, 50.0);
}

TEST(TaskQueuePacedSenderTest, ReschedulesProcessOnRateChange) {
  GlobalSimulatedTimeController time_controller(Timestamp::Millis(1234));
  MockPacketRouter packet_router;
  ScopedKeyValueConfig trials;
  TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router, trials,

                             PacingController::kMinSleepTime,
                             TaskQueuePacedSender::kNoPacketHoldback);

  // Insert a number of packets to be sent 200ms apart.
  const size_t kPacketsPerSecond = 5;
  const DataRate kPacingRate =
      DataRate::BitsPerSec(kDefaultPacketSize * 8 * kPacketsPerSecond);
  pacer.SetPacingRates(kPacingRate, DataRate::Zero());
  pacer.EnsureStarted();

  // Send some initial packets to be rid of any probes.
  EXPECT_CALL(packet_router, SendPacket).Times(kPacketsPerSecond);
  pacer.EnqueuePackets(
      GeneratePackets(RtpPacketMediaType::kVideo, kPacketsPerSecond));
  time_controller.AdvanceTime(TimeDelta::Seconds(1));

  // Insert three packets, and record send time of each of them.
  // After the second packet is sent, double the send rate so we can
  // check the third packets is sent after half the wait time.
  Timestamp first_packet_time = Timestamp::MinusInfinity();
  Timestamp second_packet_time = Timestamp::MinusInfinity();
  Timestamp third_packet_time = Timestamp::MinusInfinity();

  EXPECT_CALL(packet_router, SendPacket)
      .Times(3)
      .WillRepeatedly([&](std::unique_ptr<RtpPacketToSend> /* packet */,
                          const PacedPacketInfo& /* cluster_info */) {
        if (first_packet_time.IsInfinite()) {
          first_packet_time = time_controller.GetClock()->CurrentTime();
        } else if (second_packet_time.IsInfinite()) {
          second_packet_time = time_controller.GetClock()->CurrentTime();
          // Avoid invoke SetPacingRate in the context of sending a packet.
          time_controller.GetMainThread()->PostTask(
              [&] { pacer.SetPacingRates(2 * kPacingRate, DataRate::Zero()); });
        } else {
          third_packet_time = time_controller.GetClock()->CurrentTime();
        }
      });

  pacer.EnqueuePackets(GeneratePackets(RtpPacketMediaType::kVideo, 3));
  time_controller.AdvanceTime(TimeDelta::Millis(500));
  ASSERT_TRUE(third_packet_time.IsFinite());
  EXPECT_NEAR((second_packet_time - first_packet_time).ms<double>(), 200.0,
              1.0);
  EXPECT_NEAR((third_packet_time - second_packet_time).ms<double>(), 100.0,
              1.0);
}

TEST(TaskQueuePacedSenderTest, SendsAudioImmediately) {
  GlobalSimulatedTimeController time_controller(Timestamp::Millis(1234));
  NiceMock<MockPacketRouter> packet_router;
  ScopedKeyValueConfig trials;
  TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router, trials,

                             PacingController::kMinSleepTime,
                             TaskQueuePacedSender::kNoPacketHoldback);

  const DataRate kPacingDataRate = DataRate::KilobitsPerSec(125);

  pacer.SetPacingRates(kPacingDataRate, DataRate::Zero());
  pacer.EnsureStarted();

  // Add some initial video packets. Not all should be sent immediately.
  EXPECT_CALL(packet_router, SendPacket).Times(AtMost(9));
  pacer.EnqueuePackets(GeneratePackets(RtpPacketMediaType::kVideo, 10));
  time_controller.AdvanceTime(TimeDelta::Zero());
  ::testing::Mock::VerifyAndClearExpectations(&packet_router);

  // Insert an audio packet, it should be sent immediately.
  EXPECT_CALL(packet_router, SendPacket);
  pacer.EnqueuePackets(GeneratePackets(RtpPacketMediaType::kAudio, 1));
  time_controller.AdvanceTime(TimeDelta::Zero());
  ::testing::Mock::VerifyAndClearExpectations(&packet_router);
}

TEST(TaskQueuePacedSenderTest, SleepsDuringCoalscingWindow) {
  const TimeDelta kCoalescingWindow = TimeDelta::Millis(5);
  GlobalSimulatedTimeController time_controller(Timestamp::Millis(1234));
  NiceMock<MockPacketRouter> packet_router;
  ScopedKeyValueConfig trials;
  TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router, trials,

                             kCoalescingWindow,
                             TaskQueuePacedSender::kNoPacketHoldback);
  pacer.SetSendBurstInterval(TimeDelta::Zero());

  // Set rates so one packet adds one ms of buffer level.
  const DataSize kPacketSize = DataSize::Bytes(kDefaultPacketSize);
  const TimeDelta kPacketPacingTime = TimeDelta::Millis(1);
  const DataRate kPacingDataRate = kPacketSize / kPacketPacingTime;

  pacer.SetPacingRates(kPacingDataRate, DataRate::Zero());
  pacer.EnsureStarted();

  // Add 10 packets. The first burst should be sent immediately since the
  // buffers are clear.
  EXPECT_CALL(packet_router, SendPacket).Times(AtMost(9));
  pacer.EnqueuePackets(GeneratePackets(RtpPacketMediaType::kVideo, 10));
  time_controller.AdvanceTime(TimeDelta::Zero());
  ::testing::Mock::VerifyAndClearExpectations(&packet_router);

  // Advance time to 1ms before the coalescing window ends. No packets should
  // be sent.
  EXPECT_CALL(packet_router, SendPacket).Times(0);
  time_controller.AdvanceTime(kCoalescingWindow - TimeDelta::Millis(1));

  // Advance time to where coalescing window ends. All packets that should
  // have been sent up til now will be sent.
  EXPECT_CALL(packet_router, SendPacket).Times(5);
  time_controller.AdvanceTime(TimeDelta::Millis(1));
  ::testing::Mock::VerifyAndClearExpectations(&packet_router);
}

TEST(TaskQueuePacedSenderTest, ProbingOverridesCoalescingWindow) {
  const TimeDelta kCoalescingWindow = TimeDelta::Millis(5);
  GlobalSimulatedTimeController time_controller(Timestamp::Millis(1234));
  MockPacketRouter packet_router;
  ScopedKeyValueConfig trials;
  TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router, trials,

                             kCoalescingWindow,
                             TaskQueuePacedSender::kNoPacketHoldback);

  // Set rates so one packet adds one ms of buffer level.
  const DataSize kPacketSize = DataSize::Bytes(kDefaultPacketSize);
  const TimeDelta kPacketPacingTime = TimeDelta::Millis(1);
  const DataRate kPacingDataRate = kPacketSize / kPacketPacingTime;

  pacer.SetPacingRates(kPacingDataRate, DataRate::Zero());
  pacer.EnsureStarted();

  // Add 10 packets. The first should be sent immediately since the buffers
  // are clear. This will also trigger the probe to start.
  EXPECT_CALL(packet_router, SendPacket).Times(AtLeast(1));
  pacer.CreateProbeClusters(
      {{.at_time = time_controller.GetClock()->CurrentTime(),
        .target_data_rate = kPacingDataRate * 2,
        .target_duration = TimeDelta::Millis(15),
        .target_probe_count = 5,
        .id = 17}});
  pacer.EnqueuePackets(GeneratePackets(RtpPacketMediaType::kVideo, 10));
  time_controller.AdvanceTime(TimeDelta::Zero());
  ::testing::Mock::VerifyAndClearExpectations(&packet_router);

  // Advance time to 1ms before the coalescing window ends. Packets should be
  // flying.
  EXPECT_CALL(packet_router, SendPacket).Times(AtLeast(1));
  time_controller.AdvanceTime(kCoalescingWindow - TimeDelta::Millis(1));
}

TEST(TaskQueuePacedSenderTest, SchedulesProbeAtSentTime) {
  ScopedKeyValueConfig trials(
      "WebRTC-Bwe-ProbingBehavior/min_probe_delta:1ms/");
  GlobalSimulatedTimeController time_controller(Timestamp::Millis(1234));
  NiceMock<MockPacketRouter> packet_router;
  TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router, trials,

                             PacingController::kMinSleepTime,
                             TaskQueuePacedSender::kNoPacketHoldback);
  pacer.SetSendBurstInterval(TimeDelta::Zero());

  // Set rates so one packet adds 4ms of buffer level.
  const DataSize kPacketSize = DataSize::Bytes(kDefaultPacketSize);
  const TimeDelta kPacketPacingTime = TimeDelta::Millis(4);
  const DataRate kPacingDataRate = kPacketSize / kPacketPacingTime;
  pacer.SetPacingRates(kPacingDataRate, /*padding_rate=*/DataRate::Zero());
  pacer.EnsureStarted();
  EXPECT_CALL(packet_router, FetchFec).WillRepeatedly([]() {
    return std::vector<std::unique_ptr<RtpPacketToSend>>();
  });
  EXPECT_CALL(packet_router, GeneratePadding(_))
      .WillRepeatedly(
          [](DataSize target_size) { return GeneratePadding(target_size); });

  // Enqueue two packets, only the first is sent immediately and the next
  // will be scheduled for sending in 4ms.
  pacer.EnqueuePackets(GeneratePackets(RtpPacketMediaType::kVideo, 2));
  const int kNotAProbe = PacedPacketInfo::kNotAProbe;
  EXPECT_CALL(packet_router,
              SendPacket(_, ::testing::Field(&PacedPacketInfo::probe_cluster_id,
                                             kNotAProbe)));
  // Advance to less than 3ms before next packet send time.
  time_controller.AdvanceTime(TimeDelta::Micros(1001));

  // Trigger a probe at 2x the current pacing rate and insert the number of
  // packets the probe needs.
  const DataRate kProbeRate = 2 * kPacingDataRate;
  const int kProbeClusterId = 1;
  pacer.CreateProbeClusters(
      {{.at_time = time_controller.GetClock()->CurrentTime(),
        .target_data_rate = kProbeRate,
        .target_duration = TimeDelta::Millis(15),
        .target_probe_count = 4,
        .id = kProbeClusterId}});

  // Expected size for each probe in a cluster is twice the expected bits sent
  // during min_probe_delta.
  // Expect one additional call since probe always starts with a small (1 byte)
  // padding packet that's not counted into the probe rate here.
  const TimeDelta kProbeTimeDelta = TimeDelta::Millis(2);
  const DataSize kProbeSize = kProbeRate * kProbeTimeDelta;
  const size_t kNumPacketsInProbe =
      (kProbeSize + kPacketSize - DataSize::Bytes(1)) / kPacketSize;
  EXPECT_CALL(packet_router,
              SendPacket(_, ::testing::Field(&PacedPacketInfo::probe_cluster_id,
                                             kProbeClusterId)))
      .Times(kNumPacketsInProbe + 1);

  pacer.EnqueuePackets(
      GeneratePackets(RtpPacketMediaType::kVideo, kNumPacketsInProbe));
  time_controller.AdvanceTime(TimeDelta::Zero());

  // The pacer should have scheduled the next probe to be sent in
  // kProbeTimeDelta. That there was existing scheduled call less than
  // PacingController::kMinSleepTime before this should not matter.
  EXPECT_CALL(packet_router,
              SendPacket(_, ::testing::Field(&PacedPacketInfo::probe_cluster_id,
                                             kProbeClusterId)))
      .Times(AtLeast(1));
  time_controller.AdvanceTime(TimeDelta::Millis(2));
}

TEST(TaskQueuePacedSenderTest, NoMinSleepTimeWhenProbing) {
  // Set min_probe_delta to be less than kMinSleepTime (1ms).
  const TimeDelta kMinProbeDelta = TimeDelta::Micros(200);
  ScopedKeyValueConfig trials;
  GlobalSimulatedTimeController time_controller(Timestamp::Millis(1234));
  MockPacketRouter packet_router;
  TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router, trials,

                             PacingController::kMinSleepTime,
                             TaskQueuePacedSender::kNoPacketHoldback);

  // Set rates so one packet adds 4ms of buffer level.
  const DataSize kPacketSize = DataSize::Bytes(kDefaultPacketSize);
  const TimeDelta kPacketPacingTime = TimeDelta::Millis(4);
  const DataRate kPacingDataRate = kPacketSize / kPacketPacingTime;
  pacer.SetPacingRates(kPacingDataRate, /*padding_rate=*/DataRate::Zero());
  pacer.EnsureStarted();
  EXPECT_CALL(packet_router, FetchFec).WillRepeatedly([]() {
    return std::vector<std::unique_ptr<RtpPacketToSend>>();
  });
  EXPECT_CALL(packet_router, GeneratePadding)
      .WillRepeatedly(
          [](DataSize target_size) { return GeneratePadding(target_size); });

  // Set a high probe rate.
  const int kProbeClusterId = 1;
  DataRate kProbingRate = kPacingDataRate * 10;

  pacer.CreateProbeClusters(
      {{.at_time = time_controller.GetClock()->CurrentTime(),
        .target_data_rate = kProbingRate,
        .target_duration = TimeDelta::Millis(15),
        .min_probe_delta = kMinProbeDelta,
        .target_probe_count = 5,
        .id = kProbeClusterId}});

  // Advance time less than PacingController::kMinSleepTime, probing packets
  // for the first millisecond should be sent immediately. Min delta between
  // probes is 200us, meaning 4 times per ms we will get least one call to
  // SendPacket().
  DataSize data_sent = DataSize::Zero();
  EXPECT_CALL(packet_router,
              SendPacket(_, ::testing::Field(&PacedPacketInfo::probe_cluster_id,
                                             kProbeClusterId)))
      .Times(AtLeast(4))
      .WillRepeatedly([&](std::unique_ptr<RtpPacketToSend> packet,
                          const PacedPacketInfo&) {
        data_sent +=
            DataSize::Bytes(packet->payload_size() + packet->padding_size());
      });

  // Add one packet to kickstart probing, the rest will be padding packets.
  pacer.EnqueuePackets(GeneratePackets(RtpPacketMediaType::kVideo, 1));
  time_controller.AdvanceTime(kMinProbeDelta);

  // Verify the amount of probing data sent.
  // Probe always starts with a small (1 byte) padding packet that's not
  // counted into the probe rate here.
  const DataSize kMinProbeSize = kMinProbeDelta * kProbingRate;
  EXPECT_EQ(data_sent, DataSize::Bytes(1) + kPacketSize + 4 * kMinProbeSize);
}

TEST(TaskQueuePacedSenderTest, PacketBasedCoalescing) {
  const TimeDelta kFixedCoalescingWindow = TimeDelta::Millis(10);
  const int kPacketBasedHoldback = 5;

  GlobalSimulatedTimeController time_controller(Timestamp::Millis(1234));
  NiceMock<MockPacketRouter> packet_router;
  ScopedKeyValueConfig trials;
  TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router, trials,

                             kFixedCoalescingWindow, kPacketBasedHoldback);
  pacer.SetSendBurstInterval(TimeDelta::Zero());

  // Set rates so one packet adds one ms of buffer level.
  const DataSize kPacketSize = DataSize::Bytes(kDefaultPacketSize);
  const TimeDelta kPacketPacingTime = TimeDelta::Millis(1);
  const DataRate kPacingDataRate = kPacketSize / kPacketPacingTime;
  const TimeDelta kExpectedHoldbackWindow =
      kPacketPacingTime * kPacketBasedHoldback;
  // `kFixedCoalescingWindow` sets the upper bound for the window.
  ASSERT_GE(kFixedCoalescingWindow, kExpectedHoldbackWindow);

  pacer.SetPacingRates(kPacingDataRate, DataRate::Zero());
  EXPECT_CALL(packet_router, FetchFec).WillRepeatedly([]() {
    return std::vector<std::unique_ptr<RtpPacketToSend>>();
  });
  pacer.EnsureStarted();

  // Add some packets and wait till all have been sent, so that the pacer
  // has a valid estimate of packet size.
  const int kNumWarmupPackets = 40;
  EXPECT_CALL(packet_router, SendPacket).Times(kNumWarmupPackets);
  pacer.EnqueuePackets(
      GeneratePackets(RtpPacketMediaType::kVideo, kNumWarmupPackets));
  // Wait until all packes have been sent, with a 2x margin.
  time_controller.AdvanceTime(kPacketPacingTime * (kNumWarmupPackets * 2));

  // Enqueue packets. Expect only the first one to be sent immediately.
  EXPECT_CALL(packet_router, SendPacket).Times(1);
  pacer.EnqueuePackets(
      GeneratePackets(RtpPacketMediaType::kVideo, kPacketBasedHoldback));
  time_controller.AdvanceTime(TimeDelta::Zero());

  // Advance time to 1ms before the coalescing window ends.
  EXPECT_CALL(packet_router, SendPacket).Times(0);
  time_controller.AdvanceTime(kExpectedHoldbackWindow - TimeDelta::Millis(1));

  // Advance past where the coalescing window should end.
  EXPECT_CALL(packet_router, SendPacket).Times(kPacketBasedHoldback - 1);
  time_controller.AdvanceTime(TimeDelta::Millis(1));
}

TEST(TaskQueuePacedSenderTest, FixedHoldBackHasPriorityOverPackets) {
  const TimeDelta kFixedCoalescingWindow = TimeDelta::Millis(2);
  const int kPacketBasedHoldback = 5;

  GlobalSimulatedTimeController time_controller(Timestamp::Millis(1234));
  MockPacketRouter packet_router;
  ScopedKeyValueConfig trials;
  TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router, trials,

                             kFixedCoalescingWindow, kPacketBasedHoldback);
  pacer.SetSendBurstInterval(TimeDelta::Zero());

  // Set rates so one packet adds one ms of buffer level.
  const DataSize kPacketSize = DataSize::Bytes(kDefaultPacketSize);
  const TimeDelta kPacketPacingTime = TimeDelta::Millis(1);
  const DataRate kPacingDataRate = kPacketSize / kPacketPacingTime;
  const TimeDelta kExpectedPacketHoldbackWindow =
      kPacketPacingTime * kPacketBasedHoldback;
  // |kFixedCoalescingWindow| sets the upper bound for the window.
  ASSERT_LT(kFixedCoalescingWindow, kExpectedPacketHoldbackWindow);

  pacer.SetPacingRates(kPacingDataRate, DataRate::Zero());
  EXPECT_CALL(packet_router, FetchFec).WillRepeatedly([]() {
    return std::vector<std::unique_ptr<RtpPacketToSend>>();
  });
  pacer.EnsureStarted();

  // Add some packets and wait till all have been sent, so that the pacer
  // has a valid estimate of packet size.
  const int kNumWarmupPackets = 40;
  EXPECT_CALL(packet_router, SendPacket).Times(kNumWarmupPackets);
  pacer.EnqueuePackets(
      GeneratePackets(RtpPacketMediaType::kVideo, kNumWarmupPackets));
  // Wait until all packes have been sent, with a 2x margin.
  time_controller.AdvanceTime(kPacketPacingTime * (kNumWarmupPackets * 2));

  // Enqueue packets. Expect onlt the first one to be sent immediately.
  EXPECT_CALL(packet_router, SendPacket).Times(1);
  pacer.EnqueuePackets(
      GeneratePackets(RtpPacketMediaType::kVideo, kPacketBasedHoldback));
  time_controller.AdvanceTime(TimeDelta::Zero());

  // Advance time to the fixed coalescing window, that should take presedence so
  // at least some of the packets should be sent.
  EXPECT_CALL(packet_router, SendPacket).Times(AtLeast(1));
  time_controller.AdvanceTime(kFixedCoalescingWindow);
}

TEST(TaskQueuePacedSenderTest, ProbingStopDuringSendLoop) {
  // Set a low `min_probe_delta` to let probing finish during send loop.
  ScopedKeyValueConfig trials(
      "WebRTC-Bwe-ProbingBehavior/min_probe_delta:100us/");

  GlobalSimulatedTimeController time_controller(Timestamp::Millis(1234));
  MockPacketRouter packet_router;
  TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router, trials,

                             PacingController::kMinSleepTime,
                             TaskQueuePacedSender::kNoPacketHoldback);

  // Set rates so 2 packets adds 1ms of buffer level.
  const DataSize kPacketSize = DataSize::Bytes(kDefaultPacketSize);
  const TimeDelta kPacketPacingTime = TimeDelta::Millis(1);
  const DataRate kPacingDataRate = 2 * kPacketSize / kPacketPacingTime;

  pacer.SetPacingRates(kPacingDataRate, DataRate::Zero());
  pacer.EnsureStarted();

  EXPECT_CALL(packet_router, FetchFec).WillRepeatedly([]() {
    return std::vector<std::unique_ptr<RtpPacketToSend>>();
  });
  EXPECT_CALL(packet_router, GeneratePadding(_))
      .WillRepeatedly(
          [](DataSize target_size) { return GeneratePadding(target_size); });

  // Set probe rate.
  const int kProbeClusterId = 1;
  const DataRate kProbingRate = kPacingDataRate;

  pacer.CreateProbeClusters(
      {{.at_time = time_controller.GetClock()->CurrentTime(),
        .target_data_rate = kProbingRate,
        .target_duration = TimeDelta::Millis(15),
        .target_probe_count = 4,
        .id = kProbeClusterId}});

  const int kPacketsToSend = 100;
  const TimeDelta kPacketsPacedTime =
      std::max(kPacketsToSend * kPacketSize / kPacingDataRate,
               kPacketsToSend * kPacketSize / kProbingRate);

  // Expect all packets and one padding packet sent.
  EXPECT_CALL(packet_router, SendPacket).Times(kPacketsToSend + 1);
  pacer.EnqueuePackets(
      GeneratePackets(RtpPacketMediaType::kVideo, kPacketsToSend));
  time_controller.AdvanceTime(kPacketsPacedTime + TimeDelta::Millis(1));
}

TEST(TaskQueuePacedSenderTest, PostedPacketsNotSendFromRemovePacketsForSsrc) {
  static constexpr Timestamp kStartTime = Timestamp::Millis(1234);
  GlobalSimulatedTimeController time_controller(kStartTime);
  ScopedKeyValueConfig trials;
  MockPacketRouter packet_router;
  TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router, trials,

                             PacingController::kMinSleepTime,
                             TaskQueuePacedSender::kNoPacketHoldback);

  static constexpr DataRate kPacingRate =
      DataRate::BytesPerSec(kDefaultPacketSize * 10);
  pacer.SetPacingRates(kPacingRate, DataRate::Zero());
  pacer.EnsureStarted();

  auto encoder_queue = time_controller.GetTaskQueueFactory()->CreateTaskQueue(
      "encoder_queue", TaskQueueFactory::Priority::HIGH);

  EXPECT_CALL(packet_router, SendPacket).Times(5);
  encoder_queue->PostTask([&pacer] {
    pacer.EnqueuePackets(GeneratePackets(RtpPacketMediaType::kVideo, 6));
  });

  time_controller.AdvanceTime(TimeDelta::Millis(400));
  // 1 packet left.
  EXPECT_EQ(pacer.OldestPacketWaitTime(), TimeDelta::Millis(400));
  EXPECT_EQ(pacer.FirstSentPacketTime(), kStartTime);

  // Enqueue packets while removing ssrcs should not send any more packets.
  encoder_queue->PostTask(
      [&pacer, worker_thread = time_controller.GetMainThread()] {
        worker_thread->PostTask(
            [&pacer] { pacer.RemovePacketsForSsrc(kVideoSsrc); });
        pacer.EnqueuePackets(GeneratePackets(RtpPacketMediaType::kVideo, 5));
      });
  time_controller.AdvanceTime(TimeDelta::Seconds(1));
  EXPECT_EQ(pacer.OldestPacketWaitTime(), TimeDelta::Zero());
  EXPECT_EQ(pacer.FirstSentPacketTime(), kStartTime);
  EXPECT_EQ(pacer.QueueSizeData(), DataSize::Zero());
  EXPECT_EQ(pacer.ExpectedQueueTime(), TimeDelta::Zero());
}

TEST(TaskQueuePacedSenderTest, Stats) {
  static constexpr Timestamp kStartTime = Timestamp::Millis(1234);
  GlobalSimulatedTimeController time_controller(kStartTime);
  NiceMock<MockPacketRouter> packet_router;
  ScopedKeyValueConfig trials;
  TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router, trials,

                             PacingController::kMinSleepTime,
                             TaskQueuePacedSender::kNoPacketHoldback);

  // Simulate ~2mbps video stream, covering one second.
  static constexpr size_t kPacketsToSend = 200;
  static constexpr DataRate kPacingRate =
      DataRate::BytesPerSec(kDefaultPacketSize * kPacketsToSend);
  pacer.SetPacingRates(kPacingRate, DataRate::Zero());
  pacer.EnsureStarted();

  // Allowed `QueueSizeData` and `ExpectedQueueTime` deviation.
  static constexpr size_t kAllowedPacketsDeviation = 1;
  static constexpr DataSize kAllowedQueueSizeDeviation =
      DataSize::Bytes(kDefaultPacketSize * kAllowedPacketsDeviation) +
      kPacingRate * PacingController::kDefaultBurstInterval;
  static constexpr TimeDelta kAllowedQueueTimeDeviation =
      kAllowedQueueSizeDeviation / kPacingRate;

  DataSize expected_queue_size = DataSize::MinusInfinity();
  TimeDelta expected_queue_time = TimeDelta::MinusInfinity();

  EXPECT_CALL(packet_router, SendPacket).Times(kPacketsToSend);

  // Stats before insert any packets.
  EXPECT_TRUE(pacer.OldestPacketWaitTime().IsZero());
  EXPECT_FALSE(pacer.FirstSentPacketTime().has_value());
  EXPECT_TRUE(pacer.QueueSizeData().IsZero());
  EXPECT_TRUE(pacer.ExpectedQueueTime().IsZero());

  pacer.EnqueuePackets(
      GeneratePackets(RtpPacketMediaType::kVideo, kPacketsToSend));

  // Advance to 200ms.
  time_controller.AdvanceTime(TimeDelta::Millis(200));
  EXPECT_EQ(pacer.OldestPacketWaitTime(), TimeDelta::Millis(200));
  EXPECT_EQ(pacer.FirstSentPacketTime(), kStartTime);

  expected_queue_size = kPacingRate * TimeDelta::Millis(800);
  expected_queue_time = expected_queue_size / kPacingRate;
  EXPECT_NEAR(pacer.QueueSizeData().bytes(), expected_queue_size.bytes(),
              kAllowedQueueSizeDeviation.bytes());
  EXPECT_NEAR(pacer.ExpectedQueueTime().ms(), expected_queue_time.ms(),
              kAllowedQueueTimeDeviation.ms());

  // Advance to 500ms.
  time_controller.AdvanceTime(TimeDelta::Millis(300));
  EXPECT_EQ(pacer.OldestPacketWaitTime(), TimeDelta::Millis(500));
  EXPECT_EQ(pacer.FirstSentPacketTime(), kStartTime);

  expected_queue_size = kPacingRate * TimeDelta::Millis(500);
  expected_queue_time = expected_queue_size / kPacingRate;
  EXPECT_NEAR(pacer.QueueSizeData().bytes(), expected_queue_size.bytes(),
              kAllowedQueueSizeDeviation.bytes());
  EXPECT_NEAR(pacer.ExpectedQueueTime().ms(), expected_queue_time.ms(),
              kAllowedQueueTimeDeviation.ms());

  // Advance to 1000ms+, expect all packets to be sent.
  time_controller.AdvanceTime(TimeDelta::Millis(500) +
                              kAllowedQueueTimeDeviation);
  EXPECT_TRUE(pacer.OldestPacketWaitTime().IsZero());
  EXPECT_EQ(pacer.FirstSentPacketTime(), kStartTime);
  EXPECT_TRUE(pacer.QueueSizeData().IsZero());
  EXPECT_TRUE(pacer.ExpectedQueueTime().IsZero());
}

}  // namespace test
}  // namespace webrtc
