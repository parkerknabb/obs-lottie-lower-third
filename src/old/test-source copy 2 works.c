#include <obs-module.h>
#include <plugin-support.h>

// ─────────────────────────────────────────────────────────
// Passthrough hide transition
// ─────────────────────────────────────────────────────────

struct test_source; // forward declaration
static void test_source_tick_exit(struct test_source *ctx,
                                  uint32_t cx, uint32_t cy,
                                  gs_texture_t *a);

struct passthrough_transition {
    obs_source_t       *source;
    struct test_source *owner;
};

static const char *passthrough_get_name(void *type_data)
{
    UNUSED_PARAMETER(type_data);
    return "Lower Third Passthrough";
}

static void *passthrough_create(obs_data_t *settings, obs_source_t *source)
{
    UNUSED_PARAMETER(settings);
    blog(LOG_INFO, "[passthrough] create: source=%p", source);
    struct passthrough_transition *ctx =
        bzalloc(sizeof(struct passthrough_transition));
    ctx->source = source;
    return ctx;
}

static void passthrough_destroy(void *data)
{
    blog(LOG_INFO, "[passthrough] destroy");
    bfree(data);
}

static void passthrough_callback(void *data, gs_texture_t *a,
                                 gs_texture_t *b, float t,
                                 uint32_t cx, uint32_t cy)
{
    UNUSED_PARAMETER(b);
    struct passthrough_transition *ctx =
        (struct passthrough_transition *)data;

    static uint32_t frame_count = 0;
    if (frame_count++ % 30 == 0) {
        blog(LOG_INFO,
             "[passthrough] callback frame=%u t=%.3f a=%p cx=%u cy=%u",
             frame_count, t, a, cx, cy);
    }

    if (!a) {
        blog(LOG_WARNING, "[passthrough] callback: texture A is NULL");
        return;
    }

    if (ctx->owner) {
        test_source_tick_exit(ctx->owner, cx, cy, a);
        return;
    }

    gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_eparam_t *image  = gs_effect_get_param_by_name(effect, "image");
    gs_effect_set_texture(image, a);
    while (gs_effect_loop(effect, "Draw"))
        gs_draw_sprite(a, 0, cx, cy);
}

static void passthrough_render(void *data, gs_effect_t *effect)
{
    UNUSED_PARAMETER(effect);
    struct passthrough_transition *ctx =
        (struct passthrough_transition *)data;
    obs_transition_video_render(ctx->source, passthrough_callback);
}

static bool passthrough_audio_render(void *data, uint64_t *ts_out,
                                     struct obs_source_audio_mix *audio,
                                     uint32_t mixers, size_t channels,
                                     size_t sample_rate)
{
    return obs_transition_audio_render(
        ((struct passthrough_transition *)data)->source,
        ts_out, audio, mixers, channels, sample_rate,
        NULL, NULL);
}

static uint32_t passthrough_get_width(void *data)
{
    struct passthrough_transition *ctx =
        (struct passthrough_transition *)data;
    uint32_t cx, cy;
    obs_transition_get_size(ctx->source, &cx, &cy);
    return cx;
}

static uint32_t passthrough_get_height(void *data)
{
    struct passthrough_transition *ctx =
        (struct passthrough_transition *)data;
    uint32_t cx, cy;
    obs_transition_get_size(ctx->source, &cx, &cy);
    return cy;
}

static struct obs_source_info passthrough_transition_info = {
    .id           = "test_passthrough_transition",
    .type         = OBS_SOURCE_TYPE_TRANSITION,
    .output_flags = OBS_SOURCE_VIDEO,
    .get_name     = passthrough_get_name,
    .create       = passthrough_create,
    .destroy      = passthrough_destroy,
    .video_render = passthrough_render,
    .audio_render = passthrough_audio_render,
    .get_width    = passthrough_get_width,
    .get_height   = passthrough_get_height,
};

