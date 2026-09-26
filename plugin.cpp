#define private public
#include <wayfire/render.hpp>
#undef private

#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/scene-render.hpp>
extern "C" {
    #include <wlr/types/wlr_ext_image_copy_capture_v1.h>
    #include <wlr/types/wlr_ext_image_capture_source_v1.h>
    #include <wlr/interfaces/wlr_ext_image_capture_source_v1.h>
    #include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
    #include <wlr/render/allocator.h>
    #include <drm_fourcc.h>
    #include <sys/stat.h>
}

struct toplevel_capture_source {
    wlr_ext_image_capture_source_v1 base;

    wl_listener destroy_listener;
    wl_listener toplevel_listener;
    wl_listener client_listener;

    wf::scene::floating_inner_ptr root_node;
    std::vector<wf::scene::render_instance_uptr> instances;
    
    wf::render_pass_params_t params;

    wf::auxilliary_buffer_t *shm_buffer = nullptr;

    pixman_region32 damage;
    wlr_ext_image_capture_source_v1_frame_event event;
    
    ~toplevel_capture_source() {
        pixman_region32_fini(&damage); delete shm_buffer;
    }
};

static void finish(void *data) { delete static_cast<toplevel_capture_source *>(data); }
static void handle_destroy(wl_listener *destroy_listener, void *data) {
    auto src = reinterpret_cast<toplevel_capture_source *>(reinterpret_cast<uint8_t *>(destroy_listener) - offsetof(toplevel_capture_source, destroy_listener));

    wl_list_remove(&src->destroy_listener.link); wl_list_remove(&src->toplevel_listener.link); wl_list_remove(&src->client_listener.link);
    
    wl_event_loop_add_idle(wl_display_get_event_loop(wf::get_core().display), finish, src);
}
static void handle_toplevel(wl_listener *toplevel_listener, void *data) {
    auto src = reinterpret_cast<toplevel_capture_source *>(reinterpret_cast<uint8_t *>(toplevel_listener) - offsetof(toplevel_capture_source, toplevel_listener));
    wlr_ext_image_capture_source_v1_finish(&src->base);
}
static void handle_client(wl_listener *client_listener, void *data) {
    auto src = reinterpret_cast<toplevel_capture_source *>(reinterpret_cast<uint8_t *>(client_listener) - offsetof(toplevel_capture_source, client_listener));
    wlr_ext_image_capture_source_v1_finish(&src->base);
}

static void toplevel_capture_source_request_frame(wlr_ext_image_capture_source_v1 *source, bool schedule_frame) {
    auto src = reinterpret_cast<toplevel_capture_source *>(source);

    wl_signal_emit(&source->events.frame, &src->event);
}
static void toplevel_capture_source_copy_frame(
    wlr_ext_image_capture_source_v1 *source,
    wlr_ext_image_copy_capture_frame_v1 *dst_frame,
    wlr_ext_image_capture_source_v1_frame_event *event
) {
    auto src = reinterpret_cast<toplevel_capture_source *>(source);

    if (not src->shm_buffer) {
        struct wlr_dmabuf_attributes dmabuf;
        if (wlr_buffer_get_dmabuf(dst_frame->buffer, &dmabuf)) {
            if (src->base.width != dst_frame->buffer->width or src->base.height != dst_frame->buffer->height) {
            	wlr_ext_image_copy_capture_frame_v1_fail(dst_frame, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS);
            	return;
            }

            src->params.target.buffer = dst_frame->buffer;
        } else {
            src->shm_buffer = new wf::auxilliary_buffer_t;
            src->shm_buffer->allocate(
                {static_cast<int32_t>(src->base.width), static_cast<int32_t>(src->base.height)}, 1, {false, false}
            );
            src->params.target = wf::render_target_t(*src->shm_buffer);
        }
    }

    auto bbox = src->root_node->get_bounding_box();
    src->params.target.scale = std::min(src->base.width / bbox.width, src->base.height / bbox.height);
    auto w = src->base.width / src->params.target.scale; auto h = src->base.height / src->params.target.scale;
    bbox.x -= std::floor(w / 2 - bbox.width / 2); bbox.y -= std::floor(h / 2 - bbox.height / 2); src->params.target.geometry = bbox;
    bbox.width = std::floor(w); bbox.height = std::floor(h); src->params.damage = bbox;

    wf::render_pass_t::run(src->params);

    if (src->shm_buffer) {
        if (
            not wlr_ext_image_copy_capture_frame_v1_copy_buffer(dst_frame, src->shm_buffer->get_buffer(), wf::get_core().renderer)
        ) { return; }
    }

    timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_ext_image_copy_capture_frame_v1_ready(dst_frame, WL_OUTPUT_TRANSFORM_NORMAL, &now);
}
static void toplevel_capture_source_start(wlr_ext_image_capture_source_v1 *source, bool with_cursors) {}
static void toplevel_capture_source_stop(wlr_ext_image_capture_source_v1 *source) {}
static wlr_ext_image_capture_source_v1_cursor *toplevel_capture_source_get_pointer_cursor(wlr_ext_image_capture_source_v1 *source, wlr_seat *seat) { return nullptr; }
static const wlr_ext_image_capture_source_v1_interface toplevel_capture_source_impl = {
    .start = toplevel_capture_source_start,
    .stop = toplevel_capture_source_stop,
    .request_frame = toplevel_capture_source_request_frame,
    .copy_frame = toplevel_capture_source_copy_frame,
    .get_pointer_cursor = toplevel_capture_source_get_pointer_cursor,
};

