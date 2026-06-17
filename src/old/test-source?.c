#include <obs-module.h>
#include <plugin-support.h>
#include <thorvg_capi.h>

// ─────────────────────────────────────────────────────────
// Passthrough hide transition
// ─────────────────────────────────────────────────────────

// Hardcode a path for Milestone 1; make it a property later
#define LOTTIE_TEST_PATH "/Users/pknabb/Documents/GitHub/test.json"

struct test_source; // forward declaration
static void test_source_tick_exit(struct test_source *ctx,
                                  uint32_t cx, uint32_t cy,
                                  gs_texture_t *a);
static void test_source_begin_exit(struct test_source *ctx);

struct passthrough_transition {
    obs_source_t       *source;
    struct test_source *owner;
    uint32_t            frame_count; // per-instance instead of static
};

static const char *passthrough_get_name(void *type_data)
{
    UNUSED_PARAMETER(type_data);
    return "Lower Third Passthrough";
}

static void *passthrough_create(obs_data_t *settings, obs_source_t *source)
{
    UNUSED_PARAMETER(settings);
    blog(LOG_INFO, "[passthrough] create: source=%p", (void *)source);
    struct passthrough_transition *ctx =
        bzalloc(sizeof(struct passthrough_transition));
    ctx->source      = source;
    ctx->owner       = NULL;
    ctx->frame_count = 0;
    return ctx;
}

static void passthrough_destroy(void *data)
{
    blog(LOG_INFO, "[passthrough] destroy");
    bfree(data);
}

static void passthrough_start(void *data)
{
    struct passthrough_transition *ctx =
        (struct passthrough_transition *)data;
    blog(LOG_INFO, "[passthrough] transition_start");

    // Signal the owner source to begin its exit animation
    if (ctx->owner)
        test_source_begin_exit(ctx->owner);
}

static void passthrough_stop(void *data)
{
    struct passthrough_transition *ctx =
        (struct passthrough_transition *)data;
    blog(LOG_INFO, "[passthrough] transition_stop");
    UNUSED_PARAMETER(ctx);
}

static void passthrough_callback(void *data, gs_texture_t *a,
                                 gs_texture_t *b, float t,
                                 uint32_t cx, uint32_t cy)
{
    UNUSED_PARAMETER(b);
    struct passthrough_transition *ctx =
        (struct passthrough_transition *)data;

    ctx->frame_count++;
    if (ctx->frame_count % 30 == 0) {
        blog(LOG_INFO,
             "[passthrough] callback frame=%u t=%.3f a=%p cx=%u cy=%u",
             ctx->frame_count, t, (void *)a, cx, cy);
    }

    if (!a) {
        blog(LOG_WARNING, "[passthrough] callback: texture A is NULL");
        return;
    }

    if (ctx->owner) {
        test_source_tick_exit(ctx->owner, cx, cy, a);
        return;
    }

    // Fallback: just draw the texture directly
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
    .id               = "test_passthrough_transition",
    .type             = OBS_SOURCE_TYPE_TRANSITION,
    .output_flags     = OBS_SOURCE_VIDEO,
    .get_name         = passthrough_get_name,
    .create           = passthrough_create,
    .destroy          = passthrough_destroy,
    .video_render     = passthrough_render,
    .audio_render     = passthrough_audio_render,
    .get_width        = passthrough_get_width,
    .get_height       = passthrough_get_height,
    .transition_start = passthrough_start,
    .transition_stop  = passthrough_stop,
};

// ─────────────────────────────────────────────────────────
// Test lower third source
// ─────────────────────────────────────────────────────────

enum test_state {
    STATE_HIDDEN,
    STATE_ENTERING,
    STATE_HOLDING,
    STATE_EXITING,
};

struct test_source {
    obs_source_t *source;
    obs_source_t *hide_transition;
    enum test_state state;
    uint32_t current_frame;
    uint32_t hold_start_frame;
    uint32_t exit_start_frame;
    uint32_t end_frame;
    
    // ── ThorVG (all touched on graphics thread only) ──
    Tvg_Animation   anim;
    Tvg_Paint       pic;
    uint32_t       *buf;          // ARGB8888 CPU buffer
    gs_texture_t   *texture;      // GPU texture
    uint32_t        comp_w, comp_h;
    bool            lottie_loaded;
};

static void scene_item_visible(void *data, calldata_t *params);
static void global_source_create(void *data, calldata_t *params);

// ── Scene item search ─────────────────────────────────────

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
        return false;
    }
    return true;
}

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
        return false;
    }
    return true;
}

// ── Transition attachment ─────────────────────────────────

