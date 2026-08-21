#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

#include <drm_fourcc.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

#include "util.h"
#include "wallpaper.h"

struct wallpaper_buffer {
  struct wlr_buffer base;
  int width, height;
  uint32_t format;
  size_t stride;
  void *data;
};

static void buffer_destroy(struct wlr_buffer *wlr_buffer) {
  struct wallpaper_buffer *buf = wl_container_of(wlr_buffer, buf, base);
  if (buf->data) {
    stbi_image_free(buf->data);
  }
  free(buf);
}

static bool buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
                                         uint32_t flags, void **data,
                                         uint32_t *format, size_t *stride) {
  struct wallpaper_buffer *buf = wl_container_of(wlr_buffer, buf, base);

  if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE) {
    return false;
  }

  *data = buf->data;
  *format = buf->format;
  *stride = buf->stride;
  return true;
}

static void buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {}

static const struct wlr_buffer_impl buffer_impl = {
    .destroy = buffer_destroy,
    .begin_data_ptr_access = buffer_begin_data_ptr_access,
    .end_data_ptr_access = buffer_end_data_ptr_access,
};

struct wlr_scene_node *wallpaper_create(struct wlr_scene_tree *parent,
                                        struct wlr_renderer *renderer,
                                        const char *path) {
  (void)renderer;

  int w, h, chans;
  uint8_t *pixels = stbi_load(path, &w, &h, &chans, 4);
  if (!pixels) {
    die("wallpaper: no pude cargar %s", path);
  }

  struct wallpaper_buffer *buf = ecalloc(1, sizeof(*buf));
  wlr_buffer_init(&buf->base, &buffer_impl, w, h);
  buf->width = w;
  buf->height = h;
  buf->format = DRM_FORMAT_ABGR8888;
  buf->stride = w * 4;
  buf->data = pixels;

  struct wlr_scene_buffer *scene_buf =
      wlr_scene_buffer_create(parent, &buf->base);

  wlr_buffer_drop(&buf->base);

  if (!scene_buf) {
    die("wallpaper: error al crear scene buffer");
  }

  return &scene_buf->node;
}

void wallpaper_resize(struct wlr_scene_node *node, int width, int height) {
  if (!node)
    return;
  struct wlr_scene_buffer *scene_buf = wlr_scene_buffer_from_node(node);
  wlr_scene_buffer_set_dest_size(scene_buf, width, height);
}
