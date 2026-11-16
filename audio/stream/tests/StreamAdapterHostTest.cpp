// SPDX-License-Identifier: Apache-2.0
#include "huawei_audio_stream/StreamAdapter.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

using namespace huawei::audio::stream;

namespace {
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d CHECK(%s) failed\n", \
    __FILE__, __LINE__, #c); return false; } } while (0)

constexpr size_t kOutPrefix = 0xc8;
constexpr size_t kInPrefix = 0xb8;
static_assert(sizeof(void*) == 8 && sizeof(uintptr_t) == sizeof(void*));
static_assert(sizeof(decltype(audio_stream_out::write)) == sizeof(void*));
static_assert(offsetof(audio_stream_out, get_presentation_position) + sizeof(void*) == kOutPrefix);
static_assert(offsetof(audio_stream_in, get_active_microphones) + sizeof(void*) == kInPrefix);
static_assert(offsetof(audio_stream_out, get_audio_description_mix_level) == 0x110);
static_assert(offsetof(audio_stream_in, set_microphone_field_dimension) == 0xc0);

struct Raw {
    explicit Raw(size_t size) : size(size) {
        page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        map = mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (map == MAP_FAILED) { map = nullptr; return; }
        auto* end = static_cast<uint8_t*>(map) + page;
        if (mprotect(end, page, PROT_NONE) != 0) { munmap(map, 2 * page); map = nullptr; return; }
        data = end - size;
        if (reinterpret_cast<uintptr_t>(data) % alignof(void*) != 0) {
            munmap(map, 2 * page); map = data = nullptr; return;
        }
        std::memset(data, 0, size);
    }
    ~Raw() { if (map != nullptr) munmap(map, 2 * page); }
    Raw(const Raw&) = delete;
    Raw& operator=(const Raw&) = delete;
    template<class T> void put(size_t offset, T value) {
        if (offset + sizeof(value) > size) std::abort();
        std::memcpy(static_cast<uint8_t*>(data) + offset, &value, sizeof(value));
    }
    template<class T> T get(size_t offset) const {
        if (offset + sizeof(T) > size) std::abort();
        T value{};
        std::memcpy(&value, static_cast<const uint8_t*>(data) + offset, sizeof(value));
        return value;
    }
    bool fill(size_t offset, size_t length, uint8_t byte) {
        if (offset + length > size) return false;
        std::memset(static_cast<uint8_t*>(data) + offset, byte, length);
        return true;
    }
    void* map = nullptr;
    void* data = nullptr;
    size_t size = 0;
    size_t page = 0;
};

struct State {
    void* raw = nullptr;
    std::atomic<int> closes{0}, calls{0};
    std::atomic<bool> callback_called{false};
    bool input = false;
    stream_callback_t callback = nullptr;
    void* callback_cookie = nullptr;
    const audio_microphone_characteristic_t* expected_mics = nullptr;
    uint8_t buffer[16]{};
};
std::mutex gMutex;
std::vector<State*> gStates;
State* state(const void* raw) {
    std::lock_guard<std::mutex> lock(gMutex);
    for (auto* s : gStates) if (s->raw == raw) return s;
    return nullptr;
}
void registerState(State* s) { std::lock_guard<std::mutex> lock(gMutex); gStates.push_back(s); }
void unregisterState(State* s) {
    std::lock_guard<std::mutex> lock(gMutex);
    for (auto i = gStates.begin(); i != gStates.end(); ++i) if (*i == s) { gStates.erase(i); return; }
}
template<class T, class F> void put(Raw& raw, size_t offset, F fn) {
    raw.put(offset, static_cast<T>(fn));
}

uint32_t rate(const audio_stream* p) { auto* s=state(p); if(!s) return 0; ++s->calls; return 48013; }
size_t bufferSize(const audio_stream* p) { auto* s=state(p); if(!s) return 0; ++s->calls; return 377; }
audio_channel_mask_t channels(const audio_stream* p) { auto* s=state(p); if(!s) return static_cast<audio_channel_mask_t>(0); ++s->calls; return s->input ? AUDIO_CHANNEL_IN_STEREO : AUDIO_CHANNEL_OUT_STEREO; }
audio_format_t format(const audio_stream* p) { auto* s=state(p); if(!s) return AUDIO_FORMAT_DEFAULT; ++s->calls; return AUDIO_FORMAT_PCM_16_BIT; }
int standby(audio_stream* p) { auto* s=state(p); if(!s) return -EINVAL; ++s->calls; return -EINTR; }
int dump(const audio_stream* p,int fd) { auto* s=state(p); if(!s||fd!=51) return -EINVAL; ++s->calls; return -ERANGE; }
int setParams(audio_stream* p,const char* v) { auto* s=state(p); if(!s||v!=reinterpret_cast<const char*>(0x12340)) return -EINVAL; ++s->calls; return -ENOSPC; }
char* getParams(const audio_stream* p,const char* v) { auto* s=state(p); if(!s||v!=reinterpret_cast<const char*>(0x23450)) return nullptr; ++s->calls; return strdup("same-owned-result"); }
int addEffect(const audio_stream* p,effect_handle_t e) { auto* s=state(p); if(!s||e!=reinterpret_cast<effect_handle_t>(0x34560)) return -EINVAL; ++s->calls; return -EACCES; }
int removeEffect(const audio_stream* p,effect_handle_t e) { auto* s=state(p); if(!s||e!=reinterpret_cast<effect_handle_t>(0x45670)) return -EINVAL; ++s->calls; return -EPERM; }
int setRate(audio_stream* p,uint32_t v) { auto* s=state(p); if(!s||v!=44117) return -EINVAL; ++s->calls; return -EAGAIN; }
int setFormat(audio_stream* p,audio_format_t v) { auto* s=state(p); if(!s||v!=AUDIO_FORMAT_PCM_24_BIT_PACKED) return -EINVAL; ++s->calls; return -E2BIG; }
audio_devices_t getDevice(const audio_stream* p) { auto* s=state(p); if(!s) return static_cast<audio_devices_t>(0); ++s->calls; return s->input ? AUDIO_DEVICE_IN_BUILTIN_MIC : AUDIO_DEVICE_OUT_WIRED_HEADPHONE; }
int setDevice(audio_stream* p,audio_devices_t v) { auto* s=state(p); if(!s||v!=(s->input ? AUDIO_DEVICE_IN_BUILTIN_MIC : AUDIO_DEVICE_OUT_LINE)) return -EINVAL; ++s->calls; return -ENOTTY; }
uint32_t latency(const audio_stream_out* p) { auto* s=state(p); if(!s) return 0; ++s->calls; return 173; }
int volume(audio_stream_out* p,float l,float r) { auto* s=state(p); if(!s||l!=0.25f||r!=0.75f) return -EINVAL; ++s->calls; return -EREMOTEIO; }
ssize_t writeOut(audio_stream_out* p,const void* b,size_t n) { auto* s=state(p); if(!s||b!=s->buffer||n!=sizeof(s->buffer)) return -EINVAL; ++s->calls; return n+7; }
ssize_t writeOutCached(audio_stream_out* p,const void* b,size_t n) { auto* s=state(p); if(!s||b!=s->buffer||n!=sizeof(s->buffer)) return -EINVAL; ++s->calls; return n+21; }
int render(const audio_stream_out* p,uint32_t* f) { auto* s=state(p); if(!s||!f) return -EINVAL; ++s->calls; *f=0x76543210; return -EIO; }
int timestamp(const audio_stream_out* p,int64_t* t) { auto* s=state(p); if(!s||!t) return -EINVAL; ++s->calls; *t=INT64_C(0x1234567890); return -ETIMEDOUT; }
int setCallback(audio_stream_out* p,stream_callback_t cb,void* c) { auto* s=state(p); if(!s||!cb||c!=s) return -EINVAL; ++s->calls; s->callback=cb; s->callback_cookie=c; return -EINPROGRESS; }
int pauseOut(audio_stream_out* p) { auto* s=state(p); if(!s) return -EINVAL; ++s->calls; return -EINTR; }
int resumeOut(audio_stream_out* p) { auto* s=state(p); if(!s) return -EINVAL; ++s->calls; return -EALREADY; }
int drainOut(audio_stream_out* p,audio_drain_type_t t) { auto* s=state(p); if(!s||t!=AUDIO_DRAIN_EARLY_NOTIFY) return -EINVAL; ++s->calls; return -EPIPE; }
int flushOut(audio_stream_out* p) { auto* s=state(p); if(!s) return -EINVAL; ++s->calls; return -EBUSY; }
int presentation(const audio_stream_out* p,uint64_t* f,timespec* t) { auto* s=state(p); if(!s||!f||!t) return -EINVAL; ++s->calls; *f=UINT64_C(0x1122334455667788); t->tv_sec=1234567; t->tv_nsec=987654321; return -EOVERFLOW; }
int gain(audio_stream_in* p,float v) { auto* s=state(p); if(!s||v!=0.375f) return -EINVAL; ++s->calls; return -ENODATA; }
ssize_t readIn(audio_stream_in* p,void* b,size_t n) { auto* s=state(p); if(!s||b!=s->buffer||n!=sizeof(s->buffer)) return -EINVAL; ++s->calls; s->buffer[0]=0xa7; return n-1; }
uint32_t lost(audio_stream_in* p) { auto* s=state(p); if(!s) return 0; ++s->calls; return 19; }
int capture(const audio_stream_in* p,int64_t* f,int64_t* t) { auto* s=state(p); if(!s||!f||!t) return -EINVAL; ++s->calls; *f=0x1234567890; *t=0x3141592653; return -ETIME; }
int microphones(const audio_stream_in* p,audio_microphone_characteristic_t* a,size_t* n) {
    auto* s=state(p); if(!s||!a||!n||a!=s->expected_mics) return -EINVAL; ++s->calls;
    if (*n) std::snprintf(a[0].device_id,sizeof(a[0].device_id),"wrapped-mic");
    *n=1;
    return -ENODEV;
}
int startIn(const audio_stream_in* p) { auto* s=state(p); if(!s) return -EINVAL; ++s->calls; return -EINTR; }
int stopIn(const audio_stream_in* p) { auto* s=state(p); if(!s) return -EINVAL; ++s->calls; return -EAGAIN; }
int mmapIn(const audio_stream_in* p,int32_t n,audio_mmap_buffer_info* i) { auto* s=state(p); if(!s||n!=263||!i) return -EINVAL; ++s->calls; i->shared_memory_fd=31; i->buffer_size_frames=263; i->burst_size_frames=17; return -EIO; }
int mmapPosIn(const audio_stream_in* p,audio_mmap_position* v) { auto* s=state(p); if(!s||!v) return -EINVAL; ++s->calls; v->position_frames=37; v->time_nanoseconds=123456789; return -EAGAIN; }
int eventCallback(stream_callback_event_t,void*,void* cookie) { static_cast<State*>(cookie)->callback_called=true; return 0; }
void closeOut(void* cookie,audio_stream_out* raw) { auto* s=static_cast<State*>(cookie); if(s&&s->raw==raw) ++s->closes; }
void closeIn(void* cookie,audio_stream_in* raw) { auto* s=static_cast<State*>(cookie); if(s&&s->raw==raw) ++s->closes; }

void setCommon(Raw& r) {
    put<decltype(audio_stream::get_sample_rate)>(r,offsetof(audio_stream,get_sample_rate),&rate);
    put<decltype(audio_stream::get_buffer_size)>(r,offsetof(audio_stream,get_buffer_size),&bufferSize);
    put<decltype(audio_stream::get_channels)>(r,offsetof(audio_stream,get_channels),&channels);
    put<decltype(audio_stream::get_format)>(r,offsetof(audio_stream,get_format),&format);
    put<decltype(audio_stream::standby)>(r,offsetof(audio_stream,standby),&standby);
    put<decltype(audio_stream::dump)>(r,offsetof(audio_stream,dump),&dump);
    put<decltype(audio_stream::set_parameters)>(r,offsetof(audio_stream,set_parameters),&setParams);
    put<decltype(audio_stream::get_parameters)>(r,offsetof(audio_stream,get_parameters),&getParams);
    put<decltype(audio_stream::add_audio_effect)>(r,offsetof(audio_stream,add_audio_effect),&addEffect);
    put<decltype(audio_stream::remove_audio_effect)>(r,offsetof(audio_stream,remove_audio_effect),&removeEffect);
}
void prepareOut(Raw& r,State& s) {
    s.raw=r.data; registerState(&s); setCommon(r);
    put<decltype(audio_stream::set_sample_rate)>(r,offsetof(audio_stream,set_sample_rate),&setRate);
    put<decltype(audio_stream::set_format)>(r,offsetof(audio_stream,set_format),&setFormat);
    put<decltype(audio_stream::get_device)>(r,offsetof(audio_stream,get_device),&getDevice);
    put<decltype(audio_stream::set_device)>(r,offsetof(audio_stream,set_device),&setDevice);
    put<decltype(audio_stream_out::get_latency)>(r,offsetof(audio_stream_out,get_latency),&latency);
    put<decltype(audio_stream_out::set_volume)>(r,offsetof(audio_stream_out,set_volume),&volume);
    put<decltype(audio_stream_out::write)>(r,offsetof(audio_stream_out,write),&writeOut);
    put<decltype(audio_stream_out::get_render_position)>(r,offsetof(audio_stream_out,get_render_position),&render);
    put<decltype(audio_stream_out::get_next_write_timestamp)>(r,offsetof(audio_stream_out,get_next_write_timestamp),&timestamp);
    put<decltype(audio_stream_out::set_callback)>(r,offsetof(audio_stream_out,set_callback),&setCallback);
    put<decltype(audio_stream_out::pause)>(r,offsetof(audio_stream_out,pause),&pauseOut);
    put<decltype(audio_stream_out::resume)>(r,offsetof(audio_stream_out,resume),&resumeOut);
    put<decltype(audio_stream_out::drain)>(r,offsetof(audio_stream_out,drain),&drainOut);
    put<decltype(audio_stream_out::flush)>(r,offsetof(audio_stream_out,flush),&flushOut);
    put<decltype(audio_stream_out::get_presentation_position)>(r,offsetof(audio_stream_out,get_presentation_position),&presentation);
}
void prepareIn(Raw& r,State& s) {
    s.raw=r.data; s.input=true; registerState(&s); setCommon(r);
    put<decltype(audio_stream::set_sample_rate)>(r,offsetof(audio_stream,set_sample_rate),&setRate);
    put<decltype(audio_stream::set_format)>(r,offsetof(audio_stream,set_format),&setFormat);
    put<decltype(audio_stream::get_device)>(r,offsetof(audio_stream,get_device),&getDevice);
    put<decltype(audio_stream::set_device)>(r,offsetof(audio_stream,set_device),&setDevice);
    put<decltype(audio_stream_in::set_gain)>(r,offsetof(audio_stream_in,set_gain),&gain);
    put<decltype(audio_stream_in::read)>(r,offsetof(audio_stream_in,read),&readIn);
    put<decltype(audio_stream_in::get_input_frames_lost)>(r,offsetof(audio_stream_in,get_input_frames_lost),&lost);
    put<decltype(audio_stream_in::get_capture_position)>(r,offsetof(audio_stream_in,get_capture_position),&capture);
    put<decltype(audio_stream_in::get_active_microphones)>(r,offsetof(audio_stream_in,get_active_microphones),&microphones);
    put<decltype(audio_stream_in::start)>(r,offsetof(audio_stream_in,start),&startIn);
    put<decltype(audio_stream_in::stop)>(r,offsetof(audio_stream_in,stop),&stopIn);
    put<decltype(audio_stream_in::create_mmap_buffer)>(r,offsetof(audio_stream_in,create_mmap_buffer),&mmapIn);
    put<decltype(audio_stream_in::get_mmap_position)>(r,offsetof(audio_stream_in,get_mmap_position),&mmapPosIn);
}

bool testOutGuardPageAndForwarding() {
    Raw raw(kOutPrefix); CHECK(raw.data); State s; prepareOut(raw,s);
    audio_stream_out* out=nullptr; CHECK(wrapOutput(static_cast<audio_stream_out*>(raw.data),closeOut,&s,&out)==0);
    CHECK(out&&reinterpret_cast<void*>(out)!=raw.data);
    CHECK(out->common.get_sample_rate(&out->common)==48013);
    CHECK(out->common.get_buffer_size(&out->common)==377);
    CHECK(out->common.get_channels(&out->common)==AUDIO_CHANNEL_OUT_STEREO);
    CHECK(out->common.get_format(&out->common)==AUDIO_FORMAT_PCM_16_BIT);
    CHECK(out->common.standby(&out->common)==-EINTR);
    CHECK(out->common.dump(&out->common,51)==-ERANGE);
    CHECK(out->common.set_parameters(&out->common,reinterpret_cast<const char*>(0x12340))==-ENOSPC);
    char* v=out->common.get_parameters(&out->common,reinterpret_cast<const char*>(0x23450));
    CHECK(v&&std::strcmp(v,"same-owned-result")==0); std::free(v);
    CHECK(out->common.add_audio_effect(&out->common,reinterpret_cast<effect_handle_t>(0x34560))==-EACCES);
    CHECK(out->common.remove_audio_effect(&out->common,reinterpret_cast<effect_handle_t>(0x45670))==-EPERM);
    CHECK(out->common.set_sample_rate(&out->common,44117)==-EAGAIN);
    CHECK(out->common.set_format(&out->common,AUDIO_FORMAT_PCM_24_BIT_PACKED)==-E2BIG);
    CHECK(out->common.get_device(&out->common)==AUDIO_DEVICE_OUT_WIRED_HEADPHONE);
    CHECK(out->common.set_device(&out->common,AUDIO_DEVICE_OUT_LINE)==-ENOTTY);
    CHECK(out->get_latency(out)==173);
    CHECK(out->set_volume(out,0.25f,0.75f)==-EREMOTEIO);
    CHECK(out->write(out,s.buffer,sizeof(s.buffer))==static_cast<ssize_t>(sizeof(s.buffer)+7));
    uint32_t frames=0; CHECK(out->get_render_position(out,&frames)==-EIO&&frames==0x76543210);
    int64_t next=0; CHECK(out->get_next_write_timestamp(out,&next)==-ETIMEDOUT&&next==INT64_C(0x1234567890));
    CHECK(out->set_callback(out,&eventCallback,&s)==-EINPROGRESS);
    CHECK(s.callback==&eventCallback&&s.callback_cookie==&s);
    CHECK(out->pause(out)==-EINTR&&out->resume(out)==-EALREADY);
    CHECK(out->drain(out,AUDIO_DRAIN_EARLY_NOTIFY)==-EPIPE&&out->flush(out)==-EBUSY);
    uint64_t pos=0; timespec time{}; CHECK(out->get_presentation_position(out,&pos,&time)==-EOVERFLOW);
    CHECK(pos==UINT64_C(0x1122334455667788)&&time.tv_sec==1234567&&time.tv_nsec==987654321);
    CHECK(out->start==nullptr&&out->stop==nullptr&&out->create_mmap_buffer==nullptr);
    CHECK(out->get_mmap_position==nullptr&&out->update_source_metadata==nullptr&&out->set_event_callback==nullptr);
    CHECK(out->update_source_metadata_v7==nullptr&&out->get_dual_mono_mode==nullptr&&out->set_dual_mono_mode==nullptr);
    CHECK(out->get_audio_description_mix_level==nullptr&&out->set_audio_description_mix_level==nullptr);
    CHECK(out->get_playback_rate_parameters==nullptr&&out->set_playback_rate_parameters==nullptr);
    CHECK(out->set_latency_mode==nullptr&&out->get_recommended_latency_modes==nullptr&&out->set_latency_mode_callback==nullptr);
    closeOutput(out); CHECK(s.closes==1); unregisterState(&s); return true;
}

bool testInGuardPageAndForwarding() {
    Raw raw(kInPrefix); CHECK(raw.data); State s; prepareIn(raw,s);
    audio_stream_in* in=nullptr; CHECK(wrapInput(static_cast<audio_stream_in*>(raw.data),closeIn,&s,&in)==0);
    CHECK(in&&reinterpret_cast<void*>(in)!=raw.data);
    CHECK(in->common.get_sample_rate(&in->common)==48013&&in->common.get_buffer_size(&in->common)==377);
    CHECK(in->common.get_channels(&in->common)==AUDIO_CHANNEL_IN_STEREO);
    CHECK(in->common.get_format(&in->common)==AUDIO_FORMAT_PCM_16_BIT);
    CHECK(in->common.standby(&in->common)==-EINTR);
    CHECK(in->common.dump(&in->common,51)==-ERANGE);
    CHECK(in->common.set_parameters(&in->common,reinterpret_cast<const char*>(0x12340))==-ENOSPC);
    char* parameters=in->common.get_parameters(&in->common,reinterpret_cast<const char*>(0x23450));
    CHECK(parameters&&std::strcmp(parameters,"same-owned-result")==0); std::free(parameters);
    CHECK(in->common.add_audio_effect(&in->common,reinterpret_cast<effect_handle_t>(0x34560))==-EACCES);
    CHECK(in->common.remove_audio_effect(&in->common,reinterpret_cast<effect_handle_t>(0x45670))==-EPERM);
    CHECK(in->common.set_sample_rate(&in->common,44117)==-EAGAIN);
    CHECK(in->common.set_format(&in->common,AUDIO_FORMAT_PCM_24_BIT_PACKED)==-E2BIG);
    CHECK(in->common.get_device(&in->common)==AUDIO_DEVICE_IN_BUILTIN_MIC);
    CHECK(in->common.set_device(&in->common,AUDIO_DEVICE_IN_BUILTIN_MIC)==-ENOTTY);
    CHECK(in->set_gain(in,0.375f)==-ENODATA);
    CHECK(in->read(in,s.buffer,sizeof(s.buffer))==static_cast<ssize_t>(sizeof(s.buffer)-1)&&s.buffer[0]==0xa7);
    CHECK(in->get_input_frames_lost(in)==19);
    CHECK(in->start(in)==-EINTR&&in->stop(in)==-EAGAIN);
    audio_mmap_buffer_info mmapInfo{};
    CHECK(in->create_mmap_buffer(in,263,&mmapInfo)==-EIO);
    CHECK(mmapInfo.shared_memory_fd==31&&mmapInfo.buffer_size_frames==263&&mmapInfo.burst_size_frames==17);
    audio_mmap_position mmapPosition{};
    CHECK(in->get_mmap_position(in,&mmapPosition)==-EAGAIN);
    CHECK(mmapPosition.position_frames==37&&mmapPosition.time_nanoseconds==123456789);
    int64_t frames=0,time=0; CHECK(in->get_capture_position(in,&frames,&time)==-ETIME);
    CHECK(frames==INT64_C(0x1234567890)&&time==INT64_C(0x3141592653));
    audio_microphone_characteristic_t mics[2]{}; size_t n=2;
    s.expected_mics=mics;
    CHECK(in->get_active_microphones(in,mics,&n)==-ENODEV&&n==1);
    CHECK(std::strcmp(mics[0].device_id,"wrapped-mic")==0);
    CHECK(in->start!=nullptr&&in->stop!=nullptr&&in->create_mmap_buffer!=nullptr);
    CHECK(in->get_mmap_position!=nullptr&&in->get_active_microphones!=nullptr&&in->set_microphone_direction==nullptr);
    CHECK(in->set_microphone_field_dimension==nullptr&&in->update_sink_metadata==nullptr);
    CHECK(in->update_sink_metadata_v7==nullptr);
    closeInput(in); CHECK(s.closes==1); unregisterState(&s); return true;
}

bool testPoisonedPrivateTailAndRepeatedCacheLifecycle() {
    Raw rawOut(0x238),rawIn(0x470); CHECK(rawOut.data&&rawIn.data);
    State os,is; prepareOut(rawOut,os); prepareIn(rawIn,is);
    CHECK(rawOut.fill(kOutPrefix,sizeof(audio_stream_out)-kOutPrefix,0xa5));
    CHECK(rawIn.fill(kInPrefix,sizeof(audio_stream_in)-kInPrefix,0x69));
    std::array<uint8_t,sizeof(audio_stream_out)-kOutPrefix> outputTail{};
    std::array<uint8_t,sizeof(audio_stream_in)-kInPrefix> inputTail{};
    std::memcpy(outputTail.data(),static_cast<uint8_t*>(rawOut.data)+kOutPrefix,outputTail.size());
    std::memcpy(inputTail.data(),static_cast<uint8_t*>(rawIn.data)+kInPrefix,inputTail.size());
    audio_stream_out* out=nullptr; audio_stream_in* in=nullptr;
    CHECK(wrapOutput(static_cast<audio_stream_out*>(rawOut.data),closeOut,&os,&out)==0);
    CHECK(wrapInput(static_cast<audio_stream_in*>(rawIn.data),closeIn,&is,&in)==0);
    CHECK(out->start==nullptr&&out->stop==nullptr&&out->create_mmap_buffer==nullptr&&out->get_mmap_position==nullptr);
    CHECK(out->update_source_metadata==nullptr&&out->set_event_callback==nullptr&&out->update_source_metadata_v7==nullptr);
    CHECK(out->get_dual_mono_mode==nullptr&&out->set_dual_mono_mode==nullptr);
    CHECK(out->get_audio_description_mix_level==nullptr&&out->set_audio_description_mix_level==nullptr);
    CHECK(out->get_playback_rate_parameters==nullptr&&out->set_playback_rate_parameters==nullptr);
    CHECK(out->set_latency_mode==nullptr&&out->get_recommended_latency_modes==nullptr&&out->set_latency_mode_callback==nullptr);
    CHECK(in->start!=nullptr&&in->stop!=nullptr&&in->create_mmap_buffer!=nullptr&&in->get_mmap_position!=nullptr);
    CHECK(in->set_microphone_field_dimension==nullptr);
    CHECK(in->set_microphone_direction==nullptr&&in->update_sink_metadata==nullptr&&in->update_sink_metadata_v7==nullptr);
    CHECK(std::memcmp(static_cast<uint8_t*>(rawOut.data)+kOutPrefix,outputTail.data(),outputTail.size())==0);
    CHECK(std::memcmp(static_cast<uint8_t*>(rawIn.data)+kInPrefix,inputTail.data(),inputTail.size())==0);
    closeOutput(out);
    // Vendor close may retain/reuse its cached stream allocation. The adapter must
    // release only its own object, allowing a second wrapper over the same raw pointer.
    CHECK(os.closes==1);
    rawOut.put(offsetof(audio_stream_out, write), &writeOutCached);
    audio_stream_out* out2=nullptr;
    CHECK(wrapOutput(static_cast<audio_stream_out*>(rawOut.data),closeOut,&os,&out2)==0);
    CHECK(out2!=nullptr&&out2->write!=nullptr&&out2->get_audio_description_mix_level==nullptr);
    CHECK(out2->write(out2,os.buffer,sizeof(os.buffer))==static_cast<ssize_t>(sizeof(os.buffer)+21));
    closeOutput(out2); CHECK(os.closes==2);
    closeInput(in); CHECK(is.closes==1);
    unregisterState(&os); unregisterState(&is); return true;
}

bool testRequiredCallbacksAndInvalidArguments() {
    Raw rawOut(kOutPrefix),rawIn(kInPrefix); CHECK(rawOut.data&&rawIn.data);
    State os,is; prepareOut(rawOut,os); prepareIn(rawIn,is);
    audio_stream_out* out=nullptr;
    audio_stream_in* in=nullptr;
    const std::array<size_t, 13> requiredOut = {
        offsetof(audio_stream, get_sample_rate), offsetof(audio_stream, get_buffer_size),
        offsetof(audio_stream, get_channels), offsetof(audio_stream, get_format),
        offsetof(audio_stream, standby), offsetof(audio_stream, dump),
        offsetof(audio_stream, set_parameters), offsetof(audio_stream, get_parameters),
        offsetof(audio_stream, add_audio_effect), offsetof(audio_stream, remove_audio_effect),
        offsetof(audio_stream_out, get_latency), offsetof(audio_stream_out, write),
        offsetof(audio_stream_out, get_render_position),
    };
    const std::array<size_t, 13> requiredIn = {
        offsetof(audio_stream, get_sample_rate), offsetof(audio_stream, get_buffer_size),
        offsetof(audio_stream, get_channels), offsetof(audio_stream, get_format),
        offsetof(audio_stream, standby), offsetof(audio_stream, dump),
        offsetof(audio_stream, set_parameters), offsetof(audio_stream, get_parameters),
        offsetof(audio_stream, add_audio_effect), offsetof(audio_stream, remove_audio_effect),
        offsetof(audio_stream_in, set_gain), offsetof(audio_stream_in, read),
        offsetof(audio_stream_in, get_input_frames_lost),
    };
    std::array<uint8_t, kOutPrefix> savedOut{};
    std::array<uint8_t, kInPrefix> savedIn{};
    std::memcpy(savedOut.data(), rawOut.data, savedOut.size());
    std::memcpy(savedIn.data(), rawIn.data, savedIn.size());
    for (size_t offset : requiredOut) {
        rawOut.put(offset, static_cast<uintptr_t>(0));
        audio_stream_out* wrapped=reinterpret_cast<audio_stream_out*>(1);
        CHECK(wrapOutput(static_cast<audio_stream_out*>(rawOut.data),closeOut,&os,&wrapped)==-EINVAL);
        CHECK(wrapped==nullptr&&os.closes==0);
        std::memcpy(rawOut.data, savedOut.data(), savedOut.size());
    }
    for (size_t offset : requiredIn) {
        rawIn.put(offset, static_cast<uintptr_t>(0));
        audio_stream_in* wrapped=reinterpret_cast<audio_stream_in*>(1);
        CHECK(wrapInput(static_cast<audio_stream_in*>(rawIn.data),closeIn,&is,&wrapped)==-EINVAL);
        CHECK(wrapped==nullptr&&is.closes==0);
        std::memcpy(rawIn.data, savedIn.data(), savedIn.size());
    }
    // Every optional legacy-prefix callback stays absent when its source is absent.
    const std::array<size_t, 8> optionalOut = {
        offsetof(audio_stream_out, set_volume), offsetof(audio_stream_out, get_next_write_timestamp),
        offsetof(audio_stream_out, set_callback), offsetof(audio_stream_out, pause),
        offsetof(audio_stream_out, resume), offsetof(audio_stream_out, drain),
        offsetof(audio_stream_out, flush), offsetof(audio_stream_out, get_presentation_position),
    };
    const std::array<size_t, 6> optionalIn = {
        offsetof(audio_stream_in, get_capture_position), offsetof(audio_stream_in, start),
        offsetof(audio_stream_in, stop), offsetof(audio_stream_in, create_mmap_buffer),
        offsetof(audio_stream_in, get_mmap_position), offsetof(audio_stream_in, get_active_microphones),
    };
    for (size_t offset : optionalOut) {
        std::memcpy(rawOut.data, savedOut.data(), savedOut.size());
        rawOut.put(offset, static_cast<uintptr_t>(0));
        audio_stream_out* out=nullptr;
        CHECK(wrapOutput(static_cast<audio_stream_out*>(rawOut.data),closeOut,&os,&out)==0);
        CHECK(out!=nullptr);
        CHECK(*reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(out)+offset)==0);
        closeOutput(out);
    }
    for (size_t offset : optionalIn) {
        std::memcpy(rawIn.data, savedIn.data(), savedIn.size());
        rawIn.put(offset, static_cast<uintptr_t>(0));
        audio_stream_in* in=nullptr;
        CHECK(wrapInput(static_cast<audio_stream_in*>(rawIn.data),closeIn,&is,&in)==0);
        CHECK(in!=nullptr&&*reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(in)+offset)==0);
        closeInput(in);
    }
    CHECK(os.closes==optionalOut.size()&&is.closes==optionalIn.size());
    unregisterState(&os); unregisterState(&is);
    Raw rawOut2(kOutPrefix),rawIn2(kInPrefix); CHECK(rawOut2.data&&rawIn2.data);
    State os2,is2; prepareOut(rawOut2,os2); prepareIn(rawIn2,is2);
    out=reinterpret_cast<audio_stream_out*>(1);
    in=reinterpret_cast<audio_stream_in*>(1);
    CHECK(wrapOutput(nullptr,closeOut,&os,&out)==-EINVAL&&out==nullptr);
    CHECK(wrapInput(static_cast<audio_stream_in*>(rawIn2.data),nullptr,&is2,&in)==-EINVAL&&in==nullptr);
    CHECK(wrapOutput(static_cast<audio_stream_out*>(rawOut.data),closeOut,&os,nullptr)==-EINVAL);
    closeOutput(nullptr); closeInput(nullptr);
    unregisterState(&os2); unregisterState(&is2); return true;
}

