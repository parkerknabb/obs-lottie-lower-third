#include <obs-module.h>
#include <plugin-support.h>

// Data structure for each source instance
struct test_source {
    obs_source_t *source;
    obs_sceneitem_t *exit_item;
    enum { HIDDEN, ENTERING, HOLDING, EXITING } state;
    uint32_t current_frame;
    uint32_t hold_start_frame;
    uint32_t exit_start_frame;
    uint32_t end_frame;
    bool is_visible;
    bool is_rendering_exit;
    bool restoring_visibility;
    bool completing_hide;
};

static void scene_item_visible(void *data, calldata_t *params);
static void global_source_create(void *data, calldata_t *params);

static void restore_exit_item(void *data)
{
    struct test_source *ctx = (struct test_source *)data;
    if (!ctx->exit_item || ctx->completing_hide) {
        return;
    }

    ctx->restoring_visibility = true;
    obs_sceneitem_set_visible(ctx->exit_item, true);
    ctx->restoring_visibility = false;
}

static void connect_scene_signals(struct test_source *ctx, obs_source_t *scene_source)
{
    if (!obs_scene_from_source(scene_source) && !obs_group_from_source(scene_source)) {
        return;
    }

    signal_handler_t *handler = obs_source_get_signal_handler(scene_source);
    signal_handler_connect(handler, "item_visible", scene_item_visible, ctx);
}

static void disconnect_scene_signals(struct test_source *ctx, obs_source_t *scene_source)
{
    if (!obs_scene_from_source(scene_source) && !obs_group_from_source(scene_source)) {
        return;
    }

    signal_handler_t *handler = obs_source_get_signal_handler(scene_source);
    signal_handler_disconnect(handler, "item_visible", scene_item_visible, ctx);
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

// Required callbacks
static const char *test_get_name(void *type_data)
{
    UNUSED_PARAMETER(type_data);
    return "Test Lower Third";
}

static void global_source_create(void *data, calldata_t *params)
{
    struct test_source *ctx = (struct test_source *)data;
    obs_source_t *source = calldata_ptr(params, "source");
    if (source) {
        connect_scene_signals(ctx, source);
    }
}

static void scene_item_visible(void *data, calldata_t *params)
{
    struct test_source *ctx = (struct test_source *)data;
    obs_sceneitem_t *item = calldata_ptr(params, "item");
    bool visible = calldata_bool(params, "visible");

    if (!item || visible || ctx->restoring_visibility || ctx->completing_hide) {
        return;
    }

    if (obs_sceneitem_get_source(item) != ctx->source) {
        return;
    }

    if (ctx->exit_item) {
        obs_sceneitem_release(ctx->exit_item);
    }

    ctx->exit_item = item;
    obs_sceneitem_addref(ctx->exit_item);
    ctx->state = EXITING;
    ctx->current_frame = ctx->exit_start_frame;
    ctx->is_rendering_exit = true;
    ctx->is_visible = true;

    obs_queue_task(OBS_TASK_UI, restore_exit_item, ctx, false);
}

static void *test_create(obs_data_t *settings, obs_source_t *source)
{
    UNUSED_PARAMETER(settings);
    struct test_source *ctx = bzalloc(sizeof(struct test_source));
    ctx->source = source;
    ctx->state = HIDDEN;
    ctx->hold_start_frame = 30;
    ctx->exit_start_frame = 90;
    ctx->end_frame = 120;

    obs_enum_scenes(connect_scene_enum, ctx);
    signal_handler_connect(obs_get_signal_handler(), "source_create", global_source_create, ctx);

    return ctx;
}

static void test_destroy(void *data)
{
    struct test_source *ctx = (struct test_source *)data;

    signal_handler_disconnect(obs_get_signal_handler(), "source_create", global_source_create, ctx);
    obs_enum_scenes(disconnect_scene_enum, ctx);

    if (ctx->exit_item) {
        obs_sceneitem_release(ctx->exit_item);
    }

    bfree(data);
}

static void test_show(void *data)
{
    struct test_source *ctx = (struct test_source *)data;
    if (ctx->restoring_visibility) {
        return;
    }

    if (ctx->exit_item) {
        obs_sceneitem_release(ctx->exit_item);
        ctx->exit_item = NULL;
    }

    ctx->state = ENTERING;
    ctx->current_frame = 0;
    ctx->is_visible = true;
    ctx->is_rendering_exit = false;
}

static void test_hide(void *data)
{
    struct test_source *ctx = (struct test_source *)data;
    if (ctx->completing_hide) {
        ctx->state = HIDDEN;
        ctx->is_rendering_exit = false;
        ctx->is_visible = false;
        return;
    }

    if (ctx->is_rendering_exit) {
        return;
    }

    ctx->state = HIDDEN;
    ctx->is_visible = false;
}

static void test_render(void *data, gs_effect_t *effect)
{
    struct test_source *ctx = (struct test_source *)data;
    
    UNUSED_PARAMETER(effect);
    
    // Advance frame based on state
    switch (ctx->state) {
        case ENTERING:
            ctx->current_frame++;
            if (ctx->current_frame >= ctx->hold_start_frame) {
                ctx->state = HOLDING;
            }
            break;
        case HOLDING:
            ctx->current_frame = ctx->hold_start_frame;
            break;
        case EXITING:
            ctx->current_frame++;
            if (ctx->current_frame >= ctx->end_frame) {
                ctx->state = HIDDEN;
                ctx->is_rendering_exit = false;
                ctx->is_visible = false;

                if (ctx->exit_item) {
                    ctx->completing_hide = true;
                    obs_sceneitem_set_visible(ctx->exit_item, false);
                    ctx->completing_hide = false;
                    obs_sceneitem_release(ctx->exit_item);
                    ctx->exit_item = NULL;
                }

                return;
            }
            break;
        case HIDDEN:
            break;
    }
    
    // Get color based on state
    uint32_t color;
    switch (ctx->state) {
        case ENTERING: color = 0xFFFF0000; break;  // Red
        case HOLDING:  color = 0xFF00FF00; break;  // Green
        case EXITING:  color = 0xFFFFFF00; break;  // Yellow
        case HIDDEN:   color = 0xFF000000; break;  // Black
    }
    
    // Use OBS built-in solid color effect
    gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");
    gs_effect_set_color(color_param, color);
    
    while (gs_effect_loop(solid, "Solid")) {
        gs_draw_sprite(NULL, 0, 1920, 1080);
    }
}

static uint32_t test_get_width(void *data)
{
    UNUSED_PARAMETER(data);
    return 1920;  // Fixed width for test
}

static uint32_t test_get_height(void *data)
{
    UNUSED_PARAMETER(data);
    return 1080;  // Fixed height for test
}

// Register the source type
struct obs_source_info test_source_info = {
    .id = "test_lower_third",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
    .get_name = test_get_name,
    .create = test_create,
    .destroy = test_destroy,
    .get_width = test_get_width,
    .get_height = test_get_height,
    .show = test_show,
    .hide = test_hide,
    .video_render = test_render,
};

// Called from plugin-main.c
void register_test_source(void)
{
    obs_register_source(&test_source_info);
}

void unregister_test_source(void)
{
    // Optional: cleanup if needed
}
