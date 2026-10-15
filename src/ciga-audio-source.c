#include <obs-module.h>
#include <obs-frontend-api.h>

#include <util/bmem.h>
#include <util/platform.h>
#include <util/threading.h>
#include <util/util_uint64.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ciga-audio-source.h"

#define CIGA_AUDIO_NORMAL       0
#define CIGA_AUDIO_HALLOWEEN    1
#define CIGA_AUDIO_NOVEMBER     2
#define CIGA_AUDIO_CHRISTMAS    3

#define CIGA_AUDIO_CHUNK_FRAMES 1024

struct ciga_audio_clip {
    float *data[2];

    uint32_t frames;
    uint32_t sample_rate;

    enum speaker_layout speakers;

    bool loaded;
};

struct ciga_audio_source {
    obs_source_t *source;

    struct ciga_audio_clip clips[4];

    pthread_t thread;
    bool thread_valid;

    os_event_t *wake_event;
    pthread_mutex_t mutex;

    bool shutting_down;
    bool playing;

    int version;

    uint32_t position;

    uint64_t start_timestamp;
    uint64_t generation;
};

static struct ciga_audio_source *g_audio_source = NULL;

static pthread_mutex_t g_audio_source_mutex =
    PTHREAD_MUTEX_INITIALIZER;

static bool g_auto_attach_connected = false;

static bool g_scene_collection_changing = false;


/* ============================================================
 * LITTLE-ENDIAN HELPERS
 * ============================================================
 */

static uint16_t ciga_read_u16(
    const uint8_t *data)
{
    return
        (uint16_t)data[0]
        |
        ((uint16_t)data[1] << 8);
}

static uint32_t ciga_read_u32(
    const uint8_t *data)
{
    return
        (uint32_t)data[0]
        |
        ((uint32_t)data[1] << 8)
        |
        ((uint32_t)data[2] << 16)
        |
        ((uint32_t)data[3] << 24);
}


/* ============================================================
 * SAMPLE CONVERSION
 * ============================================================
 */

static float ciga_pcm8_to_float(
    const uint8_t *data)
{
    return
        (
            ((float)data[0] - 128.0f)
            /
            128.0f
        );
}

static float ciga_pcm16_to_float(
    const uint8_t *data)
{
    const int16_t value =
        (int16_t)ciga_read_u16(
            data
        );

    return
        (float)value /
        32768.0f;
}

static float ciga_pcm24_to_float(
    const uint8_t *data)
{
    int32_t value =
        (int32_t)data[0]
        |
        ((int32_t)data[1] << 8)
        |
        ((int32_t)data[2] << 16);

    if (
        value &
        0x00800000
    ) {
        value |=
            (int32_t)0xFF000000;
    }

    return
        (float)value /
        8388608.0f;
}

static float ciga_pcm32_to_float(
    const uint8_t *data)
{
    const int32_t value =
        (int32_t)ciga_read_u32(
            data
        );

    return
        (float)value /
        2147483648.0f;
}

static float ciga_float32_to_float(
    const uint8_t *data)
{
    float value = 0.0f;

    memcpy(
        &value,
        data,
        sizeof(value)
    );

    if (value > 1.0f) {
        value = 1.0f;
    }

    if (value < -1.0f) {
        value = -1.0f;
    }

    return value;
}


/* ============================================================
 * FREE CLIP
 * ============================================================
 */

static void ciga_audio_clip_free(
    struct ciga_audio_clip *clip)
{
    if (!clip) {
        return;
    }

    if (clip->data[0]) {
        bfree(
            clip->data[0]
        );
    }

    if (clip->data[1]) {
        bfree(
            clip->data[1]
        );
    }

    memset(
        clip,
        0,
        sizeof(*clip)
    );
}


/* ============================================================
 * LOAD WAV
 * ============================================================
 */

