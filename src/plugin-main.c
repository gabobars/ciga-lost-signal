#include <obs-module.h>
#include <time.h>
#include <string.h>
#include <plugin-support.h>

#include "ciga-audio-source.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

struct ciga_transition {
    obs_source_t *source;

    int mode;
    int version;

    gs_effect_t *effect;
    gs_texture_t *report_texture;

    gs_eparam_t *tex_a_param;
    gs_eparam_t *tex_b_param;
    gs_eparam_t *progress_param;
    gs_eparam_t *theme_version_param;
    gs_eparam_t *report_param;
};

static const char *ciga_transition_get_name(void *type_data)
{
    UNUSED_PARAMETER(type_data);

    return "CIGA - Lost Signal";
}


enum {
    CIGA_MODE_AUTO = 0,
    CIGA_MODE_MANUAL = 1,
};

enum {
    CIGA_VERSION_NORMAL = 0,
    CIGA_VERSION_HALLOWEEN = 1,
    CIGA_VERSION_NOVEMBER = 2,
    CIGA_VERSION_CHRISTMAS = 3,
};

static bool ciga_transition_mode_modified(
    obs_properties_t *props,
    obs_property_t *property,
    obs_data_t *settings)
{
    UNUSED_PARAMETER(property);

    const char *mode =
        obs_data_get_string(
            settings,
            "mode"
        );

    const bool manual =
        mode &&
        strcmp(mode, "manual") == 0;

    obs_property_t *version_property =
        obs_properties_get(
            props,
            "version"
        );

    if (version_property) {
        obs_property_set_enabled(
            version_property,
            manual
        );
    }

    return true;
}

static obs_properties_t *ciga_transition_properties(
    void *data)
{
    obs_properties_t *props =
        obs_properties_create();

    obs_property_t *mode_property =
        obs_properties_add_list(
            props,
            "mode",
            "Mode",
            OBS_COMBO_TYPE_LIST,
            OBS_COMBO_FORMAT_STRING
        );

    obs_property_list_add_string(
        mode_property,
        "Auto",
        "auto"
    );

    obs_property_list_add_string(
        mode_property,
        "Manual",
        "manual"
    );

    obs_property_set_modified_callback(
        mode_property,
        ciga_transition_mode_modified
    );

    obs_property_t *version_property =
        obs_properties_add_list(
            props,
            "version",
            "Version",
            OBS_COMBO_TYPE_LIST,
            OBS_COMBO_FORMAT_STRING
        );

    obs_property_list_add_string(
        version_property,
        "Normal",
        "normal"
    );

    obs_property_list_add_string(
        version_property,
        "Halloween",
        "halloween"
    );

    obs_property_list_add_string(
        version_property,
        "November",
        "november"
    );

    obs_property_list_add_string(
        version_property,
        "Christmas",
        "christmas"
    );

    bool manual = false;

    struct ciga_transition *transition =
        data;

    if (transition) {
        manual =
            transition->mode ==
            CIGA_MODE_MANUAL;
    }

    obs_property_set_enabled(
        version_property,
        manual
    );

    return props;
}

static void ciga_transition_defaults(
    obs_data_t *settings)
{
    obs_data_set_default_string(
        settings,
        "mode",
        "auto"
    );

    obs_data_set_default_string(
        settings,
        "version",
        "normal"
    );
}

static void ciga_transition_update(
    void *data,
    obs_data_t *settings)
{
    struct ciga_transition *transition =
        data;

    if (!transition) {
        return;
    }

    transition->mode =
        CIGA_MODE_AUTO;

    transition->version =
        CIGA_VERSION_NORMAL;

    if (!settings) {
        return;
    }

    const char *mode =
        obs_data_get_string(
            settings,
            "mode"
        );

    const char *version =
        obs_data_get_string(
            settings,
            "version"
        );

    if (mode &&
        strcmp(mode, "manual") == 0) {

        transition->mode =
            CIGA_MODE_MANUAL;
    }

    if (version) {

        if (strcmp(version, "halloween") == 0) {

            transition->version =
                CIGA_VERSION_HALLOWEEN;
        }
        else if (
            strcmp(version, "november") == 0
        ) {

            transition->version =
                CIGA_VERSION_NOVEMBER;
        }
        else if (
            strcmp(version, "christmas") == 0
        ) {

            transition->version =
                CIGA_VERSION_CHRISTMAS;
        }
        else {

            transition->version =
                CIGA_VERSION_NORMAL;
        }
    }
}