bool testConcurrentIndependentStreamsAndCallbackCookie() {
    Raw rawA(kOutPrefix),rawB(kOutPrefix); CHECK(rawA.data&&rawB.data);
    State a,b; prepareOut(rawA,a); prepareOut(rawB,b);
    audio_stream_out* x=nullptr; audio_stream_out* y=nullptr;
    CHECK(wrapOutput(static_cast<audio_stream_out*>(rawA.data),closeOut,&a,&x)==0);
    CHECK(wrapOutput(static_cast<audio_stream_out*>(rawB.data),closeOut,&b,&y)==0);
    CHECK(x->set_callback(x,&eventCallback,&a)==-EINPROGRESS);
    a.callback(STREAM_CBK_EVENT_WRITE_READY,reinterpret_cast<void*>(0xabcd0),a.callback_cookie);
    CHECK(a.callback_called);
    struct WorkerArgs { audio_stream_out* stream; uint8_t* buffer; };
    std::array<pthread_t, 6> threads{};
    std::array<WorkerArgs, 6> args{};
    for (size_t n = 0; n < threads.size(); ++n) {
        args[n] = {n % 2 ? x : y, n % 2 ? a.buffer : b.buffer};
        CHECK(pthread_create(&threads[n], nullptr, [](void* opaque) -> void* {
            auto* worker = static_cast<WorkerArgs*>(opaque);
            for (int i = 0; i < 300; ++i) {
                if (worker->stream->write(worker->stream, worker->buffer, 16) < 0) std::abort();
            }
            return nullptr;
        }, &args[n]) == 0);
    }
    for (pthread_t thread : threads) CHECK(pthread_join(thread, nullptr) == 0);
    closeOutput(x); closeOutput(y); CHECK(a.closes==1&&b.closes==1);
    unregisterState(&a); unregisterState(&b); return true;
}
} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::puts("testOutGuardPageAndForwarding");
    if (!testOutGuardPageAndForwarding()) return 1;
    std::puts("testInGuardPageAndForwarding");
    if (!testInGuardPageAndForwarding()) return 1;
    std::puts("testPoisonedPrivateTailAndRepeatedCacheLifecycle");
    if (!testPoisonedPrivateTailAndRepeatedCacheLifecycle()) return 1;
    std::puts("testRequiredCallbacksAndInvalidArguments");
    if (!testRequiredCallbacksAndInvalidArguments()) return 1;
    std::puts("testConcurrentIndependentStreamsAndCallbackCookie");
    if (!testConcurrentIndependentStreamsAndCallbackCookie()) return 1;
    std::puts("audio stream adapter host tests passed");
    return 0;
}
