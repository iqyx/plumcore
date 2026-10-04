/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Settings applet
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include <interfaces/applet.h>
#include <interfaces/event.h>
#include <interfaces/fb.h>
#include <interfaces/painter.h>
#include <interfaces/conf.h>
#include <interfaces/servicelocator.h>
#include <configlib.h>
#include <services/fb-painter/fb-painter.h>

/* The applet requires the fb-painter service (see its Kconfig depends), so its assets are always available. */
#include <services/fb-painter/assets/assets.h>

#define MODULE_NAME "settings"

/* Height of the location bar drawn along the top edge of the window, the button panel drawn along the bottom
 * edge and of a single tree row, all in pixels. The tree indents each subtree level by SETTINGS_INDENT pixels
 * and reserves SETTINGS_SCROLLBAR_W pixels along the right edge for the scroll bar. */
#define SETTINGS_LOCBAR_H 11
#define SETTINGS_BTNBAR_H 11
#define SETTINGS_ROW_H 12
#define SETTINGS_INDENT 16
#define SETTINGS_SCROLLBAR_W 8

/* Height of a line of text in the painter's font, used to vertically centre row content and the location. */
#define SETTINGS_TEXT_H 8

/* Horizontal offset of a level's guide line (and the centre of its expand/collapse icon) within the indent
 * cell, in pixels. */
#define SETTINGS_GUIDE_OFF 7

/* Size of the transient buffers a redraw uses to render one line of text (a node name, a formatted value
 * or the location path). Anything longer is cropped to the display width anyway, so this bounds only a
 * single on-screen line, never the tree. */
#define SETTINGS_LINE_BUF 64


/* One node of the configuration forest, materialised on the C stack only while it is being visited during
 * a walk. The whole tree is never held in memory: each walk recomputes it, and a node knows its ancestors
 * through @p parent (used to draw the tree guide lines and to build the location path). Names are not
 * copied but referenced in place: for a real Conf node @p name is the stable name from stat, for a virtual
 * service-locator mount node it points at the relevant component of a locator name, hence the explicit
 * length as the substring is not terminated. */
struct settings_node {
	const struct settings_node *parent;
	Conf *conf;                      /* backing Conf node, or NULL for a virtual mount node */
	const char *name;
	size_t name_len;
	uint16_t depth;                  /* indent level, 0 for a top-level node */
	bool last;                       /* true when this node is the last child of its parent */
	bool subtree;                    /* true for a subtree node (drawn with an expand icon) */
	bool has_children;               /* true when the subtree actually has children */
	bool collapsed;                  /* true for a subtree whose children are hidden (not on the active path) */
	enum conf_type type;
	enum conf_flag flags;
};

typedef struct settings_applet {
	Fb *fb;                          /* window framebuffer the applet draws into */
	Event *event;                    /* input event source delivering key presses */
	Window *window;                  /* window the applet runs in (may be NULL), used to set the title */
	FbPainter painter;               /* painter bound to the framebuffer */
	uint16_t w;                      /* framebuffer dimensions in pixels */
	uint16_t h;
	int16_t text_y;                  /* vertical offset centring a line of text within a row */
	size_t selected;                 /* index of the highlighted row in the flattened tree order */
	size_t list_top;                 /* index of the first row visible in the content area */

	/* The single expanded branch (accordion behaviour): the slash-separated path of the deepest open
	 * subtree. A subtree is expanded iff it lies on this path; every other subtree is collapsed. NULL means
	 * nothing is expanded, so only the top-level nodes show. Owned, reallocated on each toggle. */
	char *active;
} SettingsApplet;


/* A visitor called once per node during a walk, in the flattened depth-first tree order. Returning false
 * stops the walk early (used to abandon it once the visible window has been drawn). */
typedef bool (*settings_visit_fn)(SettingsApplet *self, const struct settings_node *node, void *ctx);


/***********************************************************************************************************************
 * Service-locator name component helpers
 *
 * Conf instances are advertised under space-delimited names (e.g. "system memory"), each component of
 * which becomes one level of the virtual mount tree above the instance's own Conf subtree.
 **********************************************************************************************************************/

/* Return the @p idx-th space-delimited component of @p name via @p start / @p len (not terminated).
 * Returns false when @p name has no such component. */
