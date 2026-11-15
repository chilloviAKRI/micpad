#include "audioengine.h"
#include "pcm.h"
#include <QMetaObject>
#include <spa/param/audio/format-utils.h>
#include <algorithm>
#include <cstring>
#include <cmath>

AudioEngine::AudioEngine(QObject *parent) : QObject(parent) { pw_init(nullptr, nullptr); }
AudioEngine::~AudioEngine() { stop(); }

void AudioEngine::stateChanged(void *data, pw_stream_state, pw_stream_state state, const char *message) {
    if (state != PW_STREAM_STATE_ERROR) return;
    auto self = static_cast<AudioEngine *>(data);
    const QString detail = QString::fromUtf8(message ? message : "unknown");
    QMetaObject::invokeMethod(self, [self, detail] { emit self->error(tr("PipeWire stream: %1").arg(detail)); }, Qt::QueuedConnection);
}

void AudioEngine::coreError(void *data, uint32_t, int, int, const char *message) {
    auto self = static_cast<AudioEngine *>(data);
    const QString detail = QString::fromUtf8(message ? message : "unknown");
    QMetaObject::invokeMethod(self, [self, detail] { emit self->error(tr("PipeWire: %1").arg(detail)); }, Qt::QueuedConnection);
}

bool AudioEngine::start(std::shared_ptr<const PcmFile> samples) {
    stop();
    if (!samples || !samples->samples() || samples->count() <= 0) return false;
    m_samples = std::move(samples); m_cursor = 0; m_finished = false;
    setAllowed(false,false);
    m_loop = pw_thread_loop_new("micpad-audio", nullptr);
    if (!m_loop) { stop(); return false; }
    m_context = pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0);
    if (!m_context) { stop(); return false; }
    m_core = pw_context_connect(m_context, nullptr, 0);
    if (!m_core) { stop(); return false; }
    static const pw_core_events coreEvents = [] { pw_core_events e{}; e.version = PW_VERSION_CORE_EVENTS; e.error = coreError; return e; }();
    pw_core_add_listener(m_core, &m_coreListener, &coreEvents, this);
    m_stream = pw_stream_new(m_core, "Micpad clips", pw_properties_new(
        PW_KEY_NODE_NAME, "micpad.clips", PW_KEY_NODE_DESCRIPTION, "Micpad clips",
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_CLASS, "Stream/Output/Audio", PW_KEY_MEDIA_ROLE, "Music",
        PW_KEY_NODE_AUTOCONNECT, "false", "node.dont-reconnect", "true",
        PW_KEY_STREAM_DONT_REMIX, "true", "audio.channels", "2", "audio.position", "[ FL FR ]",
        "node.restore-props", "false", "stream.restore-props", "false",
        PW_KEY_NODE_LATENCY, "256/48000", nullptr));
    if (!m_stream) { stop(); return false; }
    static const pw_stream_events streamEvents = [] { pw_stream_events e{}; e.version = PW_VERSION_STREAM_EVENTS;
        e.process = process; e.state_changed = stateChanged; return e; }();
    pw_stream_add_listener(m_stream, &m_streamListener, &streamEvents, this);
    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_audio_info_raw format{};
    format.format = SPA_AUDIO_FORMAT_F32; format.rate = 48000; format.channels = 2;
    // The two output channels are independent effect feeds. The graph sends
    // FL only to capture apps, and FR to both playback channels of the output.
    format.position[0] = SPA_AUDIO_CHANNEL_FL;
    format.position[1] = SPA_AUDIO_CHANNEL_FR;
    const spa_pod *params[] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &format)};
    const int result = pw_stream_connect(m_stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS), params, 1);
    if (result < 0 || pw_thread_loop_start(m_loop) < 0) { stop(); return false; }
    return true;
}

void AudioEngine::stop() {
    if (m_loop) {
        pw_thread_loop_stop(m_loop);
        for (auto proxy : std::as_const(m_links)) pw_proxy_destroy(proxy);
        m_links.clear();
        if (m_stream) { spa_hook_remove(&m_streamListener); pw_stream_destroy(m_stream); m_stream = nullptr; }
        if (m_core) { spa_hook_remove(&m_coreListener); pw_core_disconnect(m_core); m_core = nullptr; }
        if (m_context) { pw_context_destroy(m_context); m_context = nullptr; }
        pw_thread_loop_destroy(m_loop); m_loop = nullptr;
    }
    m_samples.reset(); m_cursor = 0; m_finished = false;
}

int AudioEngine::nodeId() const {
    if (!m_stream || !m_loop) return -1;
    pw_thread_loop_lock(m_loop);
    const auto id = pw_stream_get_node_id(m_stream);
    pw_thread_loop_unlock(m_loop);
    return id == PW_ID_ANY ? -1 : static_cast<int>(id);
}

void AudioEngine::setRoutes(const QList<Route> &routes) {
    if (!m_core || !m_loop) return;
    pw_thread_loop_lock(m_loop);
    QSet<QString> desired;
    for (const auto &r : routes) desired.insert(r.key);
    for (auto it = m_links.begin(); it != m_links.end();) {
        if (!desired.contains(it.key())) { pw_proxy_destroy(it.value()); it = m_links.erase(it); }
        else ++it;
    }
    for (const auto &r : routes) {
        if (m_links.contains(r.key)) continue;
        auto props = pw_properties_new(nullptr, nullptr);
        pw_properties_setf(props, PW_KEY_LINK_OUTPUT_NODE, "%d", r.outputNode);
        pw_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%d", r.outputPort);
        pw_properties_setf(props, PW_KEY_LINK_INPUT_NODE, "%d", r.inputNode);
        pw_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%d", r.inputPort);
        // Ownership stays with this client, including when it crashes or is killed.
        pw_properties_set(props, PW_KEY_OBJECT_LINGER, "false");
        auto proxy = static_cast<pw_proxy *>(pw_core_create_object(m_core, "link-factory",
            PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict, 0));
        pw_properties_free(props);
        if (proxy) m_links.insert(r.key, proxy);
    }
    pw_thread_loop_unlock(m_loop);
}

double AudioEngine::progress() const {
    return !m_samples ? 0 : double(m_cursor.load()) / double(m_samples->count());
}

void AudioEngine::process(void *data) {
    auto self = static_cast<AudioEngine *>(data);
    auto buffer = pw_stream_dequeue_buffer(self->m_stream);
    if (!buffer) return;
    auto &d = buffer->buffer->datas[0];
    auto dst = static_cast<float *>(d.data);
    if (!dst) { pw_stream_queue_buffer(self->m_stream, buffer); return; }
    constexpr uint32_t stride = 2 * sizeof(float);
    uint32_t frames = d.maxsize / stride;
    if (buffer->requested) frames = std::min<uint64_t>(frames, buffer->requested);
    const auto src = self->m_samples->samples();
    const auto count = self->m_samples->count();
    std::size_t cursor = self->m_cursor.load();
    const bool done = Pcm::render(src, count, cursor, dst, frames,
                                 self->m_microphoneGain.load(), self->m_headphoneGain.load(),
                                 self->m_repeat.load(), self->m_microphoneAllowed.load(), self->m_headphonesAllowed.load());
    self->m_finished.store(done);
    self->m_cursor.store(cursor);
    d.chunk->offset = 0; d.chunk->stride = stride; d.chunk->size = frames * stride;
    pw_stream_queue_buffer(self->m_stream, buffer);
}