static bool ciga_audio_clip_load(
    struct ciga_audio_clip *clip,
    const char *relative_file)
{
    if (!clip ||
        !relative_file) {
        return false;
    }

    char *path =
        obs_module_file(
            relative_file
        );

    if (!path) {

        blog(
            LOG_ERROR,
            "[CIGA-AUDIO] Could not resolve: %s",
            relative_file
        );

        return false;
    }

    FILE *file =
        fopen(
            path,
            "rb"
        );

    if (!file) {

        blog(
            LOG_ERROR,
            "[CIGA-AUDIO] Could not open: %s",
            path
        );

        bfree(
            path
        );

        return false;
    }

    if (
        fseek(
            file,
            0,
            SEEK_END
        ) != 0
    ) {

        fclose(
            file
        );

        bfree(
            path
        );

        return false;
    }

    const long file_length =
        ftell(
            file
        );

    if (
        file_length < 44
    ) {

        fclose(
            file
        );

        bfree(
            path
        );

        return false;
    }

    if (
        fseek(
            file,
            0,
            SEEK_SET
        ) != 0
    ) {

        fclose(
            file
        );

        bfree(
            path
        );

        return false;
    }

    const size_t file_size =
        (size_t)file_length;

    uint8_t *bytes =
        bmalloc(
            file_size
        );

    if (!bytes) {

        fclose(
            file
        );

        bfree(
            path
        );

        return false;
    }

    const size_t read_size =
        fread(
            bytes,
            1,
            file_size,
            file
        );

    fclose(
        file
    );

    if (
        read_size != file_size
    ) {

        bfree(
            bytes
        );

        bfree(
            path
        );

        return false;
    }

    if (
        memcmp(
            bytes,
            "RIFF",
            4
        ) != 0 ||
        memcmp(
            bytes + 8,
            "WAVE",
            4
        ) != 0
    ) {

        blog(
            LOG_ERROR,
            "[CIGA-AUDIO] Invalid WAV file: %s",
            relative_file
        );

        bfree(
            bytes
        );

        bfree(
            path
        );

        return false;
    }

    bool fmt_found = false;
    bool data_found = false;

    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;

    size_t data_offset = 0;
    size_t data_size = 0;

    size_t offset = 12;

    while (
        offset + 8 <= file_size
    ) {

        const uint8_t *chunk =
            bytes + offset;

        const uint32_t chunk_size =
            ciga_read_u32(
                chunk + 4
            );

        offset += 8;

        if (
            offset +
            (size_t)chunk_size >
            file_size
        ) {
            break;
        }

        if (
            memcmp(
                chunk,
                "fmt ",
                4
            ) == 0 &&
            chunk_size >= 16
        ) {

            const uint8_t *fmt =
                bytes + offset;

            audio_format =
                ciga_read_u16(
                    fmt + 0
                );

            channels =
                ciga_read_u16(
                    fmt + 2
                );

            sample_rate =
                ciga_read_u32(
                    fmt + 4
                );

            bits_per_sample =
                ciga_read_u16(
                    fmt + 14
                );

            if (
                audio_format == 0xFFFE &&
                chunk_size >= 40
            ) {
                audio_format =
                    ciga_read_u16(
                        fmt + 24
                    );
            }

            fmt_found =
                true;
        }
        else if (
            memcmp(
                chunk,
                "data",
                4
            ) == 0
        ) {

            data_offset =
                offset;

            data_size =
                chunk_size;

            data_found =
                true;
        }

        offset +=
            (size_t)chunk_size;

        if (
            chunk_size &
            1
        ) {
            offset++;
        }
    }

    if (
        !fmt_found ||
        !data_found ||
        !channels ||
        !sample_rate ||
        !bits_per_sample
    ) {

        blog(
            LOG_ERROR,
            "[CIGA-AUDIO] WAV is missing required chunks: %s",
            relative_file
        );

        bfree(
            bytes
        );

        bfree(
            path
        );

        return false;
    }

    if (
        channels < 1 ||
        channels > 2
    ) {

        blog(
            LOG_ERROR,
            "[CIGA-AUDIO] Only mono/stereo WAV files are supported: %s",
            relative_file
        );

        bfree(
            bytes
        );

        bfree(
            path
        );

        return false;
    }

    if (
        audio_format != 1 &&
        audio_format != 3
    ) {

        blog(
            LOG_ERROR,
            "[CIGA-AUDIO] WAV must be PCM or IEEE float: %s",
            relative_file
        );

        bfree(
            bytes
        );

        bfree(
            path
        );

        return false;
    }

    if (
        audio_format == 3 &&
        bits_per_sample != 32
    ) {

        blog(
            LOG_ERROR,
            "[CIGA-AUDIO] IEEE float WAV must be 32-bit: %s",
            relative_file
        );

        bfree(
            bytes
        );

        bfree(
            path
        );

        return false;
    }

    if (
        audio_format == 1 &&
        bits_per_sample != 8 &&
        bits_per_sample != 16 &&
        bits_per_sample != 24 &&
        bits_per_sample != 32
    ) {

        blog(
            LOG_ERROR,
            "[CIGA-AUDIO] Unsupported PCM bit depth: %s",
            relative_file
        );

        bfree(
            bytes
        );

        bfree(
            path
        );

        return false;
    }

    const size_t bytes_per_sample =
        bits_per_sample / 8;

    const size_t frame_size =
        bytes_per_sample *
        channels;

    if (
        frame_size == 0 ||
        data_size < frame_size
    ) {

        bfree(
            bytes
        );

        bfree(
            path
        );

        return false;
    }

    const size_t frame_count =
        data_size /
        frame_size;

    if (
        frame_count == 0 ||
        frame_count > UINT32_MAX
    ) {

        bfree(
            bytes
        );

        bfree(
            path
        );

        return false;
    }

    clip->data[0] =
        bzalloc(
            frame_count *
            sizeof(float)
        );

    if (!clip->data[0]) {

        bfree(
            bytes
        );

        bfree(
            path
        );

        return false;
    }

    if (channels == 2) {

        clip->data[1] =
            bzalloc(
                frame_count *
                sizeof(float)
            );

        if (!clip->data[1]) {

            ciga_audio_clip_free(
                clip
            );

            bfree(
                bytes
            );

            bfree(
                path
            );

            return false;
        }
    }

    const uint8_t *samples =
        bytes +
        data_offset;

    for (
        size_t frame = 0;
        frame < frame_count;
        frame++
    ) {

        for (
            uint16_t channel = 0;
            channel < channels;
            channel++
        ) {

            const size_t sample_offset =
                (
                    frame *
                    channels +
                    channel
                )
                *
                bytes_per_sample;

            const uint8_t *sample =
                samples +
                sample_offset;

            float value = 0.0f;

            if (
                audio_format == 3
            ) {

                value =
                    ciga_float32_to_float(
                        sample
                    );
            }
            else {

                switch (
                    bits_per_sample
                ) {

                case 8:

                    value =
                        ciga_pcm8_to_float(
                            sample
                        );

                    break;

                case 16:

                    value =
                        ciga_pcm16_to_float(
                            sample
                        );

                    break;

                case 24:

                    value =
                        ciga_pcm24_to_float(
                            sample
                        );

                    break;

                case 32:

                    value =
                        ciga_pcm32_to_float(
                            sample
                        );

                    break;

                default:

                    value =
                        0.0f;

                    break;
                }
            }

            clip->data[channel][frame] =
                value;
        }
    }

    clip->frames =
        (uint32_t)frame_count;

    clip->sample_rate =
        sample_rate;

    clip->speakers =
        channels == 1
        ? SPEAKERS_MONO
        : SPEAKERS_STEREO;

    clip->loaded =
        true;

    blog(
        LOG_INFO,
        "[CIGA-AUDIO] Loaded %s (%u Hz, %u channels, %u frames)",
        relative_file,
        sample_rate,
        channels,
        (uint32_t)frame_count
    );

    bfree(
        bytes
    );

    bfree(
        path
    );

    return true;
}