static bool settings_comp_at(const char *name, size_t idx, const char **start, size_t *len) {
	const char *p = name;
	for (size_t j = 0; ; j++) {
		const char *s = p;
		while (*p != '\0' && *p != ' ') {
			p++;
		}
		if (j == idx) {
			*start = s;
			*len = (size_t)(p - s);
			return *len > 0;
		}
		if (*p == '\0') {
			return false;
		}
		p++;
	}
}


/* Compare two name components (@p a / @p alen against @p b / @p blen), returning <0, 0 or >0 like strcmp. */
static int settings_comp_cmp(const char *a, size_t alen, const char *b, size_t blen) {
	size_t n = (alen < blen) ? alen : blen;
	int c = memcmp(a, b, n);
	if (c != 0) {
		return c;
	}
	if (alen != blen) {
		return (alen < blen) ? -1 : 1;
	}
	return 0;
}


/* Test whether the space-delimited components of @p name equal the mount-node chain ending at @p chain,
 * i.e. name[0..chain->depth] component-wise match the chain from its root down. */
static bool settings_chain_match(const struct settings_node *chain, const char *name) {
	if (chain == NULL) {
		return true;
	}
	if (!settings_chain_match(chain->parent, name)) {
		return false;
	}
	const char *c = NULL;
	size_t l = 0;
	if (!settings_comp_at(name, chain->depth, &c, &l)) {
		return false;
	}
	return settings_comp_cmp(c, l, chain->name, chain->name_len) == 0;
}


/* Among the advertised Conf names whose first @p level components equal @p parent's chain, pick the smallest
 * component at index @p level that is strictly greater than @p ref (NULL selects the first one), returning it
 * in place via @p out / @p outlen. Returns false when the level has been fully enumerated. */
static bool settings_pick(const struct settings_node *parent, uint16_t level, const char *ref, size_t reflen,
                          const char **out, size_t *outlen) {
	bool found = false;
	Interface *iface = NULL;
	for (size_t i = 0; iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CONF, i, &iface) ==
	     ISERVICELOCATOR_RET_OK; i++) {
		const char *name = NULL;
		if (iservicelocator_get_name(locator, iface, &name) != ISERVICELOCATOR_RET_OK || name == NULL) {
			continue;
		}
		if (!settings_chain_match(parent, name)) {
			continue;
		}
		const char *c = NULL;
		size_t l = 0;
		if (!settings_comp_at(name, level, &c, &l)) {
			continue;
		}
		if (ref != NULL && settings_comp_cmp(c, l, ref, reflen) <= 0) {
			continue;
		}
		if (found && settings_comp_cmp(c, l, *out, *outlen) >= 0) {
			continue;
		}
		*out = c;
		*outlen = l;
		found = true;
	}
	return found;
}


/* Return the Conf instance mounted exactly at @p node's chain (a name of exactly node->depth+1 components),
 * or NULL when the mount point is a purely virtual node with no backing instance. */
static Conf *settings_exact(const struct settings_node *node) {
	Interface *iface = NULL;
	for (size_t i = 0; iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CONF, i, &iface) ==
	     ISERVICELOCATOR_RET_OK; i++) {
		const char *name = NULL;
		if (iservicelocator_get_name(locator, iface, &name) != ISERVICELOCATOR_RET_OK || name == NULL) {
			continue;
		}
		const char *c = NULL;
		size_t l = 0;
		if (settings_chain_match(node, name) && !settings_comp_at(name, (size_t)node->depth + 1, &c, &l)) {
			return (Conf *)iface;
		}
	}
	return NULL;
}


/* Test whether any advertised name has a further mount component below @p node's chain, i.e. the node has
 * virtual children of its own. */
static bool settings_has_deeper(const struct settings_node *node) {
	Interface *iface = NULL;
	for (size_t i = 0; iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CONF, i, &iface) ==
	     ISERVICELOCATOR_RET_OK; i++) {
		const char *name = NULL;
		if (iservicelocator_get_name(locator, iface, &name) != ISERVICELOCATOR_RET_OK || name == NULL) {
			continue;
		}
		const char *c = NULL;
		size_t l = 0;
		if (settings_chain_match(node, name) && settings_comp_at(name, (size_t)node->depth + 1, &c, &l)) {
			return true;
		}
	}
	return false;
}