// ─────────────────────────────────────────────────────────
// Test lower third source
// ─────────────────────────────────────────────────────────

struct test_source {
    obs_source_t *source;
    obs_source_t *hide_transition;
    enum { HIDDEN, ENTERING, HOLDING, EXITING } state;
    uint32_t current_frame;
    uint32_t hold_start_frame;
    uint32_t exit_start_frame;
    uint32_t end_frame;
};

static void scene_item_visible(void *data, calldata_t *params);
static void global_source_create(void *data, calldata_t *params);

// ── Scene item search ─────────────────────────────────────

// Passed to obs_scene_enum_items to find the item for our source.
struct find_item_data {
    obs_source_t    *target;
    obs_sceneitem_t *result;
};

static bool find_item_cb(obs_scene_t *scene, obs_sceneitem_t *item, void *data)
{
    UNUSED_PARAMETER(scene);
    struct find_item_data *fd = (struct find_item_data *)data;
    if (obs_sceneitem_get_source(item) == fd->target) {
        fd->result = item;
        return false; // stop enumeration
    }
    return true;
}

// Passed to obs_enum_scenes to search every scene for our item.
struct find_item_in_scenes_data {
    obs_source_t    *target;
    obs_sceneitem_t *result;
};

static bool find_item_in_scenes_cb(void *data, obs_source_t *scene_source)
{
    struct find_item_in_scenes_data *fd =
        (struct find_item_in_scenes_data *)data;
    obs_scene_t *scene = obs_scene_from_source(scene_source);
    if (!scene)
        return true;

    struct find_item_data find = {.target = fd->target, .result = NULL};
    obs_scene_enum_items(scene, find_item_cb, &find);

    if (find.result) {
        fd->result = find.result;
        return false; // stop scene enumeration
    }
    return true;
}

// ── Transition attachment ─────────────────────────────────

static uint32_t get_exit_duration_ms(struct test_source *ctx)
{
    struct obs_video_info ovi;
    if (!obs_get_video_info(&ovi) || ovi.fps_den == 0) {
        blog(LOG_WARNING, "[test_source] get_exit_duration_ms: no video info, using 1000ms");
        return 1000;
    }
    uint32_t exit_frames = ctx->end_frame - ctx->exit_start_frame;
    uint32_t ms = (uint32_t)((double)exit_frames * ovi.fps_den /
                             ovi.fps_num * 1000.0);
    blog(LOG_INFO, "[test_source] exit duration: %u frames @ %u/%u fps = %u ms",
         exit_frames, ovi.fps_num, ovi.fps_den, ms);
    return ms;
}

static void attach_hide_transition(struct test_source *ctx,
                                   obs_sceneitem_t *item)
{
    if (ctx->hide_transition) {
        obs_source_release(ctx->hide_transition);
        ctx->hide_transition = NULL;
    }

    ctx->hide_transition = obs_source_create_private(
        "test_passthrough_transition",
        "lower_third_hide_transition", NULL);

    if (!ctx->hide_transition) {
        blog(LOG_ERROR, "[test_source] attach: create_private FAILED");
        return;
    }

    struct passthrough_transition *td =
        obs_source_get_type_data(ctx->hide_transition);
    if (td) {
        td->owner = ctx;
        blog(LOG_INFO, "[test_source] attach: owner back-pointer set");
    } else {
        blog(LOG_ERROR, "[test_source] attach: could not get transition type_data");
    }

    uint32_t duration_ms = get_exit_duration_ms(ctx);

    obs_sceneitem_set_transition(item, false, ctx->hide_transition);
    obs_sceneitem_set_transition_duration(item, false, duration_ms);

    obs_source_t *check     = obs_sceneitem_get_transition(item, false);
    uint32_t      check_dur = obs_sceneitem_get_transition_duration(item, false);
    blog(LOG_INFO,
         "[test_source] attach: set=%p dur=%ums | readback=%p dur=%ums",
         ctx->hide_transition, duration_ms, check, check_dur);

    if (check != ctx->hide_transition)
        blog(LOG_ERROR, "[test_source] attach: READBACK MISMATCH");
}

