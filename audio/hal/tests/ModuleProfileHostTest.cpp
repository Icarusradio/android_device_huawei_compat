// SPDX-License-Identifier: Apache-2.0

#include "../ModuleProfile.h"

#include <errno.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <array>
#include <cctype>

extern "C" int __android_log_print(int priority, const char* tag, const char* format, ...) {
    (void)priority;
    (void)tag;
    (void)format;
    return 0;
}

hw_module_t gModule{};

#define CHECK(condition)                                                                          \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d CHECK(%s) failed\n", __FILE__, __LINE__, #condition);         \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

int openFdCount() {
    DIR* directory = opendir("/proc/self/fd");
    if (directory == nullptr) return -1;

    int count = 0;
    while (dirent* entry = readdir(directory)) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) ++count;
    }
    if (closedir(directory) != 0) return -1;
    return count;
}

bool testKnownModuleProfiles(const char* expectedHex) {
    CHECK(huawei::audio::verifyModuleSha256(&gModule, expectedHex) == 0);

    std::array<char, 65> uppercase{};
    for (size_t i = 0; i < 64; ++i) {
        uppercase[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(expectedHex[i])));
    }
    CHECK(huawei::audio::verifyModuleSha256(&gModule, uppercase.data()) == 0);

    std::array<char, 65> wrongHex{};
    memcpy(wrongHex.data(), expectedHex, 64);
    wrongHex[0] = wrongHex[0] == '0' ? '1' : '0';
    CHECK(huawei::audio::verifyModuleSha256(&gModule, wrongHex.data()) == -ENODEV);
    return true;
}

bool testInvalidProfilesAndModules(const char* expectedHex) {
    std::array<char, 64> shortHex{};
    memcpy(shortHex.data(), expectedHex, 63);
    std::array<char, 66> longHex{};
    memcpy(longHex.data(), expectedHex, 64);
    longHex[64] = '0';
    longHex[65] = '\0';
    std::array<char, 65> nonHex{};
    memcpy(nonHex.data(), expectedHex, 64);
    nonHex[17] = 'g';

    CHECK(huawei::audio::verifyModuleSha256(&gModule, shortHex.data()) == -EINVAL);
    CHECK(huawei::audio::verifyModuleSha256(&gModule, longHex.data()) == -EINVAL);
    CHECK(huawei::audio::verifyModuleSha256(&gModule, nonHex.data()) == -EINVAL);
    CHECK(huawei::audio::verifyModuleSha256(&gModule, nullptr) == -EINVAL);
    CHECK(huawei::audio::verifyModuleSha256(nullptr, expectedHex) == -EINVAL);

    hw_module_t stackModule{};
    CHECK(huawei::audio::verifyModuleSha256(&stackModule, expectedHex) == -ENOENT);

    void* heapAddress = malloc(sizeof(hw_module_t));
    CHECK(heapAddress != nullptr);
    memset(heapAddress, 0, sizeof(hw_module_t));
    const int heapResult = huawei::audio::verifyModuleSha256(
            static_cast<const hw_module_t*>(heapAddress), expectedHex);
    free(heapAddress);
    CHECK(heapResult == -ENOENT);
    return true;
}

bool testRepeatedCallsDoNotLeakFileDescriptors(const char* expectedHex) {
    const int before = openFdCount();
    CHECK(before >= 0);

    for (int i = 0; i < 4; ++i) {
        CHECK(huawei::audio::verifyModuleSha256(&gModule, expectedHex) == 0);
        CHECK(huawei::audio::verifyModuleSha256(&gModule, "00000000000000000000000000000000"
                                                           "00000000000000000000000000000000") ==
              -ENODEV);
        CHECK(huawei::audio::verifyModuleSha256(&gModule, "bad") == -EINVAL);
    }

    const int after = openFdCount();
    CHECK(after == before);
    return true;
}

int main(int argc, char** argv) {
    if (argc != 2 || strlen(argv[1]) != 64) {
        fprintf(stderr, "usage: %s <sha256-of-this-executable>\n", argv[0]);
        return 2;
    }
    for (const char* digit = argv[1]; *digit != '\0'; ++digit) {
        if (!std::isxdigit(static_cast<unsigned char>(*digit))) {
            fprintf(stderr, "expected SHA-256 argument must be hexadecimal\n");
            return 2;
        }
    }

    if (!testKnownModuleProfiles(argv[1])) return 1;
    if (!testInvalidProfilesAndModules(argv[1])) return 1;
    if (!testRepeatedCallsDoNotLeakFileDescriptors(argv[1])) return 1;
    puts("ModuleProfile host tests passed");
    return 0;
}
