/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <utils/Looper.h>

namespace {

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        std::abort(); \
    } \
} while (false)

constexpr char kPrepareSymbol[] = "_ZN7android6Looper7prepareEi";
constexpr char kGetSymbol[] = "_ZN7android6Looper12getForThreadEv";
constexpr char kSetSymbol[] = "_ZN7android6Looper12setForThreadERKNS_2spIS0_EE";

class CountingLooper final : public android::Looper {
public:
    explicit CountingLooper(std::atomic<int>* destructionCount)
          : Looper(false), mDestructionCount(destructionCount) {}

protected:
    ~CountingLooper() override {
        mDestructionCount->fetch_add(1, std::memory_order_relaxed);
    }

private:
    std::atomic<int>* mDestructionCount;
};

struct ThreadContext {
    std::atomic<bool>* ready;
    std::atomic<bool>* release;
    std::atomic<android::Looper*>* value;
    std::atomic<int>* destructionCount;
};

void* runThreadIsolationCheck(void* rawContext) {
    auto* context = static_cast<ThreadContext*>(rawContext);
    android::sp<CountingLooper> owned =
            android::sp<CountingLooper>::make(context->destructionCount);
    android::Looper::setForThread(owned);
    context->value->store(android::Looper::getForThread().get(), std::memory_order_release);
    context->ready->store(true, std::memory_order_release);
    while (!context->release->load(std::memory_order_acquire)) {
        sched_yield();
    }
    owned.clear();
    return nullptr;
}

void checkSymbolScope() {
    void* const libutilsHandle = dlopen("libutils.so", RTLD_NOW | RTLD_NOLOAD);
    CHECK(libutilsHandle != nullptr);

    void* const libutilsPrepare = dlsym(libutilsHandle, kPrepareSymbol);
    void* const libutilsGet = dlsym(libutilsHandle, kGetSymbol);
    void* const libutilsSet = dlsym(libutilsHandle, kSetSymbol);
    CHECK(libutilsPrepare != nullptr && libutilsGet != nullptr && libutilsSet != nullptr);

    // Taking these addresses creates normal relocations from this test binary.
    // Verify the relocation result, rather than dlsym(RTLD_DEFAULT), whose
    // linear lookup order differs from Bionic's relocation global-group order.
    void* const relocatedPrepare = reinterpret_cast<void*>(&android::Looper::prepare);
    void* const relocatedGet = reinterpret_cast<void*>(&android::Looper::getForThread);
    void* const relocatedSet = reinterpret_cast<void*>(&android::Looper::setForThread);
    Dl_info owner{};
    CHECK(dladdr(relocatedPrepare, &owner) != 0 && owner.dli_fname != nullptr);

#if defined(LOOPER_TLS_TEST_COMPAT)
    CHECK(relocatedPrepare != libutilsPrepare);
    CHECK(relocatedGet != libutilsGet);
    CHECK(relocatedSet != libutilsSet);
    CHECK(std::strstr(owner.dli_fname, "libhloop.so") != nullptr);
#else
    CHECK(relocatedPrepare == libutilsPrepare);
    CHECK(relocatedGet == libutilsGet);
    CHECK(relocatedSet == libutilsSet);
    CHECK(std::strstr(owner.dli_fname, "libutils.so") != nullptr);
#endif

    CHECK(dlclose(libutilsHandle) == 0);
}

void checkDlopenWorker() {
    void* const fixture = dlopen("looper_tls_dlopen_fixture.so", RTLD_NOW | RTLD_LOCAL);
    CHECK(fixture != nullptr);

    using ResultFn = int (*)();
    auto result = reinterpret_cast<ResultFn>(dlsym(fixture, "looperDlopenFixtureResult"));
    CHECK(result != nullptr);
    CHECK(result() == 0);
    CHECK(dlclose(fixture) == 0);
}

void checkSetReplacementAndThreadExit() {
    std::atomic<int> destructionCount{0};
    android::sp<CountingLooper> first = android::sp<CountingLooper>::make(&destructionCount);
    android::sp<CountingLooper> replacement =
            android::sp<CountingLooper>::make(&destructionCount);

    CHECK(first->getStrongCount() == 1);
    android::Looper::setForThread(first);
    CHECK(first->getStrongCount() == 2);
    {
        android::sp<android::Looper> current = android::Looper::getForThread();
        CHECK(current.get() == first.get());
        CHECK(first->getStrongCount() == 3);
    }
    CHECK(first->getStrongCount() == 2);

    android::Looper::setForThread(first);
    CHECK(first->getStrongCount() == 2);

    android::Looper::setForThread(replacement);
    CHECK(first->getStrongCount() == 1);
    CHECK(replacement->getStrongCount() == 2);

    android::Looper::setForThread(nullptr);
    CHECK(replacement->getStrongCount() == 1);
    replacement.clear();
    CHECK(destructionCount.load(std::memory_order_relaxed) == 1);

    android::Looper::setForThread(nullptr);
    first.clear();
    CHECK(destructionCount.load(std::memory_order_relaxed) == 2);

    android::sp<android::Looper> prepared =
            android::Looper::prepare(android::Looper::PREPARE_ALLOW_NON_CALLBACKS);
    CHECK(prepared != nullptr);
    CHECK(prepared->getAllowNonCallbacks());
    CHECK(android::Looper::prepare(android::Looper::PREPARE_ALLOW_NON_CALLBACKS).get() ==
           prepared.get());
    CHECK(android::Looper::getForThread().get() == prepared.get());
    prepared->wake();
    CHECK(prepared->pollOnce(0) == android::Looper::POLL_WAKE);
    android::Looper::setForThread(nullptr);
}

void checkThreadIsolationAndDestructors() {
    std::atomic<int> destructionCount{0};
    std::atomic<bool> readyA{false};
    std::atomic<bool> readyB{false};
    std::atomic<bool> release{false};
    std::atomic<android::Looper*> looperA{nullptr};
    std::atomic<android::Looper*> looperB{nullptr};
    ThreadContext contextA{&readyA, &release, &looperA, &destructionCount};
    ThreadContext contextB{&readyB, &release, &looperB, &destructionCount};
    pthread_t threadA;
    pthread_t threadB;

    CHECK(pthread_create(&threadA, nullptr, runThreadIsolationCheck, &contextA) == 0);
    CHECK(pthread_create(&threadB, nullptr, runThreadIsolationCheck, &contextB) == 0);
    while (!readyA.load(std::memory_order_acquire) || !readyB.load(std::memory_order_acquire)) {
        sched_yield();
    }
    CHECK(looperA.load(std::memory_order_acquire) != nullptr);
    CHECK(looperB.load(std::memory_order_acquire) != nullptr);
    CHECK(looperA.load(std::memory_order_relaxed) != looperB.load(std::memory_order_relaxed));

    release.store(true, std::memory_order_release);
    CHECK(pthread_join(threadA, nullptr) == 0);
    CHECK(pthread_join(threadB, nullptr) == 0);
    CHECK(destructionCount.load(std::memory_order_relaxed) == 2);
}

}  // namespace

int main() {
    checkSymbolScope();
    std::fprintf(stderr, "Looper TLS test: symbol scope passed; about to dlopen fixture\n");
    checkDlopenWorker();
    std::fprintf(stderr, "Looper TLS test: dlopen fixture passed\n");
    checkSetReplacementAndThreadExit();
    checkThreadIsolationAndDestructors();
    std::puts("Looper TLS host test passed");
    return 0;
}
