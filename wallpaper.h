#ifndef WALLPAPER_H
#define WALLPAPER_H

#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_scene.h>

struct wlr_scene_node *wallpaper_create(struct wlr_scene_tree *parent,
                                        struct wlr_renderer *renderer,
                                        const char *path);
void wallpaper_resize(struct wlr_scene_node *node, int width, int height);

#endif