/* ============================================================
 * GET CLIP
 * ============================================================
 */

static struct ciga_audio_clip *
ciga_audio_get_clip(
    struct ciga_audio_source *audio,
    int version)
{
    if (!audio) {
        return NULL;
    }

    if (
        version < CIGA_AUDIO_NORMAL ||
        version > CIGA_AUDIO_CHRISTMAS
    ) {
        version =
            CIGA_AUDIO_NORMAL;
    }

    return
        &audio->clips[version];
}


/* ============================================================
 * AUDIO THREAD
 * ============================================================
 */

static void *ciga_audio_thread(
    void *param)
{
    struct ciga_audio_source *audio =
        param;

    while (true) {

        pthread_mutex_lock(
            &audio->mutex
        );

        if (
            audio->shutting_down
        ) {

            pthread_mutex_unlock(
                &audio->mutex
            );

            break;
        }

        if (
            !audio->playing
        ) {

            pthread_mutex_unlock(
                &audio->mutex
            );

            os_event_timedwait(
                audio->wake_event,
                100
            );

            continue;
        }

        const int version =
            audio->version;

        const uint32_t position =
            audio->position;

        const uint64_t generation =
            audio->generation;

        const uint64_t start_timestamp =
            audio->start_timestamp;

        struct ciga_audio_clip *clip =
            ciga_audio_get_clip(
                audio,
                version
            );

        if (
            !clip ||
            !clip->loaded ||
            position >= clip->frames
        ) {

            audio->playing =
                false;

            pthread_mutex_unlock(
                &audio->mutex
            );

            continue;
        }

        const uint32_t remaining =
            clip->frames -
            position;

        const uint32_t frames =
            remaining >
            CIGA_AUDIO_CHUNK_FRAMES
            ? CIGA_AUDIO_CHUNK_FRAMES
            : remaining;

        const uint64_t timestamp =
            start_timestamp +
            util_mul_div64(
                position,
                1000000000ULL,
                clip->sample_rate
            );

        const uint8_t *left =
            (
                const uint8_t *
            )(
                clip->data[0] +
                position
            );

        const uint8_t *right =
            NULL;

        if (
            clip->data[1]
        ) {
            right =
                (
                    const uint8_t *
                )(
                    clip->data[1] +
                    position
                );
        }

        pthread_mutex_unlock(
            &audio->mutex
        );

        struct obs_source_audio output =
            {0};

        output.data[0] =
            left;

        if (
            right
        ) {
            output.data[1] =
                right;
        }

        output.frames =
            frames;

        output.speakers =
            clip->speakers;

        output.format =
            AUDIO_FORMAT_FLOAT_PLANAR;

        output.samples_per_sec =
            clip->sample_rate;

        output.timestamp =
            timestamp;

        obs_source_output_audio(
            audio->source,
            &output
        );

        pthread_mutex_lock(
            &audio->mutex
        );

        if (
            generation ==
            audio->generation &&
            audio->playing
        ) {

            audio->position +=
                frames;

            if (
                audio->position >=
                clip->frames
            ) {

                audio->position =
                    clip->frames;

                audio->playing =
                    false;
            }
        }

        const bool still_playing =
            generation ==
            audio->generation &&
            audio->playing;

        const uint32_t next_position =
            audio->position;

        pthread_mutex_unlock(
            &audio->mutex
        );

        if (
            still_playing
        ) {

            const uint64_t next_timestamp =
                start_timestamp +
                util_mul_div64(
                    next_position,
                    1000000000ULL,
                    clip->sample_rate
                );

            os_sleepto_ns(
                next_timestamp
            );
        }
    }

    return NULL;
}


