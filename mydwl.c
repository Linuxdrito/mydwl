#include <getopt.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <xkbcommon/xkbcommon.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/prctl.h>

#include "util.h"

#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
#define VISIBLEON(C, M)         ((M) && (C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define END(A)                  ((A) + LENGTH(A))
#define TAGMASK                 ((1u << TAGCOUNT) - 1)
#define LISTEN(E, L, H)         wl_signal_add((E), ((L)->notify = (H), (L)))
#define LISTEN_STATIC(E, H)     do { struct wl_listener *_l = ecalloc(1, sizeof(*_l)); _l->notify = (H); wl_signal_add((E), _l); } while (0)

enum { CurNormal, CurPressed};
enum { LyrBg, LyrBottom, LyrTile, LyrFloat, LyrTop, LyrFS, LyrOverlay, LyrBlock, NUM_LAYERS };
enum { XDGShell, LayerShell };

typedef union {
	int i;
	uint32_t ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int mod;
	unsigned int button;
	void (*func)(const Arg *);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct {
	unsigned int type;

	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_rect *border[4];
	struct wlr_scene_tree *scene_surface;
	struct wl_list link;
	struct wl_list flink;
	struct wlr_box geom;
	struct wlr_box prev;
	struct wlr_box bounds;
	union {
		struct wlr_xdg_surface *xdg;
	} surface;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wl_listener commit;
	struct wl_listener map;
	struct wl_listener maximize;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener set_title;
	struct wl_listener fullscreen;
	struct wl_listener set_decoration_mode;
	struct wl_listener destroy_decoration;
	unsigned int bw;
	uint32_t tags;
	int isfloating, isurgent, isfullscreen;
	uint32_t resize;
} Client;

typedef struct {
	uint32_t mod;
	xkb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	struct wlr_keyboard_group *wlr_group;

	int nsyms;
	const xkb_keysym_t *keysyms;
	uint32_t mods;
	struct wl_event_source *key_repeat_source;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
} KeyboardGroup;

typedef struct {
	unsigned int type;

	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_tree *popups;
	struct wlr_scene_layer_surface_v1 *scene_layer;
	struct wl_list link;
	int mapped;
	struct wlr_layer_surface_v1 *layer_surface;

	struct wl_listener destroy;
	struct wl_listener unmap;
	struct wl_listener surface_commit;
} LayerSurface;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

struct Monitor {
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;
	struct wlr_scene_rect *fullscreen_bg;
	struct wl_listener frame;
	struct wl_listener destroy;
	struct wl_listener request_state;
	struct wlr_box m;
	struct wlr_box w;
	struct wl_list layers[4];
	const Layout *lt[2];
	unsigned int seltags;
	unsigned int sellt;
	uint32_t tagset[2];
	float mfact;
	int gamma_lut_changed;
	int nmaster;
	char ltsymbol[16];
};

typedef struct {
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
} PointerConstraint;

typedef struct {
	const char *id;
	const char *title;
	uint32_t tags;
	int isfloating;
} Rule;

/* function declarations */
static void applybounds(Client *c, struct wlr_box *bbox);
static void applyrules(Client *c);
static void arrange(Monitor *m);
static void arrangelayer(Monitor *m, struct wl_list *list,
		struct wlr_box *usable_area, int exclusive);
static void arrangelayers(Monitor *m);
static void axisnotify(struct wl_listener *listener, void *data);
static void buttonpress(struct wl_listener *listener, void *data);
static void cleanup(void);
static void cleanupmon(struct wl_listener *listener, void *data);
static void cleanuplisteners(void);
static void commitlayersurfacenotify(struct wl_listener *listener, void *data);
static void commitnotify(struct wl_listener *listener, void *data);
static void commitpopup(struct wl_listener *listener, void *data);
static void createdecoration(struct wl_listener *listener, void *data);
static void createkeyboard(struct wlr_keyboard *keyboard);
static KeyboardGroup *createkeyboardgroup(void);
static void createlayersurface(struct wl_listener *listener, void *data);
static void createmon(struct wl_listener *listener, void *data);
static void createnotify(struct wl_listener *listener, void *data);
static void createpointer(struct wlr_pointer *pointer);
static void createpointerconstraint(struct wl_listener *listener, void *data);
static void createpopup(struct wl_listener *listener, void *data);
static void cursorconstrain(struct wlr_pointer_constraint_v1 *constraint);
static void cursorframe(struct wl_listener *listener, void *data);
static void cursorwarptohint(void);
static void destroydecoration(struct wl_listener *listener, void *data);
static void destroydragicon(struct wl_listener *listener, void *data);
static void destroylayersurfacenotify(struct wl_listener *listener, void *data);
static void destroynotify(struct wl_listener *listener, void *data);
static void destroypointerconstraint(struct wl_listener *listener, void *data);
static void destroykeyboardgroup(struct wl_listener *listener, void *data);
static void focusclient(Client *c, int lift);
static void focusstack(const Arg *arg);
static Client *focustop(Monitor *m);
static void fullscreennotify(struct wl_listener *listener, void *data);
static void gpureset(struct wl_listener *listener, void *data);
static void handlesig(int signo);
static void inputdevice(struct wl_listener *listener, void *data);
static int keybinding(uint32_t mods, xkb_keysym_t sym);
static void keypress(struct wl_listener *listener, void *data);
static void keypressmod(struct wl_listener *listener, void *data);
static int keyrepeat(void *data);
static void killclient(const Arg *arg);
static void mapnotify(struct wl_listener *listener, void *data);
static void maximizenotify(struct wl_listener *listener, void *data);
static void motionabsolute(struct wl_listener *listener, void *data);
static void motionnotify(uint32_t time, struct wlr_input_device *device, double sx,
		double sy, double sx_unaccel, double sy_unaccel);
static void motionrelative(struct wl_listener *listener, void *data);
static void pointerfocus(Client *c, struct wlr_surface *surface,
		double sx, double sy, uint32_t time);
static void printstatus(void);
static void powermgrsetmode(struct wl_listener *listener, void *data);
static void quit(const Arg *arg);
static void rendermon(struct wl_listener *listener, void *data);
static void requestdecorationmode(struct wl_listener *listener, void *data);
static void requeststartdrag(struct wl_listener *listener, void *data);
static void requestmonstate(struct wl_listener *listener, void *data);
static void resize(Client *c, struct wlr_box geo, int interact);
static void run(char *startup_cmd);
static void setcursor(struct wl_listener *listener, void *data);
static void setcursorshape(struct wl_listener *listener, void *data);
static void setfloating(Client *c, int floating);
static void setfullscreen(Client *c, int fullscreen);
static void setpsel(struct wl_listener *listener, void *data);
static void setsel(struct wl_listener *listener, void *data);
static void setup(void);
static void spawn(const Arg *arg);
static void startdrag(struct wl_listener *listener, void *data);
static void tag(const Arg *arg);
static void tile(Monitor *m);
static void togglefullscreen(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unmaplayersurfacenotify(struct wl_listener *listener, void *data);
static void unmapnotify(struct wl_listener *listener, void *data);
static void updatemons(struct wl_listener *listener, void *data);
static void updatetitle(struct wl_listener *listener, void *data);
static void urgent(struct wl_listener *listener, void *data);
static void view(const Arg *arg);
static void xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny);

static pid_t child_pid = -1;
static void *exclusive_focus;
static struct wl_event_source *cursor_hide_timer;
static struct wl_display *dpy;
static struct wl_event_loop *event_loop;
static struct wlr_backend *backend;
static struct wlr_scene *scene;
static struct wlr_scene_tree *layers[NUM_LAYERS];
static struct wlr_scene_tree *drag_icon;
static const int layermap[] = { LyrBg, LyrBottom, LyrTop, LyrOverlay };
static struct wlr_renderer *drw;
static struct wlr_allocator *alloc;
static struct wlr_compositor *compositor;
static struct wlr_session *session;

static struct wlr_xdg_shell *xdg_shell;
static struct wlr_xdg_activation_v1 *activation;
static struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
static struct wl_list clients;
static struct wl_list fstack;
static struct wlr_layer_shell_v1 *layer_shell;
static struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr;

static struct wlr_pointer_constraints_v1 *pointer_constraints;
static struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
static struct wlr_pointer_constraint_v1 *active_constraint;

static struct wlr_cursor *cursor;
static struct wlr_xcursor_manager *cursor_mgr;

static struct wlr_scene_rect *root_bg;

static struct wlr_seat *seat;
static KeyboardGroup *kb_group;
static unsigned int cursor_mode;

static struct wlr_output_layout *output_layout;
static struct wlr_box sgeom;

static Monitor monitor;
static int mon_init = 0;
#define selmon (&monitor)

static int cursor_hidden = 0;
#define CURSOR_HIDE_TIMEOUT 1000

static struct wl_listener cursor_axis = {.notify = axisnotify};
static struct wl_listener cursor_button = {.notify = buttonpress};
static struct wl_listener cursor_frame = {.notify = cursorframe};
static struct wl_listener cursor_motion = {.notify = motionrelative};
static struct wl_listener cursor_motion_absolute = {.notify = motionabsolute};
static struct wl_listener gpu_reset = {.notify = gpureset};
static struct wl_listener layout_change = {.notify = updatemons};
static struct wl_listener new_input_device = {.notify = inputdevice};
static struct wl_listener new_pointer_constraint = {.notify = createpointerconstraint};
static struct wl_listener new_output = {.notify = createmon};
static struct wl_listener new_xdg_toplevel = {.notify = createnotify};
static struct wl_listener new_xdg_popup = {.notify = createpopup};
static struct wl_listener new_xdg_decoration = {.notify = createdecoration};
static struct wl_listener new_layer_surface = {.notify = createlayersurface};
static struct wl_listener output_power_mgr_set_mode = {.notify = powermgrsetmode};
static struct wl_listener request_activate = {.notify = urgent};
static struct wl_listener request_cursor = {.notify = setcursor};
static struct wl_listener request_set_psel = {.notify = setpsel};
static struct wl_listener request_set_sel = {.notify = setsel};
static struct wl_listener request_set_cursor_shape = {.notify = setcursorshape};
static struct wl_listener request_start_drag = {.notify = requeststartdrag};
static struct wl_listener start_drag = {.notify = startdrag};

#include "config.h"
#include "client.h"

void applybounds(Client *c, struct wlr_box *bbox) {
	c->geom.width = MAX(1 + 2 * (int)c->bw, c->geom.width);
	c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);

	if (c->geom.x >= bbox->x + bbox->width)
		c->geom.x = bbox->x + bbox->width - c->geom.width;
	if (c->geom.y >= bbox->y + bbox->height)
		c->geom.y = bbox->y + bbox->height - c->geom.height;
	if (c->geom.x + c->geom.width <= bbox->x)
		c->geom.x = bbox->x;
	if (c->geom.y + c->geom.height <= bbox->y)
		c->geom.y = bbox->y;
}

