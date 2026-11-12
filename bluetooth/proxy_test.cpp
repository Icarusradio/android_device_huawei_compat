/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bluetooth_hci_proxy.h"

#include <gtest/gtest.h>
#include <utils/Errors.h>

#include <array>
#include <vector>

namespace android::hardware::bluetooth::V1_0 {
namespace {

hidl_vec<uint8_t> Packet(const std::vector<uint8_t> &bytes) {
  return hidl_vec<uint8_t>(bytes);
}

::android::hardware::Status DeadObjectStatus() {
  return ::android::hardware::Status::fromStatusT(DEAD_OBJECT);
}

class TestDeathRecipient final : public hidl_death_recipient {
public:
  void serviceDied(uint64_t, const wp<hidl::base::V1_0::IBase> &) override {}
};

class RecordingCallbacks final : public IBluetoothHciCallbacks {
public:
  Return<void> initializationComplete(Status value) override {
    status = value;
    ++initialization_count;
    if (fail_initialization)
      return DeadObjectStatus();
    return {};
  }
  Return<void> hciEventReceived(const hidl_vec<uint8_t> &value) override {
    event = value;
    ++event_count;
    if (fail_event)
      return DeadObjectStatus();
    return {};
  }
  Return<void> aclDataReceived(const hidl_vec<uint8_t> &value) override {
    acl = value;
    ++acl_count;
    if (fail_data)
      return DeadObjectStatus();
    return {};
  }
  Return<void> scoDataReceived(const hidl_vec<uint8_t> &value) override {
    sco = value;
    ++sco_count;
    if (fail_data)
      return DeadObjectStatus();
    return {};
  }
  Return<bool> linkToDeath(const sp<hidl_death_recipient> &value,
                           uint64_t value_cookie) override {
    linked_recipient = value;
    cookie = value_cookie;
    ++link_count;
    if (fail_death)
      return DeadObjectStatus();
    return death_result;
  }
  Return<bool> unlinkToDeath(const sp<hidl_death_recipient> &value) override {
    unlinked_recipient = value;
    ++unlink_count;
    if (fail_death)
      return DeadObjectStatus();
    return death_result;
  }

  Status status = Status::UNKNOWN;
  hidl_vec<uint8_t> event;
  hidl_vec<uint8_t> acl;
  hidl_vec<uint8_t> sco;
  sp<hidl_death_recipient> linked_recipient;
  sp<hidl_death_recipient> unlinked_recipient;
  uint64_t cookie = 0;
  int initialization_count = 0;
  int event_count = 0;
  int acl_count = 0;
  int sco_count = 0;
  int link_count = 0;
  int unlink_count = 0;
  bool fail_initialization = false;
  bool fail_event = false;
  bool fail_data = false;
  bool fail_death = false;
  bool death_result = true;
};

class RecordingHci final : public IBluetoothHci {
public:
  Return<void> initialize(const sp<IBluetoothHciCallbacks> &value) override {
    callback = value;
    ++initialize_count;
    if (fail)
      return DeadObjectStatus();
    return {};
  }
  Return<void> sendHciCommand(const hidl_vec<uint8_t> &value) override {
    command = value;
    ++command_count;
    if (fail)
      return DeadObjectStatus();
    return {};
  }
  Return<void> sendAclData(const hidl_vec<uint8_t> &value) override {
    acl = value;
    ++acl_count;
    if (fail)
      return DeadObjectStatus();
    return {};
  }
  Return<void> sendScoData(const hidl_vec<uint8_t> &value) override {
    sco = value;
    ++sco_count;
    if (fail)
      return DeadObjectStatus();
    return {};
  }
  Return<void> close() override {
    ++close_count;
    if (fail)
      return DeadObjectStatus();
    return {};
  }