/* Test whether the Conf node @p parent has at least one child. */
static bool settings_conf_has_children(Conf *parent) {
	Conf *child = NULL;
	return parent->vmt->walk != NULL &&
	       parent->vmt->walk(parent, CONF_DIR_CHILD, &child) == CONF_RET_OK && child != NULL;
}


/* Append @p node's slash-separated path ("/system/memory") into @p buf by recursing to the root first. */
static size_t settings_append_path(const struct settings_node *node, char *buf, size_t size) {
	if (node == NULL) {
		buf[0] = '\0';
		return 0;
	}
	size_t off = settings_append_path(node->parent, buf, size);
	off += (size_t)snprintf(buf + off, (off < size) ? size - off : 0, "/%.*s",
	                        (int)node->name_len, node->name);
	return off;
}


/* Test whether @p node lies on the active (expanded) path, i.e. it is the open subtree or an ancestor of it.
 * Expansion is derived from the single active path rather than stored per node, so only that one branch is
 * ever open. */
static bool settings_is_expanded(SettingsApplet *self, const struct settings_node *node) {
	if (self->active == NULL) {
		return false;
	}
	char path[SETTINGS_LINE_BUF];
	settings_append_path(node, path, sizeof(path));
	size_t l = strlen(path);
	/* node's path must be a whole-component prefix of the active path (so "/sys" does not match "/system"). */
	return strncmp(self->active, path, l) == 0 && (self->active[l] == '\0' || self->active[l] == '/');
}


/***********************************************************************************************************************
 * Lazy tree walk
 *
 * The forest is never materialised. A walk recurses through the virtual service-locator mount tree and the
 * real Conf subtrees below it, in the same order the display shows, invoking the visitor per node. Each node
 * lives only in a stack frame for the duration of its subtree, so a walk needs no heap and no fixed limits.
 **********************************************************************************************************************/

static bool settings_walk_virtual(SettingsApplet *self, const struct settings_node *parent, uint16_t level,
                                  settings_visit_fn cb, void *ctx);


/* Walk the real Conf subtree below @p conf, whose display node is @p parent, invoking @p cb per node. */
static bool settings_walk_real(SettingsApplet *self, Conf *conf, const struct settings_node *parent,
                              settings_visit_fn cb, void *ctx) {
	if (conf->vmt->walk == NULL) {
		return true;
	}
	Conf *child = NULL;
	if (conf->vmt->walk(conf, CONF_DIR_CHILD, &child) != CONF_RET_OK) {
		return true;
	}
	while (child != NULL) {
		Conf *next = NULL;
		if (child->vmt->walk(child, CONF_DIR_NEXT, &next) != CONF_RET_OK) {
			next = NULL;
		}
		const char *name = NULL;
		enum conf_type type = CONF_NONE;
		enum conf_flag flags = 0;
		if (child->vmt->stat != NULL && child->vmt->stat(child, &name, &type, &flags) == CONF_RET_OK) {
			struct settings_node node = {
				.parent = parent,
				.conf = child,
				.name = (name != NULL) ? name : "",
				.name_len = (name != NULL) ? strlen(name) : 0,
				.depth = (uint16_t)(parent->depth + 1),
				.last = (next == NULL),
				.subtree = (type == CONF_SUBTREE),
				.type = type,
				.flags = flags,
			};
			node.has_children = node.subtree && settings_conf_has_children(child);
			node.collapsed = node.has_children && !settings_is_expanded(self, &node);
			if (!cb(self, &node, ctx)) {
				return false;
			}
			if (node.has_children && !node.collapsed &&
			    !settings_walk_real(self, child, &node, cb, ctx)) {
				return false;
			}
		}
		child = next;
	}
	return true;
}


/* Walk the virtual mount-tree components at @p level below @p parent (NULL, level 0 for the forest root),
 * invoking @p cb per node and descending into the backing Conf subtree or the deeper virtual level. */