void applyrules(Client *c) {
	const char *appid = client_get_appid(c);
	const char *title = client_get_title(c);
	uint32_t newtags = 0;
	const Rule *r;

	for (r = rules; r < END(rules); r++) {
		if ((!r->title || strstr(title, r->title)) && (!r->id || strstr(appid, r->id))) {
			c->isfloating = r->isfloating;
			newtags |= r->tags;
		}
	}

	c->isfloating |= client_is_float_type(c);
	c->mon = &monitor;
	c->tags = newtags ? newtags : monitor.tagset[monitor.seltags];
	resize(c, c->geom, 0);
	setfullscreen(c, c->isfullscreen);
	setfloating(c, c->isfloating);
}

void arrange(Monitor *m) {
	Client *c;
	if (!m->wlr_output || !m->wlr_output->enabled) return;

	wl_list_for_each(c, &clients, link) {
		wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m));
		client_set_suspended(c, !VISIBLEON(c, m));
	}

	wlr_scene_node_set_enabled(&m->fullscreen_bg->node, (c = focustop(m)) && c->isfullscreen);
	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, LENGTH(m->ltsymbol));

	wl_list_for_each(c, &clients, link) {
		if (c->scene->node.parent == layers[LyrFS]) continue;
		wlr_scene_node_reparent(&c->scene->node,
				(!m->lt[m->sellt]->arrange && c->isfloating) ? layers[LyrTile] :
				(m->lt[m->sellt]->arrange && c->isfloating) ? layers[LyrFloat] : c->scene->node.parent);
	}

	if (m->lt[m->sellt]->arrange) m->lt[m->sellt]->arrange(m);
	motionnotify(0, NULL, 0, 0, 0, 0);
}

static int hidecursor(void *data){
    if (cursor_hidden)
        return 0;
    cursor_hidden = 1;
    wlr_cursor_set_surface(cursor, NULL, 0, 0);
    return 0;
}

void autostart(void) {
	for (size_t i = 0; i < sizeof(autostart_cmds) / sizeof(autostart_cmds[0]); i++) {
		if (fork() == 0) {
			prctl(PR_SET_PDEATHSIG, SIGTERM);
			if (getppid() == 1) exit(1);
			setsid();
			execvp(autostart_cmds[i][0], (char *const *)autostart_cmds[i]);
			exit(1);
		}
	}
}

void arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive) {
	LayerSurface *l;
	struct wlr_box full_area = m->m;
	wl_list_for_each(l, list, link) {
		struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;
		if (!layer_surface->initialized) continue;
		if (exclusive != (layer_surface->current.exclusive_zone > 0)) continue;
		wlr_scene_layer_surface_v1_configure(l->scene_layer, &full_area, usable_area);
		wlr_scene_node_set_position(&l->popups->node, l->scene->node.x, l->scene->node.y);
	}
}

void arrangelayers(Monitor *m) {
	int i;
	struct wlr_box usable_area = m->m;
	LayerSurface *l;
	uint32_t layers_above_shell[] = { ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, ZWLR_LAYER_SHELL_V1_LAYER_TOP };
	
	if (!m->wlr_output || !m->wlr_output->enabled) return;

	for (i = 3; i >= 0; i--) arrangelayer(m, &m->layers[i], &usable_area, 1);
	if (!wlr_box_equal(&usable_area, &m->w)) {
		m->w = usable_area;
		arrange(m);
	}
	for (i = 3; i >= 0; i--) arrangelayer(m, &m->layers[i], &usable_area, 0);
	
	for (i = 0; i < (int)LENGTH(layers_above_shell); i++) {
		wl_list_for_each_reverse(l, &m->layers[layers_above_shell[i]], link) {
			if (!l->layer_surface->current.keyboard_interactive || !l->mapped) continue;
			focusclient(NULL, 0);
			exclusive_focus = l;
			client_notify_enter(l->layer_surface->surface, wlr_seat_get_keyboard(seat));
			return;
		}
	}
}

void axisnotify(struct wl_listener *listener, void *data) {
	struct wlr_pointer_axis_event *event = data;
	wlr_seat_pointer_notify_axis(seat, event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source, event->relative_direction);
}

void buttonpress(struct wl_listener *listener, void *data) {
	struct wlr_pointer_button_event *event = data;
	struct wlr_keyboard *keyboard;
	uint32_t mods;
	Client *c;
	const Button *b;

	switch (event->state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		cursor_mode = CurPressed;
		xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
		if (c && (!client_is_unmanaged(c) || client_wants_focus(c)))
			focusclient(c, 1);

		keyboard = wlr_seat_get_keyboard(seat);
		mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
		for (b = buttons; b < END(buttons); b++) {
			if (CLEANMASK(mods) == CLEANMASK(b->mod) && event->button == b->button && b->func) {
				b->func(&b->arg);
				return;
			}
		}
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		if (cursor_mode != CurNormal && cursor_mode != CurPressed) {
			wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
			cursor_mode = CurNormal;
			return;
		}
		cursor_mode = CurNormal;
		break;
	}
	wlr_seat_pointer_notify_button(seat, event->time_msec, event->button, event->state);
}

