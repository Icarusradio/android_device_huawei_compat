# SPDX-License-Identifier: Apache-2.0

LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE),mediapadm5lte)

include $(CLEAR_VARS)
LOCAL_PACKAGE_NAME := SecureElementHuaweiInstrumentationTests
LOCAL_MODULE_TAGS := tests
LOCAL_PRIVATE_PLATFORM_APIS := true
LOCAL_CERTIFICATE := platform
LOCAL_PROGUARD_ENABLED := disabled
LOCAL_INSTRUMENTATION_FOR := SecureElementHuawei
LOCAL_COMPATIBILITY_SUITE := device-tests
LOCAL_FULL_MANIFEST_FILE := packages/apps/SecureElement/tests/unit/AndroidManifest.xml
LOCAL_FULL_TEST_CONFIG := $(LOCAL_PATH)/AndroidTest.xml
LOCAL_NOTICE_FILE := packages/apps/SecureElement/NOTICE
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0 SPDX-license-identifier-BSD
LOCAL_LICENSE_CONDITIONS := notice
LOCAL_SRC_FILES := $(call all-java-files-under,src)
LOCAL_SRC_FILES += $(filter-out \
    ../../../../../packages/apps/SecureElement/tests/unit/src/com/android/se/TerminalStateCallbackTest.java, \
    $(call all-java-files-under,../../../../../packages/apps/SecureElement/tests/unit/src))
LOCAL_JAVA_LIBRARIES := \
    android.test.runner \
    android.test.base \
    android.test.mock
LOCAL_STATIC_ANDROID_LIBRARIES := \
    androidx.test.core \
    androidx.test.rules \
    androidx.test.ext.junit
LOCAL_STATIC_JAVA_LIBRARIES := \
    mockito-target \
    truth-prebuilt

include $(BUILD_PACKAGE)

endif