static bool settings_walk_virtual(SettingsApplet *self, const struct settings_node *parent, uint16_t level,
                                  settings_visit_fn cb, void *ctx) {
	const char *ref = NULL;
	size_t reflen = 0;
	const char *comp = NULL;
	size_t complen = 0;
	while (settings_pick(parent, level, ref, reflen, &comp, &complen)) {
		const char *peek = NULL;
		size_t peeklen = 0;
		struct settings_node node = {
			.parent = parent,
			.conf = NULL,
			.name = comp,
			.name_len = complen,
			.depth = level,
			.last = !settings_pick(parent, level, comp, complen, &peek, &peeklen),
			.subtree = true,
			.type = CONF_SUBTREE,
			.flags = 0,
		};
		Conf *exact = settings_exact(&node);
		bool deeper = settings_has_deeper(&node);
		node.has_children = deeper || (exact != NULL && settings_conf_has_children(exact));
		node.collapsed = node.has_children && !settings_is_expanded(self, &node);

		if (!cb(self, &node, ctx)) {
			return false;
		}
		if (node.has_children && !node.collapsed) {
			if (exact != NULL) {
				if (!settings_walk_real(self, exact, &node, cb, ctx)) {
					return false;
				}
			} else if (deeper) {
				if (!settings_walk_virtual(self, &node, (uint16_t)(level + 1), cb, ctx)) {
					return false;
				}
			}
		}

		ref = comp;
		reflen = complen;
	}
	return true;
}


/* Walk the whole configuration forest in display order. */
static bool settings_walk(SettingsApplet *self, settings_visit_fn cb, void *ctx) {
	return settings_walk_virtual(self, NULL, 0, cb, ctx);
}


/* Count all rows in the tree by walking it and counting the visits. */
static bool settings_count_visit(SettingsApplet *self, const struct settings_node *node, void *ctx) {
	(void)self;
	(void)node;
	(*(size_t *)ctx)++;
	return true;
}


static size_t settings_count(SettingsApplet *self) {
	size_t n = 0;
	settings_walk(self, settings_count_visit, &n);
	return n;
}


/* Toggle @p node's expansion. Expanding makes its path the single active branch (opening it and its
 * ancestors, closing every other branch); collapsing falls back to its parent path so the parent stays
 * open. The active path is reallocated to match. */
static void settings_toggle(SettingsApplet *self, const struct settings_node *node) {
	char path[SETTINGS_LINE_BUF];
	settings_append_path(node, path, sizeof(path));
	bool expanded = settings_is_expanded(self, node);

	free(self->active);
	self->active = NULL;

	if (expanded) {
		/* Collapse: keep the parent open, or nothing when collapsing a top-level node. */
		char *slash = strrchr(path, '/');
		if (slash != NULL && slash != path) {
			*slash = '\0';
		} else {
			path[0] = '\0';
		}
	}
	if (path[0] != '\0') {
		size_t len = strlen(path);
		self->active = malloc(len + 1);
		if (self->active != NULL) {
			memcpy(self->active, path, len + 1);
		}
	}
}


/* Walk context locating the selected row so its subtree can be toggled. */
struct settings_toggle_ctx {
	size_t index;
	bool done;
};


static bool settings_toggle_visit(SettingsApplet *self, const struct settings_node *node, void *ctx) {
	struct settings_toggle_ctx *t = ctx;
	if (t->index++ != self->selected) {
		return true;
	}
	if (node->subtree && node->has_children) {
		settings_toggle(self, node);
		t->done = true;
	}
	return false;
}


/***********************************************************************************************************************
 * Rendering
 **********************************************************************************************************************/

/* Height of the tree content area between the location bar and the button panel, in pixels. */
static uint16_t settings_content_h(SettingsApplet *self) {
	return (uint16_t)(self->h - SETTINGS_LOCBAR_H - SETTINGS_BTNBAR_H);
}


/* Number of tree rows that fit in the content area at once. */
static size_t settings_visible_rows(SettingsApplet *self) {
	return settings_content_h(self) / SETTINGS_ROW_H;
}


/* Horizontal position of level @p level's guide line (and expand-icon centre) within the content area. */
static int16_t settings_guide_x(uint16_t level) {
	return (int16_t)(level * SETTINGS_INDENT + SETTINGS_GUIDE_OFF);
}


/* Draw the tree guide lines for one node: the full-height guide lines of continuing ancestors, the connector
 * joining this node to its parent (a leaf dash plus the vertical stub), and, for an expanded subtree, the
 * line dropping from its icon to its children. @p color lets a highlighted row draw its lines in black. The
 * caller must have an active painter frame. */