static int ciga_transition_get_effective_version(
    void *data)
{
    struct ciga_transition *transition =
        data;

    if (!transition) {
        return CIGA_VERSION_NORMAL;
    }

    if (transition->mode ==
        CIGA_MODE_MANUAL) {

        return transition->version;
    }

    time_t now =
        time(NULL);

    struct tm local_time;

#ifdef _WIN32

    if (localtime_s(
            &local_time,
            &now
        ) != 0) {

        return CIGA_VERSION_NORMAL;
    }

#else

    if (!localtime_r(
            &now,
            &local_time
        )) {

        return CIGA_VERSION_NORMAL;
    }

#endif

    const int month =
        local_time.tm_mon + 1;

    switch (month) {

    case 10:
        return CIGA_VERSION_HALLOWEEN;

    case 11:
        return CIGA_VERSION_NOVEMBER;

    case 12:
        return CIGA_VERSION_CHRISTMAS;

    default:
        return CIGA_VERSION_NORMAL;
    }
}
static void *ciga_transition_create(obs_data_t *settings, obs_source_t *source)
{
    UNUSED_PARAMETER(settings);

    struct ciga_transition *transition =
        bzalloc(sizeof(*transition));

    transition->source = source;

    ciga_transition_update(transition, settings);

    char *effect_file =
        obs_module_file("ciga_transition.effect");

    char *report_file =
        obs_module_file("signal_lost.png");

    char *error_string = NULL;

    obs_enter_graphics();

    transition->effect =
        gs_effect_create_from_file(
            effect_file,
            &error_string
        );

    transition->report_texture =
        gs_texture_create_from_file(
            report_file
        );

    obs_leave_graphics();

    bfree(effect_file);
    bfree(report_file);

    if (!transition->effect) {
        blog(
            LOG_ERROR,
            "[CIGA] Failed to load ciga_transition.effect: %s",
            error_string ? error_string : "unknown error"
        );

        bfree(error_string);

        bfree(transition);

        return NULL;
    }

    bfree(error_string);

    if (!transition->report_texture) {
        blog(
            LOG_ERROR,
            "[CIGA] Failed to load signal_lost.png"
        );

        obs_enter_graphics();

        gs_effect_destroy(transition->effect);

        obs_leave_graphics();

        bfree(transition);

        return NULL;
    }

    transition->tex_a_param =
        gs_effect_get_param_by_name(
            transition->effect,
            "tex_a"
        );

    transition->tex_b_param =
        gs_effect_get_param_by_name(
            transition->effect,
            "tex_b"
        );

    transition->progress_param =
        gs_effect_get_param_by_name(
            transition->effect,
            "progress"
        );
    transition->theme_version_param =
        gs_effect_get_param_by_name(
            transition->effect,
            "theme_version"
        );

    transition->report_param =
        gs_effect_get_param_by_name(
            transition->effect,
            "report_tex"
        );

    if (!transition->tex_a_param ||
        !transition->tex_b_param ||
        !transition->progress_param ||
        !transition->report_param ||
        !transition->theme_version_param) {

        blog(
            LOG_ERROR,
            "[CIGA] Required shader parameters are missing."
        );

        obs_enter_graphics();

        gs_effect_destroy(
            transition->effect
        );

        gs_texture_destroy(
            transition->report_texture
        );

        obs_leave_graphics();

        bfree(transition);

        return NULL;
    }

    return transition;
}

static void ciga_transition_destroy(void *data)
{
    struct ciga_transition *transition = data;

    if (!transition)
        return;

    obs_enter_graphics();

    if (transition->effect)
        gs_effect_destroy(
            transition->effect
        );

    if (transition->report_texture)
        gs_texture_destroy(
            transition->report_texture
        );

    obs_leave_graphics();

    bfree(transition);
}

static void ciga_transition_callback(
    void *data,
    gs_texture_t *a,
    gs_texture_t *b,
    float t,
    uint32_t cx,
    uint32_t cy
)
{
    struct ciga_transition *transition = data;

    if (!a && !b)
        return;

    if (!a && b) {
        a = b;
        t = 1.0f;
    } else if (a && !b) {
        b = a;
        t = 0.0f;
    }

    const bool previous_srgb =
        gs_framebuffer_srgb_enabled();

    gs_enable_framebuffer_srgb(true);

    gs_effect_set_texture_srgb(
        transition->tex_a_param,
        a
    );

    gs_effect_set_texture_srgb(
        transition->tex_b_param,
        b
    );

    gs_effect_set_texture(
        transition->report_param,
        transition->report_texture
    );

    gs_effect_set_float(
        transition->progress_param,
        t
    );
    gs_effect_set_float(
        transition->theme_version_param,
        (float)ciga_transition_get_effective_version(
            transition
        )
    );

    while (gs_effect_loop(
        transition->effect,
        "CIGA"
    )) {
        gs_draw_sprite(
            NULL,
            0,
            cx,
            cy
        );
    }

    gs_enable_framebuffer_srgb(
        previous_srgb
    );
}