static void try_attach_hide_transition(struct test_source *ctx)
{
    struct find_item_in_scenes_data fd = {
        .target = ctx->source,
        .result = NULL,
    };
    obs_enum_scenes(find_item_in_scenes_cb, &fd);

    if (fd.result) {
        blog(LOG_INFO, "[test_source] tick: found item %p, pre-attaching hide transition",
             fd.result);
        attach_hide_transition(ctx, fd.result);
    } else {
        blog(LOG_WARNING, "[test_source] tick: source not found in any scene");
    }
}

// ── Exit animation (called from passthrough_callback) ─────

static void test_source_tick_exit(struct test_source *ctx,
                                  uint32_t cx, uint32_t cy,
                                  gs_texture_t *a)
{
    UNUSED_PARAMETER(a);
    UNUSED_PARAMETER(cx);
    UNUSED_PARAMETER(cy);

    if (ctx->state == EXITING) {
        ctx->current_frame++;
        blog(LOG_INFO, "[test_source] tick_exit: frame=%u / %u",
             ctx->current_frame, ctx->end_frame);
        if (ctx->current_frame >= ctx->end_frame) {
            blog(LOG_INFO, "[test_source] tick_exit: animation complete");
            ctx->state = HIDDEN;
        }
    }

    uint32_t color;
    switch (ctx->state) {
        case ENTERING: color = 0xFFFF0000; break;
        case HOLDING:  color = 0xFF00FF00; break;
        case EXITING:  color = 0xFFFFFF00; break;
        default:       color = 0xFF000000; break;
    }

    gs_effect_t *solid       = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");
    gs_effect_set_color(color_param, color);

    while (gs_effect_loop(solid, "Solid"))
        gs_draw_sprite(NULL, 0, 1920, 1080);
}

// ── Scene signal handling ─────────────────────────────────

static void connect_scene_signals(struct test_source *ctx,
                                  obs_source_t *scene_source)
{
    if (!obs_scene_from_source(scene_source) &&
        !obs_group_from_source(scene_source))
        return;
    signal_handler_t *handler =
        obs_source_get_signal_handler(scene_source);
    signal_handler_connect(handler, "item_visible",
                           scene_item_visible, ctx);
}

static void disconnect_scene_signals(struct test_source *ctx,
                                     obs_source_t *scene_source)
{
    if (!obs_scene_from_source(scene_source) &&
        !obs_group_from_source(scene_source))
        return;
    signal_handler_t *handler =
        obs_source_get_signal_handler(scene_source);
    signal_handler_disconnect(handler, "item_visible",
                              scene_item_visible, ctx);
}

static bool connect_scene_enum(void *data, obs_source_t *scene_source)
{
    connect_scene_signals((struct test_source *)data, scene_source);
    return true;
}

static bool disconnect_scene_enum(void *data, obs_source_t *scene_source)
{
    disconnect_scene_signals((struct test_source *)data, scene_source);
    return true;
}

static const char *test_get_name(void *type_data)
{
    UNUSED_PARAMETER(type_data);
    return "Test Lower Third";
}

static void global_source_create(void *data, calldata_t *params)
{
    struct test_source *ctx = (struct test_source *)data;
    obs_source_t *source = calldata_ptr(params, "source");
    if (source)
        connect_scene_signals(ctx, source);
}

static void scene_item_visible(void *data, calldata_t *params)
{
    struct test_source *ctx = (struct test_source *)data;
    obs_sceneitem_t *item   = calldata_ptr(params, "item");
    bool visible            = calldata_bool(params, "visible");

    if (!item || obs_sceneitem_get_source(item) != ctx->source)
        return;

    blog(LOG_INFO, "[test_source] scene_item_visible: visible=%d", visible);
}

// ── Source lifecycle ──────────────────────────────────────