static void settings_draw_guides(SettingsApplet *self, const struct settings_node *node, int16_t y,
                                 painter_color_t color) {
	Painter *painter = &self->painter.painter;
	int16_t yc = (int16_t)(y + SETTINGS_ROW_H / 2);
	int16_t y_bottom = (int16_t)(y + SETTINGS_ROW_H);

	painter->vmt->set_pen(painter, color, 1);

	/* Full-height guide lines of ancestors that still have siblings below this row. */
	for (const struct settings_node *a = node->parent; a != NULL; a = a->parent) {
		if (a->depth >= 1 && !a->last) {
			int16_t gx = settings_guide_x((uint16_t)(a->depth - 1));
			painter->vmt->line(painter, gx, y, gx, y_bottom);
		}
	}

	/* Connector to the parent: a vertical stub down to the row centre (and on to the bottom when this node
	 * has following siblings) plus a horizontal dash reaching the node. */
	if (node->depth >= 1) {
		int16_t gx = settings_guide_x((uint16_t)(node->depth - 1));
		painter->vmt->line(painter, gx, y, gx, yc);
		if (!node->last) {
			painter->vmt->line(painter, gx, yc, gx, y_bottom);
		}
		painter->vmt->line(painter, gx, yc, (int16_t)(node->depth * SETTINGS_INDENT + 4), yc);
	}

	/* Expanded subtree: the line dropping from its icon down to its first child's connector. A collapsed
	 * subtree has no visible children, so no such line. */
	if (node->subtree && node->has_children && !node->collapsed) {
		int16_t gx = settings_guide_x(node->depth);
		painter->vmt->line(painter, gx, yc, gx, y_bottom);
	}
}


/* Draw the expand/collapse icon of a subtree, centred on the level's guide column: the tree-minus asset when
 * expanded, the tree-plus asset when collapsed. The assets are black-on-white, so they are blitted inverted
 * on a normal (black) row and straight on the highlighted (white) row; the icon's own opaque background hides
 * the guide line that runs beneath it. The caller must have an active painter frame. */
static void settings_draw_icon(SettingsApplet *self, const struct settings_node *node, int16_t y, bool selected) {
	Painter *painter = &self->painter.painter;
	const struct painter_raw_image *icon = node->collapsed ? &tree_plus_data : &tree_minus_data;
	int16_t gx = settings_guide_x(node->depth);
	int16_t yc = (int16_t)(y + SETTINGS_ROW_H / 2);

	painter->vmt->image(painter, (int16_t)(gx - (int16_t)icon->w / 2), (int16_t)(yc - (int16_t)icon->h / 2),
	                   icon, selected ? PAINTER_MODE_NORMAL : PAINTER_MODE_INVERTED);
}


/* Draw a single tree row at screen position @p y: the highlight for the selected row, its guide lines and
 * expand icon, its name cropped to the space left of the value, and the readable value right-aligned before
 * the scroll bar. The caller must have an active painter frame. */