/* ============================================================
 * SOURCE NAME
 * ============================================================
 */

static const char *ciga_audio_get_name(
    void *type_data)
{
    UNUSED_PARAMETER(type_data);

    return "CIGA - Lost Signal Audio";
}


/* ============================================================
 * SOURCE CREATE
 * ============================================================
 */

static void *ciga_audio_create(
    obs_data_t *settings,
    obs_source_t *source)
{
    UNUSED_PARAMETER(settings);

    struct ciga_audio_source *audio =
        bzalloc(
            sizeof(*audio)
        );

    if (!audio) {
        return NULL;
    }

    audio->source =
        source;

    if (
        pthread_mutex_init(
            &audio->mutex,
            NULL
        ) != 0
    ) {

        bfree(
            audio
        );

        return NULL;
    }

    if (
        os_event_init(
            &audio->wake_event,
            OS_EVENT_TYPE_AUTO
        ) != 0
    ) {

        pthread_mutex_destroy(
            &audio->mutex
        );

        bfree(
            audio
        );

        return NULL;
    }

    const bool normal =
        ciga_audio_clip_load(
            &audio->clips[CIGA_AUDIO_NORMAL],
            "audio/normal.wav"
        );

    const bool halloween =
        ciga_audio_clip_load(
            &audio->clips[CIGA_AUDIO_HALLOWEEN],
            "audio/halloween.wav"
        );

    const bool november =
        ciga_audio_clip_load(
            &audio->clips[CIGA_AUDIO_NOVEMBER],
            "audio/november.wav"
        );

    const bool christmas =
        ciga_audio_clip_load(
            &audio->clips[CIGA_AUDIO_CHRISTMAS],
            "audio/christmas.wav"
        );

    if (
        !normal &&
        !halloween &&
        !november &&
        !christmas
    ) {

        blog(
            LOG_ERROR,
            "[CIGA-AUDIO] No WAV files could be loaded."
        );

        os_event_destroy(
            audio->wake_event
        );

        pthread_mutex_destroy(
            &audio->mutex
        );

        bfree(
            audio
        );

        return NULL;
    }

    pthread_mutex_lock(
        &g_audio_source_mutex
    );

    if (
        g_audio_source
    ) {

        pthread_mutex_unlock(
            &g_audio_source_mutex
        );

        blog(
            LOG_ERROR,
            "[CIGA-AUDIO] Only one CIGA audio source instance is supported."
        );

        for (
            int i = 0;
            i < 4;
            i++
        ) {
            ciga_audio_clip_free(
                &audio->clips[i]
            );
        }

        os_event_destroy(
            audio->wake_event
        );

        pthread_mutex_destroy(
            &audio->mutex
        );

        bfree(
            audio
        );

        return NULL;
    }

    g_audio_source =
        audio;

    pthread_mutex_unlock(
        &g_audio_source_mutex
    );

    if (
        pthread_create(
            &audio->thread,
            NULL,
            ciga_audio_thread,
            audio
        ) != 0
    ) {

        pthread_mutex_lock(
            &g_audio_source_mutex
        );

        if (
            g_audio_source ==
            audio
        ) {
            g_audio_source =
                NULL;
        }

        pthread_mutex_unlock(
            &g_audio_source_mutex
        );

        for (
            int i = 0;
            i < 4;
            i++
        ) {
            ciga_audio_clip_free(
                &audio->clips[i]
            );
        }

        os_event_destroy(
            audio->wake_event
        );

        pthread_mutex_destroy(
            &audio->mutex
        );

        bfree(
            audio
        );

        return NULL;
    }

    audio->thread_valid =
        true;

    /*
     * Keep the audio source visible in the OBS mixer even
     * when no clip is currently playing.
     *
     * Playback state is controlled independently by
     * audio->playing.
     */

    obs_source_set_audio_active(
        audio->source,
        true
    );

    blog(
        LOG_INFO,
        "[CIGA-AUDIO] CIGA - Lost Signal Audio source created."
    );

    return audio;
}