static uint32_t get_exit_duration_ms(struct test_source *ctx)
{
    struct obs_video_info ovi;
    if (!obs_get_video_info(&ovi) || ovi.fps_den == 0) {
        blog(LOG_WARNING,
             "[test_source] get_exit_duration_ms: no video info, using 1000ms");
        return 1000;
    }
    uint32_t exit_frames = ctx->end_frame - ctx->exit_start_frame;
    uint32_t ms = (uint32_t)((double)exit_frames * ovi.fps_den /
                             ovi.fps_num * 1000.0);
    blog(LOG_INFO,
         "[test_source] exit duration: %u frames @ %u/%u fps = %u ms",
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

    // FIX: use obs_obj_get_data to get the INSTANCE data returned by
    // passthrough_create, not obs_source_get_type_data which returns
    // the static type_data field from obs_source_info (always NULL here).
    struct passthrough_transition *td =
        (struct passthrough_transition *)obs_obj_get_data(ctx->hide_transition);
    if (td) {
        td->owner = ctx;
        blog(LOG_INFO, "[test_source] attach: owner back-pointer set");
    } else {
        blog(LOG_ERROR,
             "[test_source] attach: could not get transition instance data");
    }

    uint32_t duration_ms = get_exit_duration_ms(ctx);

    obs_sceneitem_set_transition(item, false, ctx->hide_transition);
    obs_sceneitem_set_transition_duration(item, false, duration_ms);

    obs_source_t *check     = obs_sceneitem_get_transition(item, false);
    uint32_t      check_dur = obs_sceneitem_get_transition_duration(item, false);
    blog(LOG_INFO,
         "[test_source] attach: set=%p dur=%ums | readback=%p dur=%ums",
         (void *)ctx->hide_transition, duration_ms,
         (void *)check, check_dur);

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
        blog(LOG_INFO,
             "[test_source] tick: found item %p, pre-attaching hide transition",
             (void *)fd.result);
        attach_hide_transition(ctx, fd.result);
    } else {
        blog(LOG_WARNING,
             "[test_source] tick: source not found in any scene");
    }
}

// ── Exit animation ────────────────────────────────────────

// Called by the transition's transition_start callback
static void test_source_begin_exit(struct test_source *ctx)
{
    if (ctx->state == STATE_HOLDING || ctx->state == STATE_ENTERING) {
        blog(LOG_INFO, "[test_source] begin_exit: entering EXITING state");
        ctx->state         = STATE_EXITING;
        ctx->current_frame = ctx->exit_start_frame;
    }
}

// Called from passthrough_callback each frame during the hide transition
static void test_source_tick_exit(struct test_source *ctx,
                                  uint32_t cx, uint32_t cy,
                                  gs_texture_t *a)
{
    UNUSED_PARAMETER(a);

    if (ctx->state == STATE_EXITING) {
        ctx->current_frame++;
        if (ctx->current_frame % 10 == 0) {
            blog(LOG_INFO, "[test_source] tick_exit: frame=%u / %u",
                 ctx->current_frame, ctx->end_frame);
        }
        if (ctx->current_frame >= ctx->end_frame) {
            blog(LOG_INFO, "[test_source] tick_exit: animation complete");
            ctx->state = STATE_HIDDEN;
        }
    }

    uint32_t color;
    switch (ctx->state) {
    case STATE_ENTERING: color = 0xFFFF0000; break; // red
    case STATE_HOLDING:  color = 0xFF00FF00; break; // green
    case STATE_EXITING:  color = 0xFFFFFF00; break; // yellow
    default:             color = 0x00000000; break; // transparent
    }

    gs_effect_t *solid       = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");
    gs_effect_set_color(color_param, color);

    while (gs_effect_loop(solid, "Solid"))
        gs_draw_sprite(NULL, 0, cx, cy);
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
    blog(LOG_INFO, "[test_source] create: source=%p", (void *)source);
    struct test_source *ctx = bzalloc(sizeof(struct test_source));
    ctx->source             = source;
    ctx->state              = STATE_HIDDEN;
    ctx->hold_start_frame   = 30;
    ctx->exit_start_frame   = 90;
    ctx->end_frame          = 120;

    obs_enum_scenes(connect_scene_enum, ctx);
    signal_handler_connect(obs_get_signal_handler(), "source_create",
                           global_source_create, ctx);
    
    // ── ThorVG init (engine init is global; safe to call per-instance,
    //    but ideally do it once in obs_module_load — see note below) ──
    if (tvg_engine_init(TVG_ENGINE_OPTION_DEFAULT) != TVG_RESULT_SUCCESS)
        blog(LOG_ERROR, "[test_source] tvg_engine_init failed");

    ctx->anim = tvg_lottie_animation_new();
    ctx->pic  = tvg_animation_get_picture(ctx->anim);

    if (tvg_picture_load(ctx->pic, LOTTIE_TEST_PATH) != TVG_RESULT_SUCCESS) {
        blog(LOG_ERROR, "[test_source] failed to load Lottie: %s",
             LOTTIE_TEST_PATH);
    } else {
        float pw = 0, ph = 0;
        tvg_picture_get_size(ctx->pic, &pw, &ph);
        ctx->comp_w = (uint32_t)pw;
        ctx->comp_h = (uint32_t)ph;
        tvg_picture_set_size(ctx->pic, pw, ph);   // native, no distortion
        ctx->buf = bzalloc(ctx->comp_w * ctx->comp_h * sizeof(uint32_t));
        ctx->lottie_loaded = true;
        blog(LOG_INFO, "[test_source] Lottie loaded: %ux%u",
             ctx->comp_w, ctx->comp_h);
    }
    
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

    if (ctx->texture) {
            obs_enter_graphics();
            gs_texture_destroy(ctx->texture);
            obs_leave_graphics();
    }
    if (ctx->anim)
        tvg_animation_del(ctx->anim);   // also frees the picture
    if (ctx->buf)
        bfree(ctx->buf);
    
    bfree(data);
}

