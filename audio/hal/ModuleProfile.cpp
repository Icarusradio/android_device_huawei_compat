/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "audio.hisi_wrapper"

#include "ModuleProfile.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dlfcn.h>
#include <openssl/sha.h>

#include <log/log.h>

namespace huawei::audio {
namespace {

constexpr size_t kSha256HexLength = SHA256_DIGEST_LENGTH * 2;

int hexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool parseExpectedDigest(const char* hex, uint8_t digest[SHA256_DIGEST_LENGTH]) {
    if (hex == nullptr || strnlen(hex, kSha256HexLength + 1) != kSha256HexLength) return false;
    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        const int high = hexValue(hex[i * 2]);
        const int low = hexValue(hex[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        digest[i] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

}  // namespace

int verifyModuleSha256(const hw_module_t* module, const char* expectedHex) {
    uint8_t expected[SHA256_DIGEST_LENGTH];
    if (module == nullptr || !parseExpectedDigest(expectedHex, expected)) {
        ALOGE("Invalid module or expected SHA-256 profile");
        return -EINVAL;
    }

    Dl_info info{};
    if (dladdr(module, &info) == 0 || info.dli_fname == nullptr) {
        ALOGE("Unable to resolve loaded audio module path");
        return -ENOENT;
    }

    int fd;
    do {
        fd = open(info.dli_fname, O_RDONLY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        const int error = errno;
        ALOGE("Unable to open loaded audio module %s: %s", info.dli_fname, strerror(error));
        return -error;
    }

    int result = 0;
    struct stat fileStat {};
    if (fstat(fd, &fileStat) != 0) {
        result = -errno;
        ALOGE("Unable to stat loaded audio module %s: %s", info.dli_fname, strerror(errno));
    } else if (!S_ISREG(fileStat.st_mode)) {
        result = -EINVAL;
        ALOGE("Loaded audio module path is not a regular file: %s", info.dli_fname);
    } else {
        SHA256_CTX context;
        SHA256_Init(&context);
        uint8_t buffer[16 * 1024];
        for (;;) {
            const ssize_t count = read(fd, buffer, sizeof(buffer));
            if (count > 0) {
                SHA256_Update(&context, buffer, static_cast<size_t>(count));
                continue;
            }
            if (count == 0) break;
            if (errno == EINTR) continue;
            result = -errno;
            ALOGE("Unable to read loaded audio module %s: %s", info.dli_fname, strerror(errno));
            break;
        }

        if (result == 0) {
            uint8_t actual[SHA256_DIGEST_LENGTH];
            SHA256_Final(actual, &context);
            uint8_t difference = 0;
            for (size_t i = 0; i < sizeof(actual); ++i) difference |= actual[i] ^ expected[i];
            if (difference != 0) {
                result = -ENODEV;
                ALOGE("Loaded audio module SHA-256 does not match the supported ABI profile: %s",
                      info.dli_fname);
            }
        }
    }

    if (close(fd) != 0) {
        const int error = errno;
        ALOGE("Unable to close loaded audio module %s: %s", info.dli_fname, strerror(error));
        if (result == 0) result = -error;
    }
    return result;
}

}  // namespace huawei::audio