void cleanup(void) {
	cleanuplisteners();
	wl_display_destroy_clients(dpy);
	if (child_pid > 0) {
		kill(-child_pid, SIGTERM);
		waitpid(child_pid, NULL, 0);
	}
	wlr_xcursor_manager_destroy(cursor_mgr);
	destroykeyboardgroup(&kb_group->destroy, NULL);
	wlr_backend_destroy(backend);
	wl_display_destroy(dpy);
	wlr_scene_node_destroy(&scene->tree.node);
}

void cleanupmon(struct wl_listener *listener, void *data) {
	LayerSurface *l, *tmp;
	for (size_t i = 0; i < LENGTH(monitor.layers); i++) {
		wl_list_for_each_safe(l, tmp, &monitor.layers[i], link)
			wlr_layer_surface_v1_destroy(l->layer_surface);
	}
	wl_list_remove(&monitor.destroy.link);
	wl_list_remove(&monitor.frame.link);
	wl_list_remove(&monitor.request_state.link);
	monitor.wlr_output->data = NULL;
	wlr_output_layout_remove(output_layout, monitor.wlr_output);
	wlr_scene_output_destroy(monitor.scene_output);
	wlr_scene_node_destroy(&monitor.fullscreen_bg->node);
	mon_init = 0;
}

void cleanuplisteners(void) {
	wl_list_remove(&cursor_axis.link);
	wl_list_remove(&cursor_button.link);
	wl_list_remove(&cursor_frame.link);
	wl_list_remove(&cursor_motion.link);
	wl_list_remove(&cursor_motion_absolute.link);
	wl_list_remove(&gpu_reset.link);
	wl_list_remove(&layout_change.link);
	wl_list_remove(&new_input_device.link);
	wl_list_remove(&new_pointer_constraint.link);
	wl_list_remove(&new_output.link);
	wl_list_remove(&new_xdg_toplevel.link);
	wl_list_remove(&new_xdg_decoration.link);
	wl_list_remove(&new_xdg_popup.link);
	wl_list_remove(&new_layer_surface.link);
	wl_list_remove(&output_power_mgr_set_mode.link);
	wl_list_remove(&request_activate.link);
	wl_list_remove(&request_cursor.link);
	wl_list_remove(&request_set_psel.link);
	wl_list_remove(&request_set_sel.link);
	wl_list_remove(&request_set_cursor_shape.link);
	wl_list_remove(&request_start_drag.link);
	wl_list_remove(&start_drag.link);
}

void commitlayersurfacenotify(struct wl_listener *listener, void *data) {
	LayerSurface *l = wl_container_of(listener, l, surface_commit);
	struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;
	struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->current.layer]];
	struct wlr_layer_surface_v1_state old_state;

	if (l->layer_surface->initial_commit) {
		client_set_scale(layer_surface->surface, monitor.wlr_output->scale);
		old_state = l->layer_surface->current;
		l->layer_surface->current = l->layer_surface->pending;
		arrangelayers(&monitor);
		l->layer_surface->current = old_state;
		return;
	}

	if (layer_surface->current.committed == 0 && l->mapped == layer_surface->surface->mapped) return;
	l->mapped = layer_surface->surface->mapped;

	if (scene_layer != l->scene->node.parent) {
		wlr_scene_node_reparent(&l->scene->node, scene_layer);
		wl_list_remove(&l->link);
		wl_list_insert(&monitor.layers[layer_surface->current.layer], &l->link);
		wlr_scene_node_reparent(&l->popups->node, (layer_surface->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer));
	}
	arrangelayers(&monitor);
}

void commitnotify(struct wl_listener *listener, void *data) {
	Client *c = wl_container_of(listener, c, commit);

	if (c->surface.xdg->initial_commit) {
		applyrules(c);
		client_set_scale(client_surface(c), monitor.wlr_output->scale);
		wlr_xdg_toplevel_set_wm_capabilities(c->surface.xdg->toplevel, WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
		if (c->decoration) requestdecorationmode(&c->set_decoration_mode, c->decoration);
		wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, 0, 0);
		return;
	}

	resize(c, c->geom, (c->isfloating && !c->isfullscreen));
	if (c->resize && c->resize <= c->surface.xdg->current.configure_serial) c->resize = 0;
}

void commitpopup(struct wl_listener *listener, void *data) {
	struct wlr_surface *surface = data;
	struct wlr_xdg_popup *popup = wlr_xdg_popup_try_from_wlr_surface(surface);
	LayerSurface *l = NULL;
	Client *c = NULL;
	struct wlr_box box;
	int type = -1;

	if (!popup->base->initial_commit) return;

	type = toplevel_from_wlr_surface(popup->base->surface, &c, &l);
	if (!popup->parent || type < 0) return;
	
	popup->base->surface->data = wlr_scene_xdg_surface_create(popup->parent->data, popup->base);
	box = monitor.w;
	box.x -= (type == LayerShell ? l->scene->node.x : c->geom.x);
	box.y -= (type == LayerShell ? l->scene->node.y : c->geom.y);
	wlr_xdg_popup_unconstrain_from_box(popup, &box);
	wl_list_remove(&listener->link);
	free(listener);
}

void createdecoration(struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_decoration_v1 *deco = data;
	Client *c = deco->toplevel->base->data;
	c->decoration = deco;
	LISTEN(&deco->events.request_mode, &c->set_decoration_mode, requestdecorationmode);
	LISTEN(&deco->events.destroy, &c->destroy_decoration, destroydecoration);
	requestdecorationmode(&c->set_decoration_mode, deco);
}

void createkeyboard(struct wlr_keyboard *keyboard) {
	wlr_keyboard_set_keymap(keyboard, kb_group->wlr_group->keyboard.keymap);
	wlr_keyboard_group_add_keyboard(kb_group->wlr_group, keyboard);
}

KeyboardGroup *createkeyboardgroup(void) {
	KeyboardGroup *group = ecalloc(1, sizeof(*group));
	struct xkb_context *context;
	struct xkb_keymap *keymap;

	group->wlr_group = wlr_keyboard_group_create();
	group->wlr_group->data = group;

	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!(keymap = xkb_keymap_new_from_names(context, &xkb_rules, XKB_KEYMAP_COMPILE_NO_FLAGS)))
		die("failed to compile keymap");

	wlr_keyboard_set_keymap(&group->wlr_group->keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);

	wlr_keyboard_set_repeat_info(&group->wlr_group->keyboard, repeat_rate, repeat_delay);
	LISTEN(&group->wlr_group->keyboard.events.key, &group->key, keypress);
	LISTEN(&group->wlr_group->keyboard.events.modifiers, &group->modifiers, keypressmod);
	group->key_repeat_source = wl_event_loop_add_timer(event_loop, keyrepeat, group);
	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	return group;
}

void createlayersurface(struct wl_listener *listener, void *data) {
	struct wlr_layer_surface_v1 *layer_surface = data;
	LayerSurface *l;
	struct wlr_surface *surface = layer_surface->surface;
	struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->pending.layer]];

	if (!layer_surface->output && !(layer_surface->output = monitor.wlr_output)) {
		wlr_layer_surface_v1_destroy(layer_surface);
		return;
	}

	l = layer_surface->data = ecalloc(1, sizeof(*l));
	l->type = LayerShell;
	LISTEN(&surface->events.commit, &l->surface_commit, commitlayersurfacenotify);
	LISTEN(&surface->events.unmap, &l->unmap, unmaplayersurfacenotify);
	LISTEN(&layer_surface->events.destroy, &l->destroy, destroylayersurfacenotify);

	l->layer_surface = layer_surface;
	l->mon = &monitor;
	l->scene_layer = wlr_scene_layer_surface_v1_create(scene_layer, layer_surface);
	l->scene = l->scene_layer->tree;
	l->popups = surface->data = wlr_scene_tree_create(layer_surface->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer);
	l->scene->node.data = l->popups->node.data = l;

	wl_list_insert(&monitor.layers[layer_surface->pending.layer], &l->link);
	wlr_surface_send_enter(surface, layer_surface->output);
}

