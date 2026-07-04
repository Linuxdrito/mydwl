static inline struct wlr_surface * client_surface(Client *c){
	return c->xdg->surface;
}

static inline Client * toplevel_from_wlr_surface(struct wlr_surface *s){
	struct wlr_xdg_surface *xdg_surface, *tmp_xdg_surface;
	struct wlr_surface *root_surface;

	if (!s)
		return NULL;
	
	root_surface = wlr_surface_get_root_surface(s);
	xdg_surface = wlr_xdg_surface_try_from_wlr_surface(root_surface);
	
	while (xdg_surface) {
		tmp_xdg_surface = NULL;
		switch (xdg_surface->role) {
		case WLR_XDG_SURFACE_ROLE_POPUP:
			if (!xdg_surface->popup || !xdg_surface->popup->parent)
				return NULL;

			tmp_xdg_surface = wlr_xdg_surface_try_from_wlr_surface(xdg_surface->popup->parent);

			if (!tmp_xdg_surface)
				return toplevel_from_wlr_surface(xdg_surface->popup->parent);

			xdg_surface = tmp_xdg_surface;
			break;
		case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
			return xdg_surface->data;
		case WLR_XDG_SURFACE_ROLE_NONE:
			return NULL;
		}
	}

	return NULL;
}

static inline void client_activate_surface(struct wlr_surface *s, int activated){
	struct wlr_xdg_toplevel *toplevel;
	if ((toplevel = wlr_xdg_toplevel_try_from_wlr_surface(s)))
		wlr_xdg_toplevel_set_activated(toplevel, activated);
}

static inline uint32_t client_set_bounds(Client *c, int32_t width, int32_t height){
	if (wl_resource_get_version(c->xdg->toplevel->resource) >=
			XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION && width >= 0 && height >= 0
			&& (c->bounds.width != width || c->bounds.height != height)) {
		c->bounds.width = width;
		c->bounds.height = height;
		return wlr_xdg_toplevel_set_bounds(c->xdg->toplevel, width, height);
	}
	return 0;
}

static inline void client_get_clip(Client *c, struct wlr_box *clip){
	*clip = (struct wlr_box){
		.x = 0,
		.y = 0,
		.width = c->geom.width - c->bw,
		.height = c->geom.height - c->bw,
	};

	clip->x = c->xdg->geometry.x;
	clip->y = c->xdg->geometry.y;
}

static inline void client_get_geometry(Client *c, struct wlr_box *geom){
	*geom = c->xdg->geometry;
}

static inline Client * client_get_parent(Client *c){
	if (c->xdg->toplevel->parent)
		return toplevel_from_wlr_surface(c->xdg->toplevel->parent->base->surface);
	return NULL;
}

static inline int client_has_children(Client *c){
	return wl_list_length(&c->xdg->link) > 1;
}

static inline int client_is_rendered_on_mon(Client *c, Monitor *m){
	struct wlr_surface_output *s;
	int unused_lx, unused_ly;
	if (!wlr_scene_node_coords(&c->scene->node, &unused_lx, &unused_ly))
		return 0;
	wl_list_for_each(s, &client_surface(c)->current_outputs, link)
		if (s->output == m->wlr_output)
			return 1;
	return 0;
}

static inline int client_is_stopped(Client *c){
	int pid;
	siginfo_t in = {0};

	wl_client_get_credentials(c->xdg->client->client, &pid, NULL, NULL);
	if (waitid(P_PID, pid, &in, WNOHANG|WCONTINUED|WSTOPPED|WNOWAIT) < 0) {
		if (errno == ECHILD)
			return 1;
	} else if (in.si_pid) {
		if (in.si_code == CLD_STOPPED || in.si_code == CLD_TRAPPED)
			return 1;
		if (in.si_code == CLD_CONTINUED)
			return 0;
	}

	return 0;
}

static inline void client_notify_enter(struct wlr_surface *s, struct wlr_keyboard *kb){
	if (kb)
		wlr_seat_keyboard_notify_enter(seat, s, kb->keycodes,
				kb->num_keycodes, &kb->modifiers);
	else
		wlr_seat_keyboard_notify_enter(seat, s, NULL, 0, NULL);
}

static inline void client_send_close(Client *c){
	wlr_xdg_toplevel_send_close(c->xdg->toplevel);
}

static inline void client_set_border_color(Client *c, const float color[static 4]){
	int i;
	for (i = 0; i < 4; i++)
		wlr_scene_rect_set_color(c->border[i], color);
}

static inline void client_set_fullscreen(Client *c, int fullscreen){
	wlr_xdg_toplevel_set_fullscreen(c->xdg->toplevel, fullscreen);
}

static inline void client_set_scale(struct wlr_surface *s, float scale){
	wlr_surface_set_preferred_buffer_scale(s, (int32_t)ceilf(scale));
}

static inline uint32_t client_set_size(Client *c, uint32_t width, uint32_t height){
	if ((int32_t)width == c->xdg->toplevel->current.width
			&& (int32_t)height == c->xdg->toplevel->current.height)
		return 0;
	return wlr_xdg_toplevel_set_size(c->xdg->toplevel, (int32_t)width, (int32_t)height);
}

static inline void client_set_tiled(Client *c, uint32_t edges){
	if (wl_resource_get_version(c->xdg->toplevel->resource)
			>= XDG_TOPLEVEL_STATE_TILED_RIGHT_SINCE_VERSION) {
		wlr_xdg_toplevel_set_tiled(c->xdg->toplevel, edges);
	} else {
		wlr_xdg_toplevel_set_maximized(c->xdg->toplevel, edges != WLR_EDGE_NONE);
	}
}

static inline void client_set_suspended(Client *c, int suspended){
	wlr_xdg_toplevel_set_suspended(c->xdg->toplevel, suspended);
}

static inline int client_wants_fullscreen(Client *c){
	return c->xdg->toplevel->requested.fullscreen;
}