static void *test_create(obs_data_t *settings, obs_source_t *source)
{
    UNUSED_PARAMETER(settings);
    blog(LOG_INFO, "[test_source] create: source=%p", source);
    struct test_source *ctx = bzalloc(sizeof(struct test_source));
    ctx->source             = source;
    ctx->state              = HIDDEN;
    ctx->hold_start_frame   = 30;
    ctx->exit_start_frame   = 90;
    ctx->end_frame          = 120;

    obs_enum_scenes(connect_scene_enum, ctx);
    signal_handler_connect(obs_get_signal_handler(), "source_create",
                           global_source_create, ctx);
    return ctx;
}

static void test_destroy(void *data)
{
    struct test_source *ctx = (struct test_source *)data;
    blog(LOG_INFO, "[test_source] destroy");

    signal_handler_disconnect(obs_get_signal_handler(), "source_create",
                              global_source_create, ctx);
    obs_enum_scenes(disconnect_scene_enum, ctx);

    if (ctx->hide_transition)
        obs_source_release(ctx->hide_transition);

    bfree(data);
}

static void test_show(void *data)
{
    struct test_source *ctx = (struct test_source *)data;
    blog(LOG_INFO, "[test_source] show");
    ctx->state         = ENTERING;
    ctx->current_frame = 0;
}

static void test_hide(void *data)
{
    struct test_source *ctx = (struct test_source *)data;
    blog(LOG_INFO, "[test_source] hide: state=%d", ctx->state);
    // Called by OBS after the transition completes.
    // Animation is already done via tick_exit by this point.
    ctx->state = HIDDEN;
}

static void test_video_tick(void *data, float seconds)
{
    UNUSED_PARAMETER(seconds);
    struct test_source *ctx = (struct test_source *)data;

    switch (ctx->state) {
        case ENTERING:
            ctx->current_frame++;
            if (ctx->current_frame >= ctx->hold_start_frame) {
                blog(LOG_INFO, "[test_source] tick: ENTERING -> HOLDING");
                ctx->state = HOLDING;
                // Pre-attach the hide transition now so it is already
                // on the item when the user hides it.
                try_attach_hide_transition(ctx);
            }
            break;
        case HOLDING:
            ctx->current_frame = ctx->hold_start_frame;
            break;
        case EXITING:
            // Driven by passthrough_callback -> test_source_tick_exit
            break;
        case HIDDEN:
            break;
    }
}

static void test_render(void *data, gs_effect_t *effect)
{
    UNUSED_PARAMETER(effect);
    struct test_source *ctx = (struct test_source *)data;

    uint32_t color;
    switch (ctx->state) {
        case ENTERING: color = 0xFFFF0000; break;
        case HOLDING:  color = 0xFF00FF00; break;
        case EXITING:  color = 0xFFFFFF00; break;
        default:       color = 0xFF000000; break;
    }

    gs_effect_t *solid       = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");
    gs_effect_set_color(color_param, color);

    while (gs_effect_loop(solid, "Solid"))
        gs_draw_sprite(NULL, 0, 1920, 1080);
}

static uint32_t test_get_width(void *data)
{
    UNUSED_PARAMETER(data);
    return 1920;
}

static uint32_t test_get_height(void *data)
{
    UNUSED_PARAMETER(data);
    return 1080;
}

static struct obs_source_info test_source_info = {
    .id           = "test_lower_third",
    .type         = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
    .get_name     = test_get_name,
    .create       = test_create,
    .destroy      = test_destroy,
    .get_width    = test_get_width,
    .get_height   = test_get_height,
    .show         = test_show,
    .hide         = test_hide,
    .video_tick   = test_video_tick,
    .video_render = test_render,
};

// ── Registration ─────────────────────────────────────────

void register_test_source(void)
{
    obs_register_source(&passthrough_transition_info);
    obs_register_source(&test_source_info);
}

void unregister_test_source(void)
{
}