static void ciga_transition_video_render(
    void *data,
    gs_effect_t *effect
)
{
    UNUSED_PARAMETER(effect);

    struct ciga_transition *transition = data;

    const bool previous_linear_srgb =
        gs_set_linear_srgb(true);

    obs_transition_video_render2(
        transition->source,
        ciga_transition_callback,
        NULL
    );

    gs_set_linear_srgb(
        previous_linear_srgb
    );
}

static float ciga_mix_a(void *data, float t)
{
    UNUSED_PARAMETER(data);

    return 1.0f - t;
}

static float ciga_mix_b(void *data, float t)
{
    UNUSED_PARAMETER(data);

    return t;
}

static bool ciga_transition_audio_render(
    void *data,
    uint64_t *ts_out,
    struct obs_source_audio_mix *audio,
    uint32_t mixers,
    size_t channels,
    size_t sample_rate
)
{
    struct ciga_transition *transition = data;

    return obs_transition_audio_render(
        transition->source,
        ts_out,
        audio,
        mixers,
        channels,
        sample_rate,
        ciga_mix_a,
        ciga_mix_b
    );
}

static enum gs_color_space ciga_transition_video_get_color_space(
    void *data,
    size_t count,
    const enum gs_color_space *preferred_spaces
)
{
    struct ciga_transition *transition = data;

    const enum gs_color_space transition_space =
        obs_transition_video_get_color_space(
            transition->source
        );

    enum gs_color_space space =
        transition_space;

    for (size_t i = 0; i < count; ++i) {
        space = preferred_spaces[i];

        if (space == transition_space)
            break;
    }

    return space;
}

static void ciga_transition_start(
    void *data
)
{
    struct ciga_transition *transition =
        data;

    if (!transition) {
        return;
    }

    const int version =
        ciga_transition_get_effective_version(
            transition
        );

    ciga_audio_source_play(
        version
    );
}

static void ciga_transition_stop(
    void *data
)
{
    UNUSED_PARAMETER(data);

    ciga_audio_source_stop();
}
static struct obs_source_info ciga_transition_info = {
    .id = "ciga_lost_signal_transition",
    .type = OBS_SOURCE_TYPE_TRANSITION,

    .get_name =
        ciga_transition_get_name,

    .create =
        ciga_transition_create,

    .destroy =
        ciga_transition_destroy,

    .update =
        ciga_transition_update,

    .get_defaults =
        ciga_transition_defaults,

    .get_properties =
        ciga_transition_properties,

    .video_render =
        ciga_transition_video_render,

    .audio_render =
        ciga_transition_audio_render,

    .video_get_color_space =
        ciga_transition_video_get_color_space,

    .transition_start =
        ciga_transition_start,

    .transition_stop =
        ciga_transition_stop,
};

bool obs_module_load(void)
{
    obs_log(
        LOG_INFO,
        "[ciga-transition] registered source type: %s",
        ciga_transition_info.id
    );

    obs_log(
        LOG_INFO,
        "[ciga-transition] display name: %s",
        ciga_transition_get_name(NULL)
    );
    ciga_audio_source_register();

    obs_register_source(
        &ciga_transition_info
    );
    const char *registered_name =
        obs_source_get_display_name(
            ciga_transition_info.id
        );

    if (registered_name) {

        obs_log(
            LOG_INFO,
            "[ciga-transition] SOURCE REGISTERED: %s",
            registered_name
        );
    }
    else {

        obs_log(
            LOG_ERROR,
            "[ciga-transition] SOURCE WAS NOT REGISTERED: %s",
            ciga_transition_info.id
        );
    }

    obs_log(
        LOG_INFO,
        "CIGA - Lost Signal transition loaded successfully"
    );

    return true;
}

void obs_module_unload(void)
{
    ciga_audio_source_unregister();

    obs_log(
        LOG_INFO,
        "CIGA - Lost Signal transition unloaded"
    );
}