static void test_show(void *data)
{
    struct test_source *ctx = (struct test_source *)data;
    blog(LOG_INFO, "[test_source] show");
    ctx->state         = STATE_ENTERING;
    ctx->current_frame = 0;
}

static void test_hide(void *data)
{
    struct test_source *ctx = (struct test_source *)data;
    blog(LOG_INFO, "[test_source] hide: state=%d", ctx->state);
    // Called by OBS after the hide transition completes.
    // The exit animation is already done via tick_exit by this point.
    ctx->state = STATE_HIDDEN;
}

static void test_video_tick(void *data, float seconds)
{
    UNUSED_PARAMETER(seconds);
    struct test_source *ctx = (struct test_source *)data;

    switch (ctx->state) {
    case STATE_ENTERING:
        ctx->current_frame++;
        if (ctx->current_frame >= ctx->hold_start_frame) {
            blog(LOG_INFO, "[test_source] tick: ENTERING -> HOLDING");
            ctx->state = STATE_HOLDING;
            try_attach_hide_transition(ctx);
        }
        break;
    case STATE_HOLDING:
        // Hold at fixed frame; exit is driven by transition callback
        break;
    case STATE_EXITING:
        // Driven by passthrough_callback -> test_source_tick_exit
        break;
    case STATE_HIDDEN:
        break;
    }
}

static void test_render(void *data, gs_effect_t *effect)
{
    UNUSED_PARAMETER(effect);
    struct test_source *ctx = (struct test_source *)data;

    if (!ctx->lottie_loaded || !ctx->buf)
        return;

    // ── Render one fixed frame (hold_start = 35 in your test template) ──
    // Milestone 1: ignore the state machine, always show the hold frame.
    float frame_no = 35.0f;

    // Fresh per-frame canvas keeps all ThorVG state on the graphics thread.
    // (Reusing a canvas is a later optimization; correctness first.)
    Tvg_Canvas canvas = tvg_swcanvas_create(TVG_ENGINE_OPTION_DEFAULT);
    tvg_swcanvas_set_target(canvas, ctx->buf, ctx->comp_w,
                            ctx->comp_w, ctx->comp_h,
                            TVG_COLORSPACE_ARGB8888);
    tvg_canvas_add(canvas, ctx->pic);

    tvg_animation_set_frame(ctx->anim, frame_no);
    tvg_canvas_update(canvas);
    tvg_canvas_draw(canvas, true);
    tvg_canvas_sync(canvas);

    // IMPORTANT: remove pic from this throwaway canvas WITHOUT destroying it,
    // so the picture survives for the next frame. tvg_canvas_destroy would
    // free the paint. Use tvg_canvas_remove(canvas, paint) then destroy.
    tvg_canvas_remove(canvas, ctx->pic);
    tvg_canvas_destroy(canvas);

    // ── Upload CPU buffer to GPU texture ──
    if (!ctx->texture) {
        ctx->texture = gs_texture_create(
            ctx->comp_w, ctx->comp_h, GS_BGRA, 1,
            (const uint8_t **)&ctx->buf, GS_DYNAMIC);
    } else {
        gs_texture_set_image(ctx->texture,
                             (const uint8_t *)ctx->buf,
                             ctx->comp_w * 4, false);
    }

    // ── Draw the texture ──
    gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_eparam_t *img = gs_effect_get_param_by_name(def, "image");
    gs_effect_set_texture(img, ctx->texture);
    while (gs_effect_loop(def, "Draw"))
        gs_draw_sprite(ctx->texture, 0, ctx->comp_w, ctx->comp_h);
}
static uint32_t test_get_width(void *data)
{
    struct test_source *ctx = (struct test_source *)data;
    return ctx->lottie_loaded ? ctx->comp_w : 1920;
}

static uint32_t test_get_height(void *data)
{
    struct test_source *ctx = (struct test_source *)data;
    return ctx->lottie_loaded ? ctx->comp_h : 1920;
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