static void settings_draw_row(SettingsApplet *self, const struct settings_node *node, size_t index, int16_t y) {
	Painter *painter = &self->painter.painter;
	uint16_t content_w = (uint16_t)(self->w - SETTINGS_SCROLLBAR_W);
	bool selected = (index == self->selected);
	painter_color_t fg = selected ? 0xff000000 : 0xffffffff;

	if (selected) {
		painter->vmt->set_pen(painter, 0xffffffff, 0);
		painter->vmt->set_brush(painter, 0xffffffff);
		painter->vmt->rect(painter, 0, y, content_w, SETTINGS_ROW_H);
	}

	settings_draw_guides(self, node, y, fg);
	if (node->subtree && node->has_children) {
		settings_draw_icon(self, node, y, selected);
	}

	painter->vmt->set_font(painter, PAINTER_FONT_NORMAL, NULL);
	painter->vmt->set_pen(painter, fg, 1);

	int16_t name_x = (int16_t)(node->depth * SETTINGS_INDENT + SETTINGS_INDENT);

	/* Read and format a readable leaf value, then reserve room for it on the right and crop the name to fit. */
	char value[SETTINGS_LINE_BUF];
	value[0] = '\0';
	if (node->conf != NULL && node->conf->vmt->read != NULL && (node->flags & CONF_READ) != 0) {
		configlib_value_str(node->conf, node->type, value, sizeof(value));
	}
	uint16_t value_w = 0;
	if (value[0] != '\0') {
		painter->vmt->text_size(painter, value, &value_w, NULL);
	}
	int16_t value_x = (int16_t)(content_w - value_w - 2);

	char name[SETTINGS_LINE_BUF];
	size_t nl = (node->name_len < sizeof(name) - 1) ? node->name_len : sizeof(name) - 1;
	memcpy(name, node->name, nl);
	name[nl] = '\0';
	uint16_t name_w = (value_w > 0) ? (uint16_t)(value_x - name_x - 4) : (uint16_t)(content_w - name_x - 2);
	fb_painter_crop_text(&self->painter, name, name_w);
	painter->vmt->text(painter, name_x, (int16_t)(y + self->text_y), name);

	if (value[0] != '\0') {
		painter->vmt->text(painter, value_x, (int16_t)(y + self->text_y), value);
	}
}


/* Draw the location bar along the top edge of the window: @p path as black text on a light grey background,
 * which sets it apart from the black tree area below. The caller must have an active painter frame. */