void createmon(struct wl_listener *listener, void *data) {
	struct wlr_output *wlr_output = data;
	struct wlr_output_state state;
	struct wlr_output_mode *mode, *best_mode = NULL;

	if (mon_init) return;

	if (!wlr_output_init_render(wlr_output, alloc, drw)) return;

	monitor.wlr_output = wlr_output;
	wlr_output->data = &monitor;

	for (size_t i = 0; i < LENGTH(monitor.layers); i++)
		wl_list_init(&monitor.layers[i]);

	wlr_output_state_init(&state);
	monitor.tagset[0] = monitor.tagset[1] = 1;
	monitor.mfact = 0.55f;
	monitor.nmaster = 1;
	monitor.lt[0] = &layouts[0];
	monitor.lt[1] = &layouts[LENGTH(layouts) > 1 ? 1 : 0];
	strncpy(monitor.ltsymbol, monitor.lt[monitor.sellt]->symbol, LENGTH(monitor.ltsymbol));
	
	wlr_output_state_set_scale(&state, 1.0f);
	wlr_output_state_set_transform(&state, WL_OUTPUT_TRANSFORM_NORMAL);

  wl_list_for_each(mode, &wlr_output->modes, link) {
		wlr_log(WLR_INFO, "Modo detectado: %dx%d @ %d mHz", mode->width, mode->height, mode->refresh);
		
		if (mode->width == 1920 && mode->height == 1080 && abs(mode->refresh - 48000) <= 2000) {
			best_mode = mode;
		}
	}
	
	if (!best_mode) {
		wlr_log(WLR_ERROR, "FATAL: Modo 1920x1080 cercano a 48Hz no encontrado. Mira los 'Modos detectados' arriba.");
		abort();
	}

	wlr_output_state_set_mode(&state, best_mode);
	wlr_output_state_set_enabled(&state, 1);
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	LISTEN(&wlr_output->events.frame, &monitor.frame, rendermon);
	LISTEN(&wlr_output->events.destroy, &monitor.destroy, cleanupmon);
	LISTEN(&wlr_output->events.request_state, &monitor.request_state, requestmonstate);

	monitor.fullscreen_bg = wlr_scene_rect_create(layers[LyrFS], 0, 0, fullscreen_bg);
	wlr_scene_node_set_enabled(&monitor.fullscreen_bg->node, 0);

	monitor.scene_output = wlr_scene_output_create(scene, wlr_output);
	wlr_output_layout_add(output_layout, wlr_output, 0, 0);

	mon_init = 1;
	printstatus();
}

void createnotify(struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel *toplevel = data;
	Client *c = toplevel->base->data = ecalloc(1, sizeof(*c));
	c->surface.xdg = toplevel->base;
	c->bw = borderpx;

	LISTEN(&toplevel->base->surface->events.commit, &c->commit, commitnotify);
	LISTEN(&toplevel->base->surface->events.map, &c->map, mapnotify);
	LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, unmapnotify);
	LISTEN(&toplevel->events.destroy, &c->destroy, destroynotify);
	LISTEN(&toplevel->events.request_fullscreen, &c->fullscreen, fullscreennotify);
	LISTEN(&toplevel->events.request_maximize, &c->maximize, maximizenotify);
	LISTEN(&toplevel->events.set_title, &c->set_title, updatetitle);
}

void createpointer(struct wlr_pointer *pointer) {
	struct libinput_device *device;
	if (wlr_input_device_is_libinput(&pointer->base) && (device = wlr_libinput_get_device_handle(&pointer->base))) {
		if (libinput_device_config_tap_get_finger_count(device)) {
			libinput_device_config_tap_set_enabled(device, tap_to_click);
			libinput_device_config_tap_set_drag_enabled(device, tap_and_drag);
			libinput_device_config_tap_set_drag_lock_enabled(device, drag_lock);
			libinput_device_config_tap_set_button_map(device, button_map);
		}
		if (libinput_device_config_scroll_has_natural_scroll(device))
			libinput_device_config_scroll_set_natural_scroll_enabled(device, natural_scrolling);
		if (libinput_device_config_dwt_is_available(device))
			libinput_device_config_dwt_set_enabled(device, disable_while_typing);
		if (libinput_device_config_left_handed_is_available(device))
			libinput_device_config_left_handed_set(device, left_handed);
		if (libinput_device_config_middle_emulation_is_available(device))
			libinput_device_config_middle_emulation_set_enabled(device, middle_button_emulation);
		if (libinput_device_config_scroll_get_methods(device) != LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
			libinput_device_config_scroll_set_method(device, scroll_method);
		if (libinput_device_config_click_get_methods(device) != LIBINPUT_CONFIG_CLICK_METHOD_NONE)
			libinput_device_config_click_set_method(device, click_method);
		if (libinput_device_config_send_events_get_modes(device))
			libinput_device_config_send_events_set_mode(device, send_events_mode);
		if (libinput_device_config_accel_is_available(device)) {
			libinput_device_config_accel_set_profile(device, accel_profile);
			libinput_device_config_accel_set_speed(device, accel_speed);
		}
	}
	wlr_cursor_attach_input_device(cursor, &pointer->base);
}

void createpointerconstraint(struct wl_listener *listener, void *data) {
	PointerConstraint *pointer_constraint = ecalloc(1, sizeof(*pointer_constraint));
	pointer_constraint->constraint = data;
	LISTEN(&pointer_constraint->constraint->events.destroy, &pointer_constraint->destroy, destroypointerconstraint);
}

void createpopup(struct wl_listener *listener, void *data) {
	struct wlr_xdg_popup *popup = data;
	LISTEN_STATIC(&popup->base->surface->events.commit, commitpopup);
}

void cursorconstrain(struct wlr_pointer_constraint_v1 *constraint) {
	if (active_constraint == constraint) return;
	if (active_constraint) wlr_pointer_constraint_v1_send_deactivated(active_constraint);
	active_constraint = constraint;
	wlr_pointer_constraint_v1_send_activated(constraint);
}

void cursorframe(struct wl_listener *listener, void *data) {
	wlr_seat_pointer_notify_frame(seat);
}

void cursorwarptohint(void) {
	Client *c = NULL;
	double sx = active_constraint->current.cursor_hint.x;
	double sy = active_constraint->current.cursor_hint.y;
	toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
	if (c && active_constraint->current.cursor_hint.enabled) {
		wlr_cursor_warp(cursor, NULL, sx + c->geom.x + c->bw, sy + c->geom.y + c->bw);
		wlr_seat_pointer_warp(active_constraint->seat, sx, sy);
	}
}

void destroydecoration(struct wl_listener *listener, void *data) {
	Client *c = wl_container_of(listener, c, destroy_decoration);
	wl_list_remove(&c->destroy_decoration.link);
	wl_list_remove(&c->set_decoration_mode.link);
}

void destroydragicon(struct wl_listener *listener, void *data) {
	focusclient(focustop(&monitor), 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
	wl_list_remove(&listener->link);
	free(listener);
}

void destroylayersurfacenotify(struct wl_listener *listener, void *data) {
	LayerSurface *l = wl_container_of(listener, l, destroy);
	wl_list_remove(&l->link);
	wl_list_remove(&l->destroy.link);
	wl_list_remove(&l->unmap.link);
	wl_list_remove(&l->surface_commit.link);
	wlr_scene_node_destroy(&l->scene->node);
	wlr_scene_node_destroy(&l->popups->node);
	free(l);
}

void destroynotify(struct wl_listener *listener, void *data) {
	Client *c = wl_container_of(listener, c, destroy);
	wl_list_remove(&c->destroy.link);
	wl_list_remove(&c->set_title.link);
	wl_list_remove(&c->fullscreen.link);
	wl_list_remove(&c->commit.link);
	wl_list_remove(&c->map.link);
	wl_list_remove(&c->unmap.link);
	wl_list_remove(&c->maximize.link);
	free(c);
}

void destroypointerconstraint(struct wl_listener *listener, void *data) {
	PointerConstraint *pointer_constraint = wl_container_of(listener, pointer_constraint, destroy);
	if (active_constraint == pointer_constraint->constraint) {
		cursorwarptohint();
		active_constraint = NULL;
	}
	wl_list_remove(&pointer_constraint->destroy.link);
	free(pointer_constraint);
}

void destroykeyboardgroup(struct wl_listener *listener, void *data) {
	KeyboardGroup *group = wl_container_of(listener, group, destroy);
	wl_event_source_remove(group->key_repeat_source);
	wl_list_remove(&group->key.link);
	wl_list_remove(&group->modifiers.link);
	wl_list_remove(&group->destroy.link);
	wlr_keyboard_group_destroy(group->wlr_group);
	free(group);
}

void focusclient(Client *c, int lift) {
	struct wlr_surface *old = seat->keyboard_state.focused_surface;
	int unused_lx, unused_ly, old_client_type;
	Client *old_c = NULL;
	LayerSurface *old_l = NULL;

	if (c && lift) wlr_scene_node_raise_to_top(&c->scene->node);
	if (c && client_surface(c) == old) return;

	if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) == XDGShell) {
		struct wlr_xdg_popup *popup, *tmp;
		wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
			wlr_xdg_popup_destroy(popup);
	}

	if (c && !client_is_unmanaged(c)) {
		wl_list_remove(&c->flink);
		wl_list_insert(&fstack, &c->flink);
		c->isurgent = 0;
		if (!exclusive_focus && !seat->drag) client_set_border_color(c, focuscolor);
	}

	if (old && (!c || client_surface(c) != old)) {
		if (old_client_type == LayerShell && wlr_scene_node_coords(&old_l->scene->node, &unused_lx, &unused_ly) && old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
			return;
		} else if (old_c && old_c == exclusive_focus && client_wants_focus(old_c)) {
			return;
		} else if (old_c && !client_is_unmanaged(old_c) && (!c || !client_wants_focus(c))) {
			client_set_border_color(old_c, bordercolor);
			client_activate_surface(old, 0);
		}
	}
	printstatus();

	if (!c) {
		wlr_seat_keyboard_notify_clear_focus(seat);
		return;
	}

	motionnotify(0, NULL, 0, 0, 0, 0);
	client_notify_enter(client_surface(c), wlr_seat_get_keyboard(seat));
	client_activate_surface(client_surface(c), 1);
}