/* ============================================================
 * SOURCE DESTROY
 * ============================================================
 */

static void ciga_audio_destroy(
    void *data)
{
    struct ciga_audio_source *audio =
        data;

    if (!audio) {
        return;
    }

    if (audio->source) {

        obs_source_set_audio_active(
            audio->source,
            false
        );
    }

    pthread_mutex_lock(
        &g_audio_source_mutex
    );

    if (
        g_audio_source ==
        audio
    ) {
        g_audio_source =
            NULL;
    }

    pthread_mutex_unlock(
        &g_audio_source_mutex
    );

    pthread_mutex_lock(
        &audio->mutex
    );

    audio->shutting_down =
        true;

    audio->playing =
        false;

    audio->generation++;

    pthread_mutex_unlock(
        &audio->mutex
    );

    os_event_signal(
        audio->wake_event
    );

    if (
        audio->thread_valid
    ) {

        void *thread_result =
            NULL;

        pthread_join(
            audio->thread,
            &thread_result
        );
    }

    for (
        int i = 0;
        i < 4;
        i++
    ) {
        ciga_audio_clip_free(
            &audio->clips[i]
        );
    }

    os_event_destroy(
        audio->wake_event
    );

    pthread_mutex_destroy(
        &audio->mutex
    );

    bfree(
        audio
    );

    blog(
        LOG_INFO,
        "[CIGA-AUDIO] CIGA - Lost Signal Audio source destroyed."
    );
}


