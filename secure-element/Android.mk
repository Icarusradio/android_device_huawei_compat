# SPDX-License-Identifier: Apache-2.0

LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE),mediapadm5lte)

huawei_se_upstream := packages/apps/SecureElement
huawei_se_upstream_relative := ../../../../$(huawei_se_upstream)

# This replacement was based on android-13.0.0_r75 (also r84), commit
# 7e9d165899058103fa612ffb41858e4fd975182b. Do not silently combine a stale
# Terminal with a changed upstream implementation when updating the manifest.
huawei_se_terminal_sha256 := f77c52dd82906670d20df6b25a48e390204c67f9e9a605ae3f65e2531b675e96
ifneq ($(word 1,$(shell sha256sum $(huawei_se_upstream)/src/com/android/se/Terminal.java)),$(huawei_se_terminal_sha256))
$(error SecureElement upstream Terminal.java changed; review the Huawei replacement and update its baseline checksum)
endif

include $(CLEAR_VARS)
LOCAL_PACKAGE_NAME := SecureElementHuawei
LOCAL_OVERRIDES_PACKAGES := SecureElement
LOCAL_MODULE_TAGS := optional
LOCAL_PRIVATE_PLATFORM_APIS := true
LOCAL_CERTIFICATE := platform
LOCAL_PROGUARD_ENABLED := disabled
LOCAL_FULL_MANIFEST_FILE := $(huawei_se_upstream)/AndroidManifest.xml
LOCAL_RESOURCE_DIR := $(huawei_se_upstream)/res
LOCAL_NOTICE_FILE := $(huawei_se_upstream)/NOTICE
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0 SPDX-license-identifier-BSD
LOCAL_LICENSE_CONDITIONS := notice

LOCAL_SRC_FILES := $(filter-out \
    $(huawei_se_upstream_relative)/src/com/android/se/Terminal.java, \
    $(call all-java-files-under,$(huawei_se_upstream_relative)/src))
LOCAL_SRC_FILES += src/com/android/se/Terminal.java
LOCAL_STATIC_JAVA_LIBRARIES := \
    android.hardware.secure_element-V1.0-java \
    android.hardware.secure_element-V1.1-java \
    android.hardware.secure_element-V1.2-java

# Generate the same stats class as the upstream Soong module, using the host
# tool instead of depending on Soong's private intermediate directory layout.
huawei_se_statslog_java := $(call intermediates-dir-for,APPS,$(LOCAL_PACKAGE_NAME),,COMMON)/src/com/android/se/SecureElementStatsLog.java
LOCAL_GENERATED_SOURCES := $(huawei_se_statslog_java)
$(huawei_se_statslog_java): PRIVATE_STATSLOG_TOOL := $(HOST_OUT_EXECUTABLES)/stats-log-api-gen$(HOST_EXECUTABLE_SUFFIX)
$(huawei_se_statslog_java): $(HOST_OUT_EXECUTABLES)/stats-log-api-gen$(HOST_EXECUTABLE_SUFFIX) $(LOCAL_PATH)/Android.mk
	@mkdir -p $(dir $@)
	$(hide) $(PRIVATE_STATSLOG_TOOL) --java $@ --module secure_element --javaPackage com.android.se --javaClass SecureElementStatsLog

include $(BUILD_PACKAGE)

include $(call all-makefiles-under,$(LOCAL_PATH))

endif