static void settings_draw_locbar(SettingsApplet *self, const char *path) {
	Painter *painter = &self->painter.painter;

	painter->vmt->set_pen(painter, 0xff444444, 0);
	painter->vmt->set_brush(painter, 0xff444444);
	painter->vmt->rect(painter, 0, 0, self->w, SETTINGS_LOCBAR_H);

	painter->vmt->set_font(painter, PAINTER_FONT_BOLD, NULL);
	painter->vmt->set_pen(painter, 0xffffffff, 1);

	/* The "Location: " label is drawn first, then the path is cropped to and drawn in the space left of it. */
	uint16_t label_w = 0;
	painter->vmt->text_size(painter, "Location: ", &label_w, NULL);
	painter->vmt->text(painter, 2, self->text_y, "Location: ");

	int16_t path_x = (int16_t)(2 + label_w + 2);
	char buf[SETTINGS_LINE_BUF];
	strncpy(buf, path, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	fb_painter_crop_text(&self->painter, buf, (uint16_t)(self->w - path_x - 2));
	painter->vmt->text(painter, path_x, self->text_y, buf);
}


/* Default function-key mapping shown in the button panel: F1 opens the details, F2 collapses the whole tree
 * and F4 saves, with F3 left unassigned. An empty string leaves that key's cell blank. */
static const char *settings_default_buttons[4] = {
	"Details",
	"Collapse",
	"",
	"Save",
};


/* Draw the button panel along the bottom edge of the window, matching the location bar's light grey
 * background with no per-button rectangles. The width is split into four equal cells, one per function key,
 * each holding "F<n>: <label>" centred in its cell; a cell whose label is empty is left blank. The caller
 * must have an active painter frame. */
static void settings_draw_btnbar(SettingsApplet *self, const char *buttons[4]) {
	Painter *painter = &self->painter.painter;
	int16_t bar_y = (int16_t)(self->h - SETTINGS_BTNBAR_H);
	uint16_t cell_w = (uint16_t)(self->w / 4);

	painter->vmt->set_pen(painter, 0xff444444, 0);
	painter->vmt->set_brush(painter, 0xff444444);
	painter->vmt->rect(painter, 0, bar_y, self->w, SETTINGS_BTNBAR_H);

	painter->vmt->set_font(painter, PAINTER_FONT_BOLD, NULL);
	painter->vmt->set_pen(painter, 0xffffffff, 1);

	for (int i = 0; i < 4; i++) {
		if (buttons[i] == NULL || buttons[i][0] == '\0') {
			continue;
		}
		char label[SETTINGS_LINE_BUF];
		snprintf(label, sizeof(label), "F%d: %s", i + 1, buttons[i]);
		uint16_t label_w = 0;
		painter->vmt->text_size(painter, label, &label_w, NULL);
		int16_t cell_x = (int16_t)(i * cell_w);
		int16_t label_x = (int16_t)(cell_x + (cell_w - label_w) / 2);
		painter->vmt->text(painter, label_x, (int16_t)(bar_y + self->text_y), label);
	}
}


/* Draw the vertical scroll bar along the right edge of the content area, exactly like the live-data one: a
 * black-filled, white-bordered track with a white-filled scroller sized and positioned to reflect how much
 * of the tree is visible and how far it is scrolled. The caller must have an active painter frame. */
static void settings_draw_scrollbar(SettingsApplet *self, size_t total) {
	Painter *painter = &self->painter.painter;
	int16_t bar_x = (int16_t)(self->w - SETTINGS_SCROLLBAR_W);
	int16_t bar_y = SETTINGS_LOCBAR_H;
	uint16_t bar_h = settings_content_h(self);

	painter->vmt->set_pen(painter, 0xffffffff, 1);
	painter->vmt->set_brush(painter, 0xff000000);
	painter->vmt->rect(painter, bar_x, bar_y, SETTINGS_SCROLLBAR_W, bar_h);

	int32_t content_h = (int32_t)total * SETTINGS_ROW_H;
	int32_t inner_h = (int32_t)bar_h - 2;
	int32_t y_scroll = (int32_t)self->list_top * SETTINGS_ROW_H;

	int32_t thumb_h = inner_h;
	int32_t thumb_y = 1;
	if (content_h > bar_h) {
		thumb_h = inner_h * bar_h / content_h;
		thumb_y = 1 + (inner_h - thumb_h) * y_scroll / (content_h - bar_h);
	}

	painter->vmt->set_pen(painter, 0xffffffff, 0);
	painter->vmt->set_brush(painter, 0xffffffff);
	painter->vmt->rect(painter, (int16_t)(bar_x + 1), (int16_t)(bar_y + thumb_y),
	                   (uint16_t)(SETTINGS_SCROLLBAR_W - 2), (uint16_t)thumb_h);
}


/* Redraw context: tracks the running row index across the walk, draws the ones inside the visible window and
 * captures the highlighted row's location path (the selection is always kept in view). */
struct settings_render {
	size_t index;
	size_t visible;
	bool have_path;
	char path[SETTINGS_LINE_BUF];
};


static bool settings_render_visit(SettingsApplet *self, const struct settings_node *node, void *ctx) {
	struct settings_render *r = ctx;
	size_t i = r->index++;

	if (i == self->selected) {
		/* The location is the subtree the selection sits in: an open subtree's own path, otherwise its
		 * parent's (so a collapsed or leaf row shows the enclosing subtree, and a top-level one shows "/"). */
		bool open = node->subtree && node->has_children && !node->collapsed;
		settings_append_path(open ? node : node->parent, r->path, sizeof(r->path));
		if (r->path[0] == '\0') {
			r->path[0] = '/';
			r->path[1] = '\0';
		}
		r->have_path = true;
	}
	if (i < self->list_top) {
		return true;
	}
	size_t row = i - self->list_top;
	if (row >= r->visible) {
		/* Past the visible window; the selection is always within it, so nothing more is needed. */
		return false;
	}
	settings_draw_row(self, node, i, (int16_t)(SETTINGS_LOCBAR_H + row * SETTINGS_ROW_H));
	return true;
}


/* Fully repaint the window: the visible slice of the tree, the scroll bar and the location bar. */
static void settings_redraw(SettingsApplet *self) {
	Painter *painter = &self->painter.painter;

	painter->vmt->begin(painter);

	/* Clear the content area below the location bar. */
	painter->vmt->set_pen(painter, 0xff000000, 0);
	painter->vmt->set_brush(painter, 0xff000000);
	painter->vmt->rect(painter, 0, SETTINGS_LOCBAR_H, self->w, settings_content_h(self));

	struct settings_render r = {0};
	r.visible = settings_visible_rows(self);
	settings_walk(self, settings_render_visit, &r);

	settings_draw_scrollbar(self, r.index);
	settings_draw_locbar(self, r.have_path ? r.path : "/");
	settings_draw_btnbar(self, settings_default_buttons);

	painter->vmt->end(painter);
}


/***********************************************************************************************************************
 * Input handling and lifecycle
 **********************************************************************************************************************/

/* Move the list cursor by @p delta rows, clamped to the tree, scrolling list_top just enough to keep the
 * newly selected row in view. */
static void settings_move_selection(SettingsApplet *self, int32_t delta) {
	size_t total = settings_count(self);
	if (total == 0) {
		return;
	}
	int32_t target = (int32_t)self->selected + delta;
	if (target < 0) {
		target = 0;
	}
	if (target >= (int32_t)total) {
		target = (int32_t)total - 1;
	}
	if ((size_t)target == self->selected) {
		return;
	}
	self->selected = (size_t)target;

	size_t visible = settings_visible_rows(self);
	if (self->selected < self->list_top) {
		self->list_top = self->selected;
	}
	if (visible > 0 && self->selected >= self->list_top + visible) {
		self->list_top = self->selected - visible + 1;
	}
}


/* Block for and process a single input event: the up/down keys and a relative REL_X count move the cursor,
 * ESC quits. Returns false when the user requested to quit, true to keep running. */
static bool settings_process_event(SettingsApplet *self) {
	enum event_type type = EV_TYPE_NONE;
	enum event_code code = EV_CODE_NONE;
	int32_t value = 0;
	if (self->event->vmt->listen(self->event, &type, &code, &value) != EV_RET_OK) {
		return true;
	}

	if (type == EV_TYPE_REL && code == EV_REL_X) {
		settings_move_selection(self, value);
		settings_redraw(self);
		return true;
	}

	if (type != EV_TYPE_KEY || value != 1) {
		return true;
	}

	switch (code) {
		case EV_KEY_ESC:
			return false;
		case EV_KEY_UP:
			settings_move_selection(self, -1);
			settings_redraw(self);
			break;
		case EV_KEY_DOWN:
			settings_move_selection(self, 1);
			settings_redraw(self);
			break;
		case EV_KEY_ENTER: {
			/* Toggle the highlighted subtree. Collapsing only removes rows below it, so the selection
			 * index keeps pointing at the same node and stays valid. */
			struct settings_toggle_ctx t = {0};
			settings_walk(self, settings_toggle_visit, &t);
			if (t.done) {
				settings_redraw(self);
			}
			break;
		}
		default:
			/* passthrough */
	}
	return true;
}


/* Set up the applet instance: bind the framebuffer and event source, start the painter, capture the
 * framebuffer dimensions and text metrics and set the window title. */
static applet_ret_t settings_init(SettingsApplet *self, struct applet_args *args) {
	memset(self, 0, sizeof(SettingsApplet));

	if (args->fb == NULL || args->event == NULL) {
		return APPLET_RET_FAILED;
	}
	self->fb = args->fb;
	self->event = args->event;
	self->window = args->window;

	struct fb_stat stat = {0};
	if (self->fb->vmt->stat(self->fb, &stat) != FB_RET_OK) {
		return APPLET_RET_FAILED;
	}
	self->w = (uint16_t)stat.w;
	self->h = (uint16_t)stat.h;

	if (fb_painter_init(&self->painter, self->fb) != FB_PAINTER_RET_OK) {
		return APPLET_RET_FAILED;
	}

	/* Vertical offset centring a line of the font within a row. */
	self->text_y = (int16_t)((SETTINGS_ROW_H - SETTINGS_TEXT_H) / 2);

	if (self->window != NULL) {
		self->window->vmt->set_title(self->window, "Settings");
	}

	return APPLET_RET_OK;
}


static void settings_free(SettingsApplet *self) {
	free(self->active);
	self->active = NULL;
	fb_painter_free(&self->painter);
}


static applet_ret_t settings_main(Applet *self, struct applet_args *args) {
	(void)self;
	if (args->logger != NULL) {
		u_log(args->logger, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("settings applet started"));
	}

	SettingsApplet app;
	if (settings_init(&app, args) != APPLET_RET_OK) {
		return APPLET_RET_FAILED;
	}
	settings_redraw(&app);

	while (settings_process_event(&app)) {
		/* Keep processing input until the user quits. */
	}

	settings_free(&app);
	return APPLET_RET_OK;
}


const Applet settings = {
	.executable.native = {
		.main = settings_main
	},
	.name = "Settings",
	.help = "View and edit device settings",
	.stack_size = 1536,
	.icon = &settings_data,
};