/* ============================================================
 * PLAY
 * ============================================================
 */

bool ciga_audio_source_play(
    int version)
{
    pthread_mutex_lock(
        &g_audio_source_mutex
    );

    struct ciga_audio_source *audio =
        g_audio_source;

    if (!audio) {

        pthread_mutex_unlock(
            &g_audio_source_mutex
        );

        blog(
            LOG_WARNING,
            "[CIGA-AUDIO] No CIGA - Lost Signal Audio source exists in OBS."
        );

        return false;
    }

    pthread_mutex_lock(
        &audio->mutex
    );

    pthread_mutex_unlock(
        &g_audio_source_mutex
    );

    if (
        version < CIGA_AUDIO_NORMAL ||
        version > CIGA_AUDIO_CHRISTMAS
    ) {
        version =
            CIGA_AUDIO_NORMAL;
    }

    struct ciga_audio_clip *clip =
        ciga_audio_get_clip(
            audio,
            version
        );

    if (
        !clip ||
        !clip->loaded
    ) {

        pthread_mutex_unlock(
            &audio->mutex
        );

        blog(
            LOG_WARNING,
            "[CIGA-AUDIO] Requested audio version %d is unavailable.",
            version
        );

        return false;
    }

    audio->version =
        version;

    audio->position =
        0;

    audio->start_timestamp =
        os_gettime_ns();

    audio->generation++;

    audio->playing =
        true;

    pthread_mutex_unlock(
        &audio->mutex
    );

    obs_source_set_audio_active(
        audio->source,
        true
    );

    os_event_signal(
        audio->wake_event
    );

    blog(
        LOG_INFO,
        "[CIGA-AUDIO] Playback started for version %d.",
        version
    );

    return true;
}


/* ============================================================
 * STOP
 * ============================================================
 */

void ciga_audio_source_stop(void)
{
    pthread_mutex_lock(
        &g_audio_source_mutex
    );

    struct ciga_audio_source *audio =
        g_audio_source;

    if (!audio) {

        pthread_mutex_unlock(
            &g_audio_source_mutex
        );

        return;
    }

    pthread_mutex_lock(
        &audio->mutex
    );

    pthread_mutex_unlock(
        &g_audio_source_mutex
    );

    audio->playing =
        false;

    audio->generation++;

    pthread_mutex_unlock(
        &audio->mutex
    );


    os_event_signal(
        audio->wake_event
    );
}


/* ============================================================
 * SOURCE INFO
 * ============================================================
 */

static struct obs_source_info ciga_audio_source_info = {
    .id = "ciga_lost_signal_audio",
    .type = OBS_SOURCE_TYPE_INPUT,

    .output_flags =
        OBS_SOURCE_AUDIO,

    .get_name =
        ciga_audio_get_name,

    .create =
        ciga_audio_create,

    .destroy =
        ciga_audio_destroy,

    .icon_type =
        OBS_ICON_TYPE_AUDIO_INPUT,
};


struct ciga_audio_find_source_context {
    obs_source_t *source;
};