void focusstack(const Arg *arg) {
	Client *c, *sel = focustop(&monitor);
	if (!sel || (sel->isfullscreen && !client_has_children(sel))) return;
	
	if (arg->i > 0) {
		wl_list_for_each(c, &sel->link, link) {
			if (&c->link == &clients) continue;
			if (VISIBLEON(c, &monitor)) break;
		}
	} else {
		wl_list_for_each_reverse(c, &sel->link, link) {
			if (&c->link == &clients) continue;
			if (VISIBLEON(c, &monitor)) break;
		}
	}
	focusclient(c, 1);
}

Client *focustop(Monitor *m) {
	Client *c;
	wl_list_for_each(c, &fstack, flink) {
		if (VISIBLEON(c, m)) return c;
	}
	return NULL;
}

void fullscreennotify(struct wl_listener *listener, void *data) {
	Client *c = wl_container_of(listener, c, fullscreen);
	setfullscreen(c, client_wants_fullscreen(c));
}

void gpureset(struct wl_listener *listener, void *data) {
	struct wlr_renderer *old_drw = drw;
	struct wlr_allocator *old_alloc = alloc;

	if (!(drw = wlr_renderer_autocreate(backend))) die("couldn't recreate renderer");
	if (!(alloc = wlr_allocator_autocreate(backend, drw))) die("couldn't recreate allocator");

	wl_list_remove(&gpu_reset.link);
	wl_signal_add(&drw->events.lost, &gpu_reset);
	wlr_compositor_set_renderer(compositor, drw);

	if (monitor.wlr_output) wlr_output_init_render(monitor.wlr_output, alloc, drw);

	wlr_allocator_destroy(old_alloc);
	wlr_renderer_destroy(old_drw);
}

void handlesig(int signo) {
	if (signo == SIGCHLD) while (waitpid(-1, NULL, WNOHANG) > 0);
	else if (signo == SIGINT || signo == SIGTERM) quit(NULL);
}

void inputdevice(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	uint32_t caps;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD: createkeyboard(wlr_keyboard_from_input_device(device)); break;
	case WLR_INPUT_DEVICE_POINTER: createpointer(wlr_pointer_from_input_device(device)); break;
	default: break;
	}

	caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&kb_group->wlr_group->devices)) caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	wlr_seat_set_capabilities(seat, caps);
}

int keybinding(uint32_t mods, xkb_keysym_t sym) {
	const Key *k;
	for (k = keys; k < END(keys); k++) {
		if (CLEANMASK(mods) == CLEANMASK(k->mod) && xkb_keysym_to_lower(sym) == xkb_keysym_to_lower(k->keysym) && k->func) {
			k->func(&k->arg);
			return 1;
		}
	}
	return 0;
}

void keypress(struct wl_listener *listener, void *data) {
	int i, handled = 0;
	KeyboardGroup *group = wl_container_of(listener, group, key);
	struct wlr_keyboard_key_event *event = data;
	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(group->wlr_group->keyboard.xkb_state, keycode, &syms);
	uint32_t mods = wlr_keyboard_get_modifiers(&group->wlr_group->keyboard);

	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (i = 0; i < nsyms; i++) handled = keybinding(mods, syms[i]) || handled;
	}

	if (handled && group->wlr_group->keyboard.repeat_info.delay > 0) {
		group->mods = mods;
		group->keysyms = syms;
		group->nsyms = nsyms;
		wl_event_source_timer_update(group->key_repeat_source, group->wlr_group->keyboard.repeat_info.delay);
	} else {
		group->nsyms = 0;
		wl_event_source_timer_update(group->key_repeat_source, 0);
	}

	if (handled) return;

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
}

void keypressmod(struct wl_listener *listener, void *data) {
	KeyboardGroup *group = wl_container_of(listener, group, modifiers);
	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	wlr_seat_keyboard_notify_modifiers(seat, &group->wlr_group->keyboard.modifiers);
}

int keyrepeat(void *data) {
	KeyboardGroup *group = data;
	int i;
	if (!group->nsyms || group->wlr_group->keyboard.repeat_info.rate <= 0) return 0;
	wl_event_source_timer_update(group->key_repeat_source, 1000 / group->wlr_group->keyboard.repeat_info.rate);
	for (i = 0; i < group->nsyms; i++) keybinding(group->mods, group->keysyms[i]);
	return 0;
}

void killclient(const Arg *arg) {
	Client *sel = focustop(&monitor);
	if (sel) client_send_close(sel);
}

void mapnotify(struct wl_listener *listener, void *data) {
	Client *p = NULL;
	Client *w, *c = wl_container_of(listener, c, map);
	int i;

	c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrTile]);
	wlr_scene_node_set_enabled(&c->scene->node, client_is_unmanaged(c));
	c->scene_surface = c->type == XDGShell ? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg) : wlr_scene_subsurface_tree_create(c->scene, client_surface(c));
	c->scene->node.data = c->scene_surface->node.data = c;

	client_get_geometry(c, &c->geom);

	if (client_is_unmanaged(c)) {
		wlr_scene_node_reparent(&c->scene->node, layers[LyrFloat]);
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		client_set_size(c, c->geom.width, c->geom.height);
		if (client_wants_focus(c)) {
			focusclient(c, 1);
			exclusive_focus = c;
		}
		goto unset_fullscreen;
	}

	for (i = 0; i < 4; i++) {
		c->border[i] = wlr_scene_rect_create(c->scene, 0, 0, c->isurgent ? urgentcolor : bordercolor);
		c->border[i]->node.data = c;
	}

	client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
	c->geom.width += 2 * c->bw;
	c->geom.height += 2 * c->bw;

	wl_list_insert(&clients, &c->link);
	wl_list_insert(&fstack, &c->flink);

	if ((p = client_get_parent(c))) {
		c->isfloating = 1;
		c->mon = &monitor;
		c->tags = p->tags;
	} else {
		applyrules(c);
	}

  focusclient(c, 1);
	arrange(&monitor);
	printstatus();