  sp<IBluetoothHciCallbacks> callback;
  hidl_vec<uint8_t> command;
  hidl_vec<uint8_t> acl;
  hidl_vec<uint8_t> sco;
  int initialize_count = 0;
  int command_count = 0;
  int acl_count = 0;
  int sco_count = 0;
  int close_count = 0;
  bool fail = false;
};

TEST(FeatureMaskTest, MasksOnlyPageZeroExtendedFeature54) {
  auto event = Packet({0x0e, 0x0e, 0x01, 0x04, 0x10, 0x00, 0x00, 0x02, 0xbf,
                       0xfe, 0xcd, 0xfe, 0xdb, 0xff, 0x7b, 0x87});
  const auto original = event;
  EXPECT_TRUE(maskLocalNonFlushablePbFeature(&event));
  EXPECT_EQ(event[14], 0x3b);
  EXPECT_EQ(event.size(), original.size());
  for (size_t i = 0; i < event.size(); ++i) {
    if (i != 14)
      EXPECT_EQ(event[i], original[i]);
  }
  EXPECT_EQ(original[14], 0x7b);
  EXPECT_FALSE(maskLocalNonFlushablePbFeature(&event));
}

TEST(FeatureMaskTest, MasksLocalSupportedFeatures) {
  auto event = Packet({0x0e, 0x0c, 0x01, 0x03, 0x10, 0x00, 0xbf, 0xfe, 0xcd,
                       0xfe, 0xdb, 0xff, 0x7b, 0x87});
  EXPECT_TRUE(maskLocalNonFlushablePbFeature(&event));
  EXPECT_EQ(event[12], 0x3b);
}

TEST(FeatureMaskTest, LeavesInvalidOrUnrelatedEventsUnchanged) {
  const std::array<std::vector<uint8_t>, 9> invalid = {{
      {0x0e, 0x0e, 0x01, 0x04, 0x10, 0x00, 0x01, 0x02, 0xbf, 0xfe, 0xcd, 0xfe,
       0xdb, 0xff, 0x7b, 0x87},
      {0x0e, 0x0e, 0x01, 0x04, 0x10, 0x01, 0x00, 0x02, 0xbf, 0xfe, 0xcd, 0xfe,
       0xdb, 0xff, 0x7b, 0x87},
      {0x0e, 0x0e, 0x01, 0x05, 0x10, 0x00, 0x00, 0x02, 0xbf, 0xfe, 0xcd, 0xfe,
       0xdb, 0xff, 0x7b, 0x87},
      {0x0f, 0x0e, 0x01, 0x04, 0x10, 0x00, 0x00, 0x02, 0xbf, 0xfe, 0xcd, 0xfe,
       0xdb, 0xff, 0x7b, 0x87},
      {0x0e, 0x0f, 0x01, 0x04, 0x10, 0x00, 0x00, 0x02, 0xbf, 0xfe, 0xcd, 0xfe,
       0xdb, 0xff, 0x7b, 0x87, 0x00},
      {0x0e, 0x0e, 0x01, 0x04, 0x10, 0x00, 0x00, 0x02, 0xbf},
      {0x0e, 0x0c, 0x01, 0x03, 0x10},
      {0x0e, 0x0d, 0x01, 0x04, 0x10, 0x00, 0x00, 0x02, 0xbf, 0xfe, 0xcd, 0xfe,
       0xdb, 0xff, 0x7b, 0x87},
      {0x0e, 0x0e, 0x01, 0x04, 0x10, 0x00, 0x00, 0x02, 0xbf, 0xfe, 0xcd, 0xfe,
       0xdb, 0xff, 0x3b, 0x87},
  }};
  for (const auto &bytes : invalid) {
    auto event = Packet(std::vector<uint8_t>(bytes));
    const auto original = event;
    EXPECT_FALSE(maskLocalNonFlushablePbFeature(&event));
    EXPECT_EQ(event, original);
  }
  hidl_vec<uint8_t> empty;
  EXPECT_FALSE(maskLocalNonFlushablePbFeature(&empty));
  EXPECT_FALSE(maskLocalNonFlushablePbFeature(nullptr));
}

TEST(FilteredCallbacksTest, MasksCopyAndForwardsOtherMethodsAndDeathLifecycle) {
  sp<RecordingCallbacks> target = new RecordingCallbacks();
  sp<FilteredCallbacks> filtered = new FilteredCallbacks(target);
  auto event = Packet({0x0e, 0x0e, 0x01, 0x04, 0x10, 0x00, 0x00, 0x02, 0xbf,
                       0xfe, 0xcd, 0xfe, 0xdb, 0xff, 0x7b, 0x87});
  const auto original = event;
  EXPECT_TRUE(filtered->hciEventReceived(event).isOk());
  EXPECT_EQ(event, original);
  EXPECT_EQ(target->event[14], 0x3b);
  EXPECT_TRUE(filtered->initializationComplete(Status::SUCCESS).isOk());
  EXPECT_TRUE(filtered->aclDataReceived(Packet({1, 2})).isOk());
  EXPECT_TRUE(filtered->scoDataReceived(Packet({3, 4})).isOk());
  EXPECT_EQ(target->initialization_count, 1);
  EXPECT_EQ(target->event_count, 1);
  EXPECT_EQ(target->acl_count, 1);
  EXPECT_EQ(target->sco_count, 1);
  EXPECT_EQ(target->status, Status::SUCCESS);
  EXPECT_EQ(target->acl, Packet({1, 2}));
  EXPECT_EQ(target->sco, Packet({3, 4}));

  sp<TestDeathRecipient> recipient = new TestDeathRecipient();
  EXPECT_TRUE(filtered->linkToDeath(recipient, 0x12345678).withDefault(false));
  EXPECT_TRUE(filtered->unlinkToDeath(recipient).withDefault(false));
  EXPECT_EQ(target->linked_recipient, recipient);
  EXPECT_EQ(target->unlinked_recipient, recipient);
  EXPECT_EQ(target->cookie, 0x12345678u);
  EXPECT_EQ(target->link_count, 1);
  EXPECT_EQ(target->unlink_count, 1);

  target->fail_initialization = true;
  EXPECT_FALSE(filtered->initializationComplete(Status::SUCCESS).isOk());
  target->fail_event = true;
  EXPECT_FALSE(filtered->hciEventReceived(Packet({0x01})).isOk());
}

TEST(BluetoothHciProxyTest, ForwardsInitializationCommandsDataAndClose) {
  sp<RecordingHci> target = new RecordingHci();
  sp<BluetoothHciProxy> proxy = new BluetoothHciProxy(target);
  sp<RecordingCallbacks> callback = new RecordingCallbacks();
  EXPECT_TRUE(proxy->initialize(callback).isOk());
  EXPECT_EQ(target->initialize_count, 1);
  EXPECT_NE(target->callback, callback);
  EXPECT_TRUE(target->callback->initializationComplete(Status::SUCCESS).isOk());
  EXPECT_EQ(callback->initialization_count, 1);

  const auto command = Packet({0x5f, 0x0c, 0x03, 0x01, 0x00, 0x00});
  EXPECT_TRUE(proxy->sendHciCommand(command).isOk());
  EXPECT_EQ(target->command, command);
  EXPECT_EQ(target->command_count, 1);
  EXPECT_TRUE(proxy->sendAclData(Packet({5, 6})).isOk());
  EXPECT_TRUE(proxy->sendScoData(Packet({7, 8})).isOk());
  EXPECT_EQ(target->acl_count, 1);
  EXPECT_EQ(target->sco_count, 1);
  EXPECT_EQ(target->acl, Packet({5, 6}));
  EXPECT_EQ(target->sco, Packet({7, 8}));
  EXPECT_TRUE(proxy->close().isOk());
  EXPECT_EQ(target->close_count, 1);

  sp<RecordingHci> null_target = new RecordingHci();
  sp<BluetoothHciProxy> null_proxy = new BluetoothHciProxy(null_target);
  EXPECT_TRUE(null_proxy->initialize(nullptr).isOk());
  EXPECT_EQ(null_target->callback, nullptr);
}

TEST(FeatureMaskTest, RejectsEveryTruncationEvenWithAdjustedLength) {
  for (const auto &complete :
       {std::vector<uint8_t>{0x0e, 0x0e, 0x01, 0x04, 0x10, 0x00, 0x00, 0x02,
                             0xbf, 0xfe, 0xcd, 0xfe, 0xdb, 0xff, 0x7b, 0x87},
        std::vector<uint8_t>{0x0e, 0x0c, 0x01, 0x03, 0x10, 0x00, 0xbf, 0xfe,
                             0xcd, 0xfe, 0xdb, 0xff, 0x7b, 0x87}}) {
    for (size_t size = 0; size < complete.size(); ++size) {
      auto event = Packet(
          std::vector<uint8_t>(complete.begin(), complete.begin() + size));
      const auto original = event;
      EXPECT_FALSE(maskLocalNonFlushablePbFeature(&event));
      EXPECT_EQ(event, original);
      if (size >= 2) {
        event[1] = size - 2;
        const auto adjusted = event;
        EXPECT_FALSE(maskLocalNonFlushablePbFeature(&event));
        EXPECT_EQ(event, adjusted);
      }
    }
  }
}

TEST(FilteredCallbacksTest, PreservesDeathLinkResultsAndTransportErrors) {
  sp<RecordingCallbacks> target = new RecordingCallbacks();
  sp<FilteredCallbacks> filtered = new FilteredCallbacks(target);
  sp<TestDeathRecipient> recipient = new TestDeathRecipient();
  target->death_result = false;
  EXPECT_FALSE(filtered->linkToDeath(recipient, 42).withDefault(true));
  EXPECT_FALSE(filtered->unlinkToDeath(recipient).withDefault(true));
  target->fail_death = true;
  EXPECT_FALSE(filtered->linkToDeath(recipient, 42).isOk());
  EXPECT_FALSE(filtered->unlinkToDeath(recipient).isOk());
  target->fail_initialization = true;
  target->fail_event = true;
  target->fail_data = true;
  EXPECT_FALSE(filtered->initializationComplete(Status::SUCCESS).isOk());
  EXPECT_FALSE(filtered->hciEventReceived(Packet({0x10, 0x01, 0x03})).isOk());
  EXPECT_FALSE(filtered
                   ->hciEventReceived(
                       Packet({0x0e, 0x0e, 0x01, 0x04, 0x10, 0x00, 0x00, 0x02,
                               0xbf, 0xfe, 0xcd, 0xfe, 0xdb, 0xff, 0x7b, 0x87}))
                   .isOk());
  EXPECT_FALSE(filtered->aclDataReceived(Packet({1, 2})).isOk());
  EXPECT_FALSE(filtered->scoDataReceived(Packet({3, 4})).isOk());
}

TEST(BluetoothHciProxyTest, PreservesImplementationTransportErrors) {
  sp<RecordingHci> target = new RecordingHci();
  sp<BluetoothHciProxy> proxy = new BluetoothHciProxy(target);
  sp<RecordingCallbacks> callback = new RecordingCallbacks();
  target->fail = true;
  EXPECT_FALSE(proxy->initialize(callback).isOk());
  EXPECT_FALSE(
      proxy->sendHciCommand(Packet({0x5f, 0x0c, 0x03, 0x01, 0, 0})).isOk());
  EXPECT_FALSE(proxy->sendAclData(Packet({1, 2})).isOk());
  EXPECT_FALSE(proxy->sendScoData(Packet({3, 4})).isOk());
  EXPECT_FALSE(proxy->close().isOk());
}

} // namespace
} // namespace android::hardware::bluetooth::V1_0