static wlr_ext_foreign_toplevel_image_capture_source_manager_v1 *manager = nullptr;
static wl_listener *request_listener = nullptr;

static void handle_request(wl_listener *request_listener, void *data) {
    auto request = static_cast<wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request *>(data);

    toplevel_capture_source *src = new toplevel_capture_source;
    wlr_ext_image_capture_source_v1_init(&src->base, &toplevel_capture_source_impl);

    auto view = static_cast<wf::view_interface_t *>(request->toplevel_handle->data);
    src->root_node = view->get_surface_root_node();
    src->root_node->gen_render_instances(src->instances, [] (auto) {});
    auto bbox = src->root_node->get_bounding_box();

    src->params.instances = &src->instances;
    src->params.flags = wf::RPASS_CLEAR_BACKGROUND; src->params.background_color = {0, 0, 0, 0};
    
    auto buffer = wf::render_buffer_t(nullptr, {static_cast<int32_t>(bbox.width), static_cast<int32_t>(bbox.height)});
    src->params.target = wf::render_target_t(buffer);

    pixman_region32_init_rect(&src->damage, 0, 0, bbox.width, bbox.height);
    src->event = { .damage = &src->damage };

    src->base.width = bbox.width; src->base.height = bbox.height;

    src->base.shm_formats_len = 1;
    src->base.shm_formats = static_cast<uint32_t *>(malloc(sizeof(uint32_t)));
    src->base.shm_formats[0] = DRM_FORMAT_XRGB8888;
    if (wf::get_core().allocator->buffer_caps & WLR_BUFFER_CAP_DMABUF) {
        auto fmts = wlr_renderer_get_texture_formats(wf::get_core().renderer, WLR_BUFFER_CAP_DMABUF);
        auto format = wlr_drm_format_set_get(fmts, DRM_FORMAT_XRGB8888);
        if (format) {
            auto drm_fd = wlr_renderer_get_drm_fd(wf::get_core().renderer);
            if (drm_fd >= 0) {
                struct stat dev_stat;
                if (not fstat(drm_fd, &dev_stat)) {
                    src->base.dmabuf_device = dev_stat.st_rdev;
                    for (uint8_t i = 0; i < format->len; i++) {
                        wlr_drm_format_set_add(&src->base.dmabuf_formats, DRM_FORMAT_XRGB8888, format->modifiers[i]);
                    }
                }
            }
        }
    }

    wl_signal_emit_mutable(&src->base.events.constraints_update, nullptr);

    src->destroy_listener.notify = handle_destroy;
    wl_signal_add(&src->base.events.destroy, &src->destroy_listener);
    src->toplevel_listener.notify = handle_toplevel;
    wl_signal_add(&request->toplevel_handle->events.destroy, &src->toplevel_listener);
    src->client_listener.notify = handle_client;
    wl_client_add_destroy_listener(request->client, &src->client_listener);

    wlr_ext_image_capture_source_v1_create_resource(&src->base, request->client, request->WLR_PRIVATE.new_id);
    wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(request, &src->base);
}

class toplevel_capture : public wf::plugin_interface_t {
    public:
    void init() override {
        if (not manager) {
            manager = wlr_ext_foreign_toplevel_image_capture_source_manager_v1_create(wf::get_core().display, 1);
            request_listener = new wl_listener;

            request_listener->notify = handle_request;
            wl_signal_add(&manager->events.new_request, request_listener);
        }
    }
    bool is_unloadable() override {
        return false;
    }
};

DECLARE_WAYFIRE_PLUGIN(toplevel_capture)