static bool ciga_audio_find_source_enum(
    void *data,
    obs_source_t *source)
{
    struct ciga_audio_find_source_context *context =
        data;

    if (!context ||
        !source) {
        return true;
    }

    const char *id =
        obs_source_get_unversioned_id(
            source
        );

    if (
        id &&
        strcmp(
            id,
            "ciga_lost_signal_audio"
        ) == 0
    ) {

        context->source =
            obs_source_get_ref(
                source
            );

        return false;
    }

    return true;
}

static obs_source_t *ciga_audio_find_existing_source(void)
{
    struct ciga_audio_find_source_context context =
        {0};

    obs_enum_sources(
        ciga_audio_find_source_enum,
        &context
    );

    return context.source;
}

struct ciga_audio_scene_contains_context {
    obs_source_t *target;
    bool found;
};

static bool ciga_audio_scene_contains_enum(
    obs_scene_t *scene,
    obs_sceneitem_t *item,
    void *data)
{
    UNUSED_PARAMETER(scene);

    struct ciga_audio_scene_contains_context *context =
        data;

    if (!context ||
        !item) {
        return true;
    }

    if (
        obs_sceneitem_get_source(
            item
        ) ==
        context->target
    ) {

        context->found =
            true;

        return false;
    }

    return true;
}

static bool ciga_audio_scene_contains_source(
    obs_scene_t *scene,
    obs_source_t *source)
{
    struct ciga_audio_scene_contains_context context = {
        .target = source,
        .found = false
    };

    obs_scene_enum_items(
        scene,
        ciga_audio_scene_contains_enum,
        &context
    );

    return context.found;
}

static void ciga_audio_attach_to_scene(
    obs_scene_t *scene,
    obs_source_t *source)
{
    if (!scene ||
        !source) {
        return;
    }

    if (
        ciga_audio_scene_contains_source(
            scene,
            source
        )
    ) {
        return;
    }

    obs_scene_add(
        scene,
        source
    );

    blog(
        LOG_INFO,
        "[CIGA-AUDIO] Added CIGA - Lost Signal Audio to scene: %s",
        obs_source_get_name(
            obs_scene_get_source(
                scene
            )
        )
    );
}

static bool ciga_audio_attach_existing_scene_enum(
    void *data,
    obs_source_t *scene_source)
{
    obs_source_t *audio_source =
        data;

    if (
        !scene_source ||
        !audio_source
    ) {
        return true;
    }

    obs_scene_t *scene =
        obs_scene_from_source(
            scene_source
        );

    if (scene) {
        ciga_audio_attach_to_scene(
            scene,
            audio_source
        );
    }

    return true;
}

static void ciga_audio_global_source_create(
    void *data,
    calldata_t *calldata)
{
    UNUSED_PARAMETER(data);

    if (!g_audio_source ||
        !g_audio_source->source ||
        !calldata) {
        return;
    }

    obs_source_t *source =
        calldata_ptr(
            calldata,
            "source"
        );

    if (!source) {
        return;
    }

    if (
        !obs_source_is_scene(
            source
        )
    ) {
        return;
    }

    obs_scene_t *scene =
        obs_scene_from_source(
            source
        );

    if (!scene) {
        return;
    }

    ciga_audio_attach_to_scene(
        scene,
        g_audio_source->source
    );
}

static void ciga_audio_auto_attach_all_scenes(
    obs_source_t *audio_source)
{
    if (!audio_source) {
        return;
    }

    obs_enum_scenes(
        ciga_audio_attach_existing_scene_enum,
        audio_source
    );
}
/* ============================================================
 * REGISTER
 * ============================================================
 */