unset_fullscreen:
	wl_list_for_each(w, &clients, link) {
		if (w != c && w != p && w->isfullscreen && (w->tags & c->tags)) setfullscreen(w, 0);
	}
}

void maximizenotify(struct wl_listener *listener, void *data) {
	Client *c = wl_container_of(listener, c, maximize);
	if (c->surface.xdg->initialized && wl_resource_get_version(c->surface.xdg->toplevel->resource) < XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
		wlr_xdg_surface_schedule_configure(c->surface.xdg);
}

void motionabsolute(struct wl_listener *listener, void *data) {
	struct wlr_pointer_motion_absolute_event *event = data;
	double lx, ly, dx, dy;
	if (!event->time_msec) wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);
	wlr_cursor_absolute_to_layout_coords(cursor, &event->pointer->base, event->x, event->y, &lx, &ly);
	dx = lx - cursor->x;
	dy = ly - cursor->y;
	motionnotify(event->time_msec, &event->pointer->base, dx, dy, dx, dy);
}

void motionnotify(uint32_t time, struct wlr_input_device *device, double dx, double dy, double dx_unaccel, double dy_unaccel) {
	double sx = 0, sy = 0, sx_confined, sy_confined;
	Client *c = NULL, *w = NULL;
	LayerSurface *l = NULL;
	struct wlr_surface *surface = NULL;
	struct wlr_pointer_constraint_v1 *constraint;
  wl_event_source_timer_update(cursor_hide_timer,CURSOR_HIDE_TIMEOUT);

  if (cursor_hidden) {
    cursor_hidden = 0;
    wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
  }

	xytonode(cursor->x, cursor->y, &surface, &c, NULL, &sx, &sy);

	if (cursor_mode == CurPressed && !seat->drag && surface != seat->pointer_state.focused_surface && toplevel_from_wlr_surface(seat->pointer_state.focused_surface, &w, &l) >= 0) {
		c = w;
		surface = seat->pointer_state.focused_surface;
		sx = cursor->x - (l ? l->scene->node.x : w->geom.x);
		sy = cursor->y - (l ? l->scene->node.y : w->geom.y);
	}

	if (time) {
		wlr_relative_pointer_manager_v1_send_relative_motion(relative_pointer_mgr, seat, (uint64_t)time * 1000, dx, dy, dx_unaccel, dy_unaccel);
		wl_list_for_each(constraint, &pointer_constraints->constraints, link) cursorconstrain(constraint);

		if (active_constraint) {
			toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
			if (c && active_constraint->surface == seat->pointer_state.focused_surface) {
				sx = cursor->x - c->geom.x - c->bw;
				sy = cursor->y - c->geom.y - c->bw;
				if (wlr_region_confine(&active_constraint->region, sx, sy, sx + dx, sy + dy, &sx_confined, &sy_confined)) {
					dx = sx_confined - sx;
					dy = sy_confined - sy;
				}
				if (active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) return;
			}
		}
		wlr_cursor_move(cursor, device, dx, dy);
	}

	wlr_scene_node_set_position(&drag_icon->node, (int)round(cursor->x), (int)round(cursor->y));
	if (!surface && !seat->drag) wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
	pointerfocus(c, surface, sx, sy, time);
}

void motionrelative(struct wl_listener *listener, void *data) {
	struct wlr_pointer_motion_event *event = data;
	motionnotify(event->time_msec, &event->pointer->base, event->delta_x, event->delta_y, event->unaccel_dx, event->unaccel_dy);
}

void pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy, uint32_t time) {
	struct timespec now;
	if (surface != seat->pointer_state.focused_surface && sloppyfocus && time && c && !client_is_unmanaged(c))
		focusclient(c, 0);

	if (!surface) {
		wlr_seat_pointer_notify_clear_focus(seat);
		return;
	}

	if (!time) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
	}

	wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

void printstatus(void) {
	Client *c;
	uint32_t occ = 0, urg = 0, sel = 0;
	if (!monitor.wlr_output) return;

	wl_list_for_each(c, &clients, link) {
		occ |= c->tags;
		if (c->isurgent) urg |= c->tags;
	}

	if ((c = focustop(&monitor))) {
		printf("%s title %s\n", monitor.wlr_output->name, client_get_title(c));
		printf("%s appid %s\n", monitor.wlr_output->name, client_get_appid(c));
		printf("%s fullscreen %d\n", monitor.wlr_output->name, c->isfullscreen);
		printf("%s floating %d\n", monitor.wlr_output->name, c->isfloating);
		sel = c->tags;
	} else {
		printf("%s title \n", monitor.wlr_output->name);
		printf("%s appid \n", monitor.wlr_output->name);
		printf("%s fullscreen \n", monitor.wlr_output->name);
		printf("%s floating \n", monitor.wlr_output->name);
	}

	printf("%s selmon 1\n", monitor.wlr_output->name);
	printf("%s tags %"PRIu32" %"PRIu32" %"PRIu32" %"PRIu32"\n", monitor.wlr_output->name, occ, monitor.tagset[monitor.seltags], sel, urg);
	printf("%s layout %s\n", monitor.wlr_output->name, monitor.ltsymbol);
	fflush(stdout);
}

void powermgrsetmode(struct wl_listener *listener, void *data) {
	updatemons(NULL, NULL);
}

void quit(const Arg *arg) {
	wl_display_terminate(dpy);
}

void rendermon(struct wl_listener *listener, void *data) {
	Monitor *m = wl_container_of(listener, m, frame);
	Client *c;
	struct wlr_output_state pending = {0};
	struct timespec now;

	wl_list_for_each(c, &clients, link) {
		if (c->resize && !c->isfloating && client_is_rendered_on_mon(c, m) && !client_is_stopped(c)) goto skip;
	}
	wlr_scene_output_commit(m->scene_output, NULL);

skip:
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(m->scene_output, &now);
	wlr_output_state_finish(&pending);
}

void requestdecorationmode(struct wl_listener *listener, void *data) {
	Client *c = wl_container_of(listener, c, set_decoration_mode);
	if (c->surface.xdg->initialized) wlr_xdg_toplevel_decoration_v1_set_mode(c->decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void requeststartdrag(struct wl_listener *listener, void *data) {
	struct wlr_seat_request_start_drag_event *event = data;
	if (wlr_seat_validate_pointer_grab_serial(seat, event->origin, event->serial))
		wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
	else
		wlr_data_source_destroy(event->drag->source);
}

void requestmonstate(struct wl_listener *listener, void *data) {
	struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(event->output, event->state);
	updatemons(NULL, NULL);
}

void resize(Client *c, struct wlr_box geo, int interact) {
	struct wlr_box *bbox;
	struct wlr_box clip;

	if (!client_surface(c)->mapped) return;

	bbox = interact ? &sgeom : &monitor.w;
	client_set_bounds(c, geo.width, geo.height);
	c->geom = geo;
	applybounds(c, bbox);

	wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
	wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
	wlr_scene_rect_set_size(c->border[0], c->geom.width, c->bw);
	wlr_scene_rect_set_size(c->border[1], c->geom.width, c->bw);
	wlr_scene_rect_set_size(c->border[2], c->bw, c->geom.height - 2 * c->bw);
	wlr_scene_rect_set_size(c->border[3], c->bw, c->geom.height - 2 * c->bw);
	wlr_scene_node_set_position(&c->border[1]->node, 0, c->geom.height - c->bw);
	wlr_scene_node_set_position(&c->border[2]->node, 0, c->bw);
	wlr_scene_node_set_position(&c->border[3]->node, c->geom.width - c->bw, c->bw);

	c->resize = client_set_size(c, c->geom.width - 2 * c->bw, c->geom.height - 2 * c->bw);
	client_get_clip(c, &clip);
	wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
}

void run(char *startup_cmd) {
	const char *socket = wl_display_add_socket_auto(dpy);
	if (!socket) die("startup: display_add_socket_auto");
	setenv("WAYLAND_DISPLAY", socket, 1);

	if (!wlr_backend_start(backend)) die("startup: backend_start");

	if (startup_cmd) {
		int piperw[2];
		if (pipe(piperw) < 0) die("startup: pipe:");
		if ((child_pid = fork()) < 0) die("startup: fork:");
		if (child_pid == 0) {
			setsid();
			dup2(piperw[0], STDIN_FILENO);
			close(piperw[0]);
			close(piperw[1]);
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
			die("startup: execl:");
		}
		dup2(piperw[1], STDOUT_FILENO);
		close(piperw[1]);
		close(piperw[0]);
	}

	if (fd_set_nonblock(STDOUT_FILENO) < 0) close(STDOUT_FILENO);
	printstatus();

	wlr_cursor_warp_closest(cursor, NULL, cursor->x, cursor->y);
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
	wl_display_run(dpy);
}

void setcursor(struct wl_listener *listener, void *data) {
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	if (cursor_mode != CurNormal && cursor_mode != CurPressed) return;
	if (event->seat_client == seat->pointer_state.focused_client) wlr_cursor_set_surface(cursor, event->surface, event->hotspot_x, event->hotspot_y);
}

void setcursorshape(struct wl_listener *listener, void *data) {
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
	if (cursor_mode != CurNormal && cursor_mode != CurPressed) return;
	if (event->seat_client == seat->pointer_state.focused_client) wlr_cursor_set_xcursor(cursor, cursor_mgr, wlr_cursor_shape_v1_name(event->shape));
}

void setfloating(Client *c, int floating) {
	Client *p = client_get_parent(c);
	c->isfloating = floating;
	if (!client_surface(c)->mapped || !monitor.lt[monitor.sellt]->arrange) return;
	wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen || (p && p->isfullscreen) ? LyrFS : c->isfloating ? LyrFloat : LyrTile]);
	arrange(&monitor);
	printstatus();
}

void setfullscreen(Client *c, int fullscreen) {
	c->isfullscreen = fullscreen;
	if (!client_surface(c)->mapped) return;
	c->bw = fullscreen ? 0 : borderpx;
	client_set_fullscreen(c, fullscreen);
	wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen ? LyrFS : c->isfloating ? LyrFloat : LyrTile]);

	if (fullscreen) {
		c->prev = c->geom;
		resize(c, monitor.m, 0);
	} else {
		resize(c, c->prev, 0);
	}
	arrange(&monitor);
	printstatus();
}

void setpsel(struct wl_listener *listener, void *data) {
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

void setsel(struct wl_listener *listener, void *data) {
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(seat, event->source, event->serial);
}

void setup(void) {
	int drm_fd, i, sig[] = {SIGCHLD, SIGINT, SIGTERM, SIGPIPE};
	struct sigaction sa = {.sa_flags = SA_RESTART, .sa_handler = handlesig};
	sigemptyset(&sa.sa_mask);
	for (i = 0; i < (int)LENGTH(sig); i++) sigaction(sig[i], &sa, NULL);

	wlr_log_init(log_level, NULL);

	dpy = wl_display_create();
	event_loop = wl_display_get_event_loop(dpy);

	if (!(backend = wlr_backend_autocreate(event_loop, &session))) die("couldn't create backend");

	scene = wlr_scene_create();
	root_bg = wlr_scene_rect_create(&scene->tree, 0, 0, rootcolor);
	for (i = 0; i < NUM_LAYERS; i++) layers[i] = wlr_scene_tree_create(&scene->tree);
	drag_icon = wlr_scene_tree_create(&scene->tree);
	wlr_scene_node_place_below(&drag_icon->node, &layers[LyrBlock]->node);

	if (!(drw = wlr_renderer_autocreate(backend))) die("couldn't create renderer");
	wl_signal_add(&drw->events.lost, &gpu_reset);

	wlr_renderer_init_wl_shm(drw, dpy);
	if (wlr_renderer_get_texture_formats(drw, WLR_BUFFER_CAP_DMABUF)) {
		wlr_drm_create(dpy, drw);
		wlr_scene_set_linux_dmabuf_v1(scene, wlr_linux_dmabuf_v1_create_with_renderer(dpy, 5, drw));
	}

	if ((drm_fd = wlr_renderer_get_drm_fd(drw)) >= 0 && drw->features.timeline && backend->features.timeline)
		wlr_linux_drm_syncobj_manager_v1_create(dpy, 1, drm_fd);

	if (!(alloc = wlr_allocator_autocreate(backend, drw))) die("couldn't create allocator");

	compositor = wlr_compositor_create(dpy, 6, drw);
	wlr_subcompositor_create(dpy);
	wlr_data_device_manager_create(dpy);
	wlr_export_dmabuf_manager_v1_create(dpy);
	
	wlr_data_control_manager_v1_create(dpy);
	wlr_ext_data_control_manager_v1_create(dpy, 1);
	wlr_primary_selection_v1_device_manager_create(dpy);
	wlr_viewporter_create(dpy);
	wlr_single_pixel_buffer_manager_v1_create(dpy);
	wlr_fractional_scale_manager_v1_create(dpy, 1);
	wlr_presentation_create(dpy, backend, 2);
	wlr_alpha_modifier_v1_create(dpy);

	activation = wlr_xdg_activation_v1_create(dpy);
	wl_signal_add(&activation->events.request_activate, &request_activate);

	output_layout = wlr_output_layout_create(dpy);
	wl_signal_add(&output_layout->events.change, &layout_change);
  wlr_xdg_output_manager_v1_create(dpy, output_layout);
  wlr_screencopy_manager_v1_create(dpy);

	wl_signal_add(&backend->events.new_output, &new_output);

	wl_list_init(&clients);
	wl_list_init(&fstack);

  cursor_hide_timer = wl_event_loop_add_timer(event_loop, hidecursor, NULL);

	xdg_shell = wlr_xdg_shell_create(dpy, 6);
	wl_signal_add(&xdg_shell->events.new_toplevel, &new_xdg_toplevel);
	wl_signal_add(&xdg_shell->events.new_popup, &new_xdg_popup);

	layer_shell = wlr_layer_shell_v1_create(dpy, 3);
	wl_signal_add(&layer_shell->events.new_surface, &new_layer_surface);

	wlr_server_decoration_manager_set_default_mode(wlr_server_decoration_manager_create(dpy), WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
	xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(dpy);
	wl_signal_add(&xdg_decoration_mgr->events.new_toplevel_decoration, &new_xdg_decoration);

	pointer_constraints = wlr_pointer_constraints_v1_create(dpy);
	wl_signal_add(&pointer_constraints->events.new_constraint, &new_pointer_constraint);

	relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(dpy);

	cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(cursor, output_layout);
	cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	setenv("XCURSOR_SIZE", "24", 1);

	wl_signal_add(&cursor->events.motion, &cursor_motion);
	wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
	wl_signal_add(&cursor->events.button, &cursor_button);
	wl_signal_add(&cursor->events.axis, &cursor_axis);
	wl_signal_add(&cursor->events.frame, &cursor_frame);

	cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(dpy, 1);
	wl_signal_add(&cursor_shape_mgr->events.request_set_shape, &request_set_cursor_shape);

	wl_signal_add(&backend->events.new_input, &new_input_device);

	seat = wlr_seat_create(dpy, "seat0");
	wl_signal_add(&seat->events.request_set_cursor, &request_cursor);
	wl_signal_add(&seat->events.request_set_selection, &request_set_sel);
	wl_signal_add(&seat->events.request_set_primary_selection, &request_set_psel);
	wl_signal_add(&seat->events.request_start_drag, &request_start_drag);
	wl_signal_add(&seat->events.start_drag, &start_drag);

	kb_group = createkeyboardgroup();
	wl_list_init(&kb_group->destroy.link);

	unsetenv("DISPLAY");
}

void spawn(const Arg *arg) {
	if (fork() == 0) {
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		die("dwl: execvp %s failed:", ((char **)arg->v)[0]);
	}
}

void startdrag(struct wl_listener *listener, void *data) {
	struct wlr_drag *drag = data;
	if (!drag->icon) return;
	drag->icon->data = &wlr_scene_drag_icon_create(drag_icon, drag->icon)->node;
	LISTEN_STATIC(&drag->icon->events.destroy, destroydragicon);
}

void tag(const Arg *arg) {
	Client *sel = focustop(&monitor);
	if (!sel || (arg->ui & TAGMASK) == 0) return;
	sel->tags = arg->ui & TAGMASK;
	focusclient(focustop(&monitor), 1);
	arrange(&monitor);
	printstatus();
}

void tile(Monitor *m) {
	unsigned int mw, my, ty;
	int i, n = 0;
	Client *c;

	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen) n++;
	if (n == 0) return;

	if (n > m->nmaster) mw = m->nmaster ? (int)roundf(m->w.width * m->mfact) : 0;
	else mw = m->w.width;
	
	i = my = ty = 0;
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen) continue;
		if (i < m->nmaster) {
			resize(c, (struct wlr_box){.x = m->w.x, .y = m->w.y + my, .width = mw, .height = (m->w.height - my) / (MIN(n, m->nmaster) - i)}, 0);
			my += c->geom.height;
		} else {
			resize(c, (struct wlr_box){.x = m->w.x + mw, .y = m->w.y + ty, .width = m->w.width - mw, .height = (m->w.height - ty) / (n - i)}, 0);
			ty += c->geom.height;
		}
		i++;
	}
}