static obs_source_t *ciga_audio_ensure_source(void)
{
    /*
     * First use the currently alive CIGA audio source,
     * while taking a temporary OBS reference.
     */

    pthread_mutex_lock(
        &g_audio_source_mutex
    );

    struct ciga_audio_source *audio =
        g_audio_source;

    obs_source_t *source =
        NULL;

    if (
        audio &&
        audio->source
    ) {

        source =
            obs_source_get_ref(
                audio->source
            );
    }

    pthread_mutex_unlock(
        &g_audio_source_mutex
    );

    if (source) {
        return source;
    }

    /*
     * The previous source may have been destroyed because
     * its scene collection was unloaded. Check OBS once more
     * before creating a new source.
     */

    source =
        ciga_audio_find_existing_source();

    if (source) {
        return source;
    }

    /*
     * No source exists anymore. Create a fresh one for the
     * current scene collection.
     */

    source =
        obs_source_create(
            "ciga_lost_signal_audio",
            "CIGA - Lost Signal Audio",
            NULL,
            NULL
        );

    if (!source) {

        blog(
            LOG_ERROR,
            "[CIGA-AUDIO] Could not create CIGA - Lost Signal Audio source."
        );

        return NULL;
    }

    blog(
        LOG_INFO,
        "[CIGA-AUDIO] Created CIGA - Lost Signal Audio source for current scene collection."
    );

    return source;
}
static void ciga_audio_frontend_event(
    enum obs_frontend_event event,
    void *private_data);
void ciga_audio_source_unregister(void)
{
    if (
        g_auto_attach_connected
    ) {

        obs_frontend_remove_event_callback(
            ciga_audio_frontend_event,
            NULL
        );

        g_auto_attach_connected =
            false;
    }

    g_scene_collection_changing =
        false;

    ciga_audio_source_stop();

    blog(
        LOG_INFO,
        "[CIGA-AUDIO] Frontend callbacks removed and audio stopped."
    );
}

static void ciga_audio_frontend_event(
    enum obs_frontend_event event,
    void *private_data)
{
    UNUSED_PARAMETER(private_data);

    if (
        event ==
        OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING
    ) {

        g_scene_collection_changing =
            true;

        blog(
            LOG_INFO,
            "[CIGA-AUDIO] Scene collection changing. Stopping audio and allowing source cleanup."
        );

        ciga_audio_source_stop();

        return;
    }

    if (
        event ==
        OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP
    ) {

        g_scene_collection_changing =
            true;

        blog(
            LOG_INFO,
            "[CIGA-AUDIO] Scene collection cleanup."
        );

        ciga_audio_source_stop();

        return;
    }

    if (
        event ==
        OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED
    ) {

        g_scene_collection_changing =
            false;

        blog(
            LOG_INFO,
            "[CIGA-AUDIO] Scene collection changed. Ensuring audio source."
        );

        obs_source_t *source =
            ciga_audio_ensure_source();

        if (source) {

            ciga_audio_auto_attach_all_scenes(
                source
            );

            obs_source_release(
                source
            );
        }

        return;
    }

    if (
        event ==
        OBS_FRONTEND_EVENT_FINISHED_LOADING
    ) {

        g_scene_collection_changing =
            false;

        blog(
            LOG_INFO,
            "[CIGA-AUDIO] OBS finished loading. Ensuring audio source."
        );

        obs_source_t *source =
            ciga_audio_ensure_source();

        if (source) {

            ciga_audio_auto_attach_all_scenes(
                source
            );

            obs_source_release(
                source
            );
        }

        return;
    }

    if (
        event ==
        OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED
    ) {

        if (
            g_scene_collection_changing
        ) {
            return;
        }

        obs_source_t *source =
            ciga_audio_ensure_source();

        if (source) {

            ciga_audio_auto_attach_all_scenes(
                source
            );

            obs_source_release(
                source
            );
        }

        return;
    }
}
void ciga_audio_source_register(void)
{
    obs_register_source(
        &ciga_audio_source_info
    );

    blog(
        LOG_INFO,
        "[CIGA-AUDIO] Registered source type: %s",
        ciga_audio_source_info.id
    );

    /*
     * Only observe frontend scene events.
     * No source_create callback is needed.
     */

    if (
        !g_auto_attach_connected
    ) {

        obs_frontend_add_event_callback(
            ciga_audio_frontend_event,
            NULL
        );

        g_auto_attach_connected =
            true;
    }

    blog(
        LOG_INFO,
        "[CIGA-AUDIO] Automatic scene attachment enabled."
    );
}
