void togglefullscreen(const Arg *arg) {
	Client *sel = focustop(&monitor);
	if (sel) setfullscreen(sel, !sel->isfullscreen);
}

void toggletag(const Arg *arg) {
	uint32_t newtags;
	Client *sel = focustop(&monitor);
	if (!sel || !(newtags = sel->tags ^ (arg->ui & TAGMASK))) return;
	sel->tags = newtags;
	focusclient(focustop(&monitor), 1);
	arrange(&monitor);
	printstatus();
}

void toggleview(const Arg *arg) {
	uint32_t newtagset;
	if (!(newtagset = monitor.tagset[monitor.seltags] ^ (arg->ui & TAGMASK))) return;
	monitor.tagset[monitor.seltags] = newtagset;
	focusclient(focustop(&monitor), 1);
	arrange(&monitor);
	printstatus();
}

void unmaplayersurfacenotify(struct wl_listener *listener, void *data) {
	LayerSurface *l = wl_container_of(listener, l, unmap);
	l->mapped = 0;
	wlr_scene_node_set_enabled(&l->scene->node, 0);
	if (l == exclusive_focus) exclusive_focus = NULL;
	if (l->layer_surface->output) arrangelayers(&monitor);
	if (l->layer_surface->surface == seat->keyboard_state.focused_surface) focusclient(focustop(&monitor), 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void unmapnotify(struct wl_listener *listener, void *data) {
	Client *c = wl_container_of(listener, c, unmap);
	if (client_is_unmanaged(c)) {
		if (c == exclusive_focus) {
			exclusive_focus = NULL;
			focusclient(focustop(&monitor), 1);
		}
	} else {
		wl_list_remove(&c->link);
		c->mon = NULL;
		wl_list_remove(&c->flink);
    focusclient(focustop(&monitor), 1);
		arrange(&monitor);
	}
	wlr_scene_node_destroy(&c->scene->node);
	printstatus();
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void updatemons(struct wl_listener *listener, void *data) {
	Client *c;
	if (!monitor.wlr_output || !monitor.wlr_output->enabled) return;

	wlr_output_layout_get_box(output_layout, NULL, &sgeom);
	wlr_scene_node_set_position(&root_bg->node, sgeom.x, sgeom.y);
	wlr_scene_rect_set_size(root_bg, sgeom.width, sgeom.height);

	wlr_output_layout_get_box(output_layout, monitor.wlr_output, &monitor.m);
	monitor.w = monitor.m;
	wlr_scene_output_set_position(monitor.scene_output, monitor.m.x, monitor.m.y);
	wlr_scene_node_set_position(&monitor.fullscreen_bg->node, monitor.m.x, monitor.m.y);
	wlr_scene_rect_set_size(monitor.fullscreen_bg, monitor.m.width, monitor.m.height);

	arrangelayers(&monitor);
	arrange(&monitor);
	if ((c = focustop(&monitor)) && c->isfullscreen) resize(c, monitor.m, 0);

	monitor.gamma_lut_changed = 1;

	wl_list_for_each(c, &clients, link) {
		if (!c->mon && client_surface(c)->mapped) {
			c->mon = &monitor;
			c->tags = monitor.tagset[monitor.seltags];
		}
	}
	focusclient(focustop(&monitor), 1);
	wlr_cursor_move(cursor, NULL, 0, 0);
}

void updatetitle(struct wl_listener *listener, void *data) {
	Client *c = wl_container_of(listener, c, set_title);
	if (c == focustop(&monitor)) printstatus();
}

void urgent(struct wl_listener *listener, void *data) {
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	Client *c = NULL;
	toplevel_from_wlr_surface(event->surface, &c, NULL);
	if (!c || c == focustop(&monitor)) return;
	c->isurgent = 1;
	printstatus();
	if (client_surface(c)->mapped) client_set_border_color(c, urgentcolor);
}

void view(const Arg *arg) {
	if ((arg->ui & TAGMASK) == monitor.tagset[monitor.seltags]) return;
	monitor.seltags ^= 1;
	if (arg->ui & TAGMASK) monitor.tagset[monitor.seltags] = arg->ui & TAGMASK;
	focusclient(focustop(&monitor), 1);
	arrange(&monitor);
	printstatus();
}

void xytonode(double x, double y, struct wlr_surface **psurface, Client **pc, LayerSurface **pl, double *nx, double *ny) {
	struct wlr_scene_node *node, *pnode;
	struct wlr_surface *surface = NULL;
	Client *c = NULL;
	LayerSurface *l = NULL;
	int layer;

	for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
		if (!(node = wlr_scene_node_at(&layers[layer]->node, x, y, nx, ny))) continue;
		if (node->type == WLR_SCENE_NODE_BUFFER) surface = wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node))->surface;
		for (pnode = node; pnode && !c; pnode = &pnode->parent->node) c = pnode->data;
		if (c && c->type == LayerShell) {
			c = NULL;
			l = pnode->data;
		}
	}

	if (psurface) *psurface = surface;
	if (pc) *pc = c;
	if (pl) *pl = l;
}

int main(int argc, char *argv[]) {
	char *startup_cmd = NULL;
	int c;

	while ((c = getopt(argc, argv, "s:hdv")) != -1) {
		if (c == 's') startup_cmd = optarg;
		else if (c == 'd') log_level = WLR_DEBUG;
		else if (c == 'v') die("dwl minimal custom");
		else goto usage;
	}
	if (optind < argc) goto usage;
	if (!getenv("XDG_RUNTIME_DIR")) die("XDG_RUNTIME_DIR must be set");

	setup();
	autostart();
	run(startup_cmd);
	cleanup();
	return EXIT_SUCCESS;

usage:
	die("Usage: %s [-v] [-d] [-s startup command]", argv[0]);
}
