#include "automap_plugin.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DataEntry exports_gams_automap_automap_data_entry_t
#define DataList automap_plugin_list_data_entry_t
#define WitLayer exports_gams_automap_automap_tile_layer_t
#define WitMap exports_gams_automap_automap_tile_map_t
#define Str automap_plugin_string_t
#define U32List automap_plugin_list_u32_t

typedef struct {
  int x, y;
} Point;
typedef struct {
  Point p;
  uint32_t value;
} Tile;
typedef struct {
  Tile *items;
  size_t len, cap;
} TileVec;

typedef struct {
  DataEntry *ptr;
  size_t len;
} Props;
typedef struct {
  uint32_t width;
  uint32_t *data;
  size_t data_len;
  Props props;
} Layer;
typedef struct {
  Layer *layers;
  size_t len;
  Props props;
} Map;

typedef struct {
  uint32_t empty, non_empty, other, ignore, negate, different, same;
  bool match_outside, overflow_border, wrap_border, no_overlap, delete_tiles;
  int mod_x, mod_y, offset_x, offset_y;
  double probability;
} Config;

typedef struct {
  uint32_t value;
  bool negated;
} Matcher;
typedef struct {
  Point p;
  Matcher *matchers;
  size_t len, cap;
} InputCell;
typedef struct {
  char *selector;
  char *input_index;
  InputCell *cells;
  size_t len, cap;
  bool has_empty_matcher;
} InputGroup;

typedef struct {
  Tile *tiles;
  size_t len;
  char *selector;
  char *input_index;
  bool negated;
} InputLayer;

typedef struct {
  Tile *tiles;
  size_t len;
  char *selector;
  char *output_index;
  double probability;
  Props props;
} OutputLayer;

typedef struct {
  char *index;
  double probability;
  OutputLayer **layers;
  size_t len, cap;
} OutputVariant;
typedef struct {
  OutputLayer **always;
  size_t always_len, always_cap;
  OutputVariant *variants;
  size_t var_len, var_cap;
} RuleOutputs;

typedef struct {
  InputGroup *groups;
  size_t group_len;
  RuleOutputs outputs;
  int mod_x, mod_y, offset_x, offset_y, order_x, order_y;
  double probability;
  Config *config;
} Rule;

typedef struct {
  Rule *items;
  size_t len, cap;
} RuleVec;
typedef struct {
  int original_w, original_h, pad_x, pad_y;
  bool extended;
} EdgeCtx;
typedef struct {
  char *selector;
  int x, y;
} Occ;
typedef struct {
  Occ *items;
  size_t len, cap;
} OccVec;

static void fail_oom(void) { abort(); }
static void *xcalloc(size_t n, size_t s) {
  void *p = calloc(n ? n : 1, s ? s : 1);
  if (!p)
    fail_oom();
  return p;
}
static void *xrealloc(void *p, size_t n) {
  void *r = realloc(p, n ? n : 1);
  if (!r)
    fail_oom();
  return r;
}
static char *xstrndup_u8(const uint8_t *p, size_t n) {
  char *s = xcalloc(n + 1, 1);
  if (n)
    memcpy(s, p, n);
  return s;
}
static char *xstrdup0(const char *s) {
  return xstrndup_u8((const uint8_t *)s, strlen(s));
}
static bool streq(const char *a, const char *b) {
  return strcmp(a ? a : "", b ? b : "") == 0;
}

static void set_err(Str *err, const char *msg) {
  automap_plugin_string_dup(err, msg);
}
static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }
static bool starts_with(const char *s, const char *p) {
  return strncmp(s, p, strlen(p)) == 0;
}
static char *trim_copy(const char *s) {
  while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
    s++;
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' ||
               s[n - 1] == '\r'))
    n--;
  return xstrndup_u8((const uint8_t *)s, n);
}

static bool str_eq_wit_lit(Str *s, const char *lit) {
  size_t n = strlen(lit);
  return s->len == n && memcmp(s->ptr, lit, n) == 0;
}
static char *wit_to_cstr(Str *s) { return xstrndup_u8(s->ptr, s->len); }
static const char *prop_get(Props *p, const char *key) {
  for (size_t i = 0; i < p->len; i++)
    if (str_eq_wit_lit(&p->ptr[i].f0, key))
      return (const char *)xstrndup_u8(p->ptr[i].f1.ptr, p->ptr[i].f1.len);
  return "";
}
static bool prop_has(Props *p, const char *key) {
  for (size_t i = 0; i < p->len; i++)
    if (str_eq_wit_lit(&p->ptr[i].f0, key))
      return true;
  return false;
}
static void prop_set(Props *p, const char *key, const char *val) {
  for (size_t i = 0; i < p->len; i++)
    if (str_eq_wit_lit(&p->ptr[i].f0, key)) {
      automap_plugin_string_dup(&p->ptr[i].f1, val);
      return;
    }
  p->ptr = xrealloc(p->ptr, sizeof(DataEntry) * (p->len + 1));
  automap_plugin_string_dup(&p->ptr[p->len].f0, key);
  automap_plugin_string_dup(&p->ptr[p->len].f1, val);
  p->len++;
}
static Props clone_props(Props *src) {
  Props p = {0};
  if (src->len) {
    p.ptr = xcalloc(src->len, sizeof(DataEntry));
    p.len = src->len;
    for (size_t i = 0; i < src->len; i++) {
      automap_plugin_string_dup_n(&p.ptr[i].f0, (char *)src->ptr[i].f0.ptr,
                                  src->ptr[i].f0.len);
      automap_plugin_string_dup_n(&p.ptr[i].f1, (char *)src->ptr[i].f1.ptr,
                                  src->ptr[i].f1.len);
    }
  }
  return p;
}

static uint32_t parse_u32(const char *s, uint32_t fb) {
  if (!s || !*s)
    return fb;
  char *e;
  unsigned long v = strtoul(s, &e, 10);
  return e != s ? (uint32_t)v : fb;
}
static int parse_int(const char *s, int fb) {
  if (!s || !*s)
    return fb;
  char *e;
  long v = strtol(s, &e, 10);
  return e != s ? (int)v : fb;
}
static double parse_float(const char *s, double fb) {
  if (!s || !*s)
    return fb;
  char *e;
  double v = strtod(s, &e);
  return e != s ? v : fb;
}
static bool parse_bool(const char *s, bool fb) {
  if (!s || !*s)
    return fb;
  return strcmp(s, "true") == 0 || strcmp(s, "1") == 0;
}

static int layer_height(Layer *l) {
  return l->width ? (int)(l->data_len / l->width) : 0;
}
static Map map_from_wit(WitMap *w) {
  Map m = {0};
  m.len = w->layers.len;
  m.layers = xcalloc(m.len, sizeof(Layer));
  for (size_t i = 0; i < m.len; i++) {
    WitLayer *wl = &w->layers.ptr[i];
    m.layers[i].width = wl->width;
    m.layers[i].data_len = wl->data.len;
    m.layers[i].data = xcalloc(wl->data.len, sizeof(uint32_t));
    memcpy(m.layers[i].data, wl->data.ptr, wl->data.len * sizeof(uint32_t));
    m.layers[i].props = (Props){wl->props.ptr, wl->props.len};
  }
  m.props = (Props){w->props.ptr, w->props.len};
  return m;
}
static Layer clone_layer(Layer *l) {
  Layer c = {0};
  c.width = l->width;
  c.data_len = l->data_len;
  c.data = xcalloc(c.data_len, sizeof(uint32_t));
  memcpy(c.data, l->data, c.data_len * sizeof(uint32_t));
  c.props = clone_props(&l->props);
  return c;
}
static Map clone_map(Map *src) {
  Map m = {0};
  m.len = src ? src->len : 0;
  m.layers = xcalloc(m.len, sizeof(Layer));
  for (size_t i = 0; i < m.len; i++)
    m.layers[i] = clone_layer(&src->layers[i]);
  if (src)
    m.props = clone_props(&src->props);
  return m;
}

static int prop_cmp(const void *a, const void *b) {
  const DataEntry *x = a, *y = b;
  size_t n = x->f0.len < y->f0.len ? x->f0.len : y->f0.len;
  int c = memcmp(x->f0.ptr, y->f0.ptr, n);
  return c ? c : (x->f0.len > y->f0.len) - (x->f0.len < y->f0.len);
}
static void sort_props(Props *p) {
  if (p->len > 1)
    qsort(p->ptr, p->len, sizeof(DataEntry), prop_cmp);
}
static void map_to_wit(Map *m, WitMap *out) {
  out->layers.len = m->len;
  out->layers.ptr = xcalloc(m->len, sizeof(WitLayer));
  for (size_t i = 0; i < m->len; i++) {
    out->layers.ptr[i].width = m->layers[i].width;
    out->layers.ptr[i].data.len = m->layers[i].data_len;
    out->layers.ptr[i].data.ptr =
        xcalloc(m->layers[i].data_len, sizeof(uint32_t));
    memcpy(out->layers.ptr[i].data.ptr, m->layers[i].data,
           m->layers[i].data_len * sizeof(uint32_t));
    sort_props(&m->layers[i].props);
    out->layers.ptr[i].props.ptr = m->layers[i].props.ptr;
    out->layers.ptr[i].props.len = m->layers[i].props.len;
  }
  sort_props(&m->props);
  out->props.ptr = m->props.ptr;
  out->props.len = m->props.len;
}

static bool is_special(Config *c, uint32_t v) {
  return v && (v == c->empty || v == c->non_empty || v == c->other ||
               v == c->ignore || v == c->negate || v == c->different ||
               v == c->same);
}
static Config parse_config(Props *p) {
  Config c = {0};
  c.probability = 1.0;
  c.empty = parse_u32(prop_get(p, "rule_Empty"), 0);
  c.non_empty = parse_u32(prop_get(p, "rule_NonEmpty"), 0);
  c.other = parse_u32(prop_get(p, "rule_Other"), 0);
  c.ignore = parse_u32(prop_get(p, "rule_Ignore"), 0);
  c.negate = parse_u32(prop_get(p, "rule_Negate"), 0);
  c.different = parse_u32(prop_get(p, "rule_Different"), 0);
  c.same = parse_u32(prop_get(p, "rule_Same"), 0);
  c.match_outside = parse_bool(prop_get(p, "rule_MatchOutsideMap"), false);
  c.overflow_border = parse_bool(prop_get(p, "rule_OverflowBorder"), false);
  c.wrap_border = parse_bool(prop_get(p, "rule_WrapBorder"), false);
  c.no_overlap = parse_bool(prop_get(p, "rule_NoOverlappingOutput"), false);
  c.delete_tiles = parse_bool(prop_get(p, "rule_DeleteTiles"), false);
  c.mod_x = parse_int(prop_get(p, "rule_ModX"), 0);
  c.mod_y = parse_int(prop_get(p, "rule_ModY"), 0);
  c.offset_x = parse_int(prop_get(p, "rule_OffsetX"), 0);
  c.offset_y = parse_int(prop_get(p, "rule_OffsetY"), 0);
  c.probability = parse_float(prop_get(p, "rule_Probability"), 1.0);
  return c;
}

static bool excluded_prop(const char *k) {
  static const char *xs[] = {"name",
                             "rule_role",
                             "rule_target_layer",
                             "rule_input_index",
                             "rule_input_not",
                             "rule_layer_IgnoreHorizontalFlip",
                             "rule_layer_IgnoreVerticalFlip",
                             "rule_layer_IgnoreDiagonalFlip",
                             "rule_layer_AutoEmpty",
                             "rule_output_index",
                             "rule_output_Probability",
                             "rule_ModX",
                             "rule_ModY",
                             "rule_OffsetX",
                             "rule_OffsetY",
                             "rule_Probability",
                             "rule_Disabled",
                             "rule_MatchOutsideMap",
                             "rule_OverflowBorder",
                             "rule_WrapBorder",
                             "rule_NoOverlappingOutput",
                             "rule_DeleteTiles",
                             "rule_Empty",
                             "rule_NonEmpty",
                             "rule_Other",
                             "rule_Ignore",
                             "rule_Negate",
                             "rule_Different",
                             "rule_Same",
                             NULL};
  for (int i = 0; xs[i]; i++)
    if (strcmp(k, xs[i]) == 0)
      return true;
  return false;
}
static Props copy_non_rule_props(Props *src) {
  Props p = {0};
  for (size_t i = 0; i < src->len; i++) {
    char *k = wit_to_cstr(&src->ptr[i].f0);
    if (!excluded_prop(k)) {
      p.ptr = xrealloc(p.ptr, sizeof(DataEntry) * (p.len + 1));
      automap_plugin_string_dup_n(&p.ptr[p.len].f0, (char *)src->ptr[i].f0.ptr,
                                  src->ptr[i].f0.len);
      automap_plugin_string_dup_n(&p.ptr[p.len].f1, (char *)src->ptr[i].f1.ptr,
                                  src->ptr[i].f1.len);
      p.len++;
    }
  }
  return p;
}
static bool is_input_role(const char *r) {
  return strcmp(r, "input") == 0 || strcmp(r, "inputnot") == 0;
}
static void tile_push(TileVec *v, Tile t) {
  if (v->len == v->cap) {
    v->cap = v->cap ? v->cap * 2 : 8;
    v->items = xrealloc(v->items, v->cap * sizeof(Tile));
  }
  v->items[v->len++] = t;
}
static bool has_any(Map *m, int idx) {
  for (size_t i = 0; i < m->len; i++)
    if (idx >= 0 && (size_t)idx < m->layers[i].data_len &&
        m->layers[i].data[idx] != 0)
      return true;
  return false;
}
static void collect(Map *m, int idx, int w, int h, bool *used,
                    TileVec *by_layer) {
  if (used[idx])
    return;
  used[idx] = true;
  int x = idx % w, y = idx / w;
  bool any = false;
  for (size_t li = 0; li < m->len; li++) {
    uint32_t v = m->layers[li].data[idx];
    if (v) {
      any = true;
      tile_push(&by_layer[li], (Tile){{x, y}, v});
    }
  }
  if (!any)
    return;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      if (!dx && !dy)
        continue;
      int nx = x + dx, ny = y + dy;
      if (nx >= 0 && nx < w && ny >= 0 && ny < h)
        collect(m, ny * w + nx, w, h, used, by_layer);
    }
}

static void inputlayer_push(InputLayer **arr, size_t *len, size_t *cap,
                            InputLayer v) {
  if (*len == *cap) {
    *cap = *cap ? *cap * 2 : 4;
    *arr = xrealloc(*arr, *cap * sizeof(InputLayer));
  }
  (*arr)[(*len)++] = v;
}
static void outputlayer_push(OutputLayer **arr, size_t *len, size_t *cap,
                             OutputLayer v) {
  if (*len == *cap) {
    *cap = *cap ? *cap * 2 : 4;
    *arr = xrealloc(*arr, *cap * sizeof(OutputLayer));
  }
  (*arr)[(*len)++] = v;
}
static void outptr_push(OutputLayer ***arr, size_t *len, size_t *cap,
                        OutputLayer *v) {
  if (*len == *cap) {
    *cap = *cap ? *cap * 2 : 4;
    *arr = xrealloc(*arr, *cap * sizeof(OutputLayer *));
  }
  (*arr)[(*len)++] = v;
}

static void cell_matcher_push(InputCell *c, Matcher m) {
  if (c->len == c->cap) {
    c->cap = c->cap ? c->cap * 2 : 2;
    c->matchers = xrealloc(c->matchers, c->cap * sizeof(Matcher));
  }
  c->matchers[c->len++] = m;
}
static InputCell *group_cell(InputGroup *g, Point p) {
  for (size_t i = 0; i < g->len; i++)
    if (g->cells[i].p.x == p.x && g->cells[i].p.y == p.y)
      return &g->cells[i];
  if (g->len == g->cap) {
    g->cap = g->cap ? g->cap * 2 : 4;
    g->cells = xrealloc(g->cells, g->cap * sizeof(InputCell));
  }
  InputCell *c = &g->cells[g->len++];
  memset(c, 0, sizeof(*c));
  c->p = p;
  return c;
}
static int cell_cmp(const void *a, const void *b) {
  const InputCell *x = a, *y = b;
  if (x->p.y != y->p.y)
    return x->p.y - y->p.y;
  return x->p.x - y->p.x;
}
static bool reference_binder(uint32_t v, Config *c) {
  return !(v == c->empty || v == c->other || v == c->negate ||
           v == c->different || v == c->same);
}
static bool validate_group(InputGroup *g, Config *c, Str *err) {
  bool has_ref = false;
  for (size_t i = 0; i < g->len; i++) {
    bool cell_rel = false, cell_bind = false;
    for (size_t j = 0; j < g->cells[i].len; j++) {
      Matcher *m = &g->cells[i].matchers[j];
      if (m->value == c->same)
        cell_rel = true;
      if (!m->negated && reference_binder(m->value, c))
        cell_bind = true;
    }
    if (cell_rel && !has_ref) {
      char buf[128];
      snprintf(buf, sizeof(buf),
               "relative matcher at (%d,%d) used before reference established",
               g->cells[i].p.x, g->cells[i].p.y);
      set_err(err, buf);
      return false;
    }
    if (cell_bind)
      has_ref = true;
  }
  return true;
}

static bool build_groups(InputLayer *ils, size_t ilen, Config *c,
                         InputGroup **out, size_t *out_len, Str *err) {
  InputGroup *gs = NULL;
  size_t glen = 0, gcap = 0;
  for (size_t i = 0; i < ilen; i++) {
    InputLayer *il = &ils[i];
    size_t gi;
    for (gi = 0; gi < glen; gi++)
      if (streq(gs[gi].selector, il->selector) &&
          streq(gs[gi].input_index, il->input_index))
        break;
    if (gi == glen) {
      if (glen == gcap) {
        gcap = gcap ? gcap * 2 : 4;
        gs = xrealloc(gs, gcap * sizeof(InputGroup));
      }
      memset(&gs[glen], 0, sizeof(InputGroup));
      gs[glen].selector = il->selector;
      gs[glen].input_index = il->input_index;
      gi = glen++;
    }
    for (size_t t = 0; t < il->len; t++) {
      InputCell *cell = group_cell(&gs[gi], il->tiles[t].p);
      cell_matcher_push(cell, (Matcher){il->tiles[t].value, il->negated});
    }
  }
  for (size_t gi = 0; gi < glen; gi++) {
    for (size_t ci = 0; ci < gs[gi].len; ci++)
      for (size_t mi = 0; mi < gs[gi].cells[ci].len; mi++)
        if (gs[gi].cells[ci].matchers[mi].value == c->empty)
          gs[gi].has_empty_matcher = true;
    qsort(gs[gi].cells, gs[gi].len, sizeof(InputCell), cell_cmp);
    if (!validate_group(&gs[gi], c, err))
      return false;
  }
  *out = gs;
  *out_len = glen;
  return true;
}

static RuleOutputs build_outputs(OutputLayer *ols, size_t olen) {
  RuleOutputs ro = {0};
  for (size_t i = 0; i < olen; i++) {
    OutputLayer *ol = &ols[i];
    if (!ol->output_index || !*ol->output_index) {
      outptr_push(&ro.always, &ro.always_len, &ro.always_cap, ol);
      continue;
    }
    size_t vi;
    for (vi = 0; vi < ro.var_len; vi++)
      if (streq(ro.variants[vi].index, ol->output_index))
        break;
    if (vi == ro.var_len) {
      if (ro.var_len == ro.var_cap) {
        ro.var_cap = ro.var_cap ? ro.var_cap * 2 : 4;
        ro.variants = xrealloc(ro.variants, ro.var_cap * sizeof(OutputVariant));
      }
      memset(&ro.variants[ro.var_len], 0, sizeof(OutputVariant));
      ro.variants[ro.var_len].index = ol->output_index;
      ro.variants[ro.var_len].probability = ol->probability;
      vi = ro.var_len++;
    }
    outptr_push(&ro.variants[vi].layers, &ro.variants[vi].len,
                &ro.variants[vi].cap, ol);
    ro.variants[vi].probability = ol->probability;
  }
  return ro;
}
static const char *first_layer_prop(InputLayer *ils, size_t ilen, Map *rules,
                                    const char *key) {
  for (size_t ii = 0; ii < ilen; ii++)
    for (size_t i = 0; i < rules->len; i++) {
      Layer *l = &rules->layers[i];
      if (!is_input_role(prop_get(&l->props, "rule_role")))
        continue;
      if (!streq(prop_get(&l->props, "rule_target_layer"), ils[ii].selector))
        continue;
      if (!streq(prop_get(&l->props, "rule_input_index"), ils[ii].input_index))
        continue;
      const char *v = prop_get(&l->props, key);
      if (*v)
        return v;
    }
  return "";
}
static int rule_cmp(const void *a, const void *b) {
  const Rule *x = a, *y = b;
  if (x->order_y != y->order_y)
    return x->order_y - y->order_y;
  return x->order_x - y->order_x;
}

static bool extract_rules(Map *rules_map, Config *cfg, RuleVec *rv, Str *err) {
  if (rules_map->len == 0) {
    set_err(err, "no layers in rules map");
    return false;
  }
  for (size_t i = 0; i < rules_map->len; i++)
    if (prop_has(&rules_map->layers[i].props, "rule_input_not")) {
      char b[128];
      snprintf(
          b, sizeof(b),
          "layer %zu uses obsolete rule_input_not; use rule_role=\"inputnot\"",
          i);
      set_err(err, b);
      return false;
    }
  int w = rules_map->layers[0].width, h = layer_height(&rules_map->layers[0]);
  bool *used = xcalloc((size_t)w * h, sizeof(bool));
  for (int idx = 0; idx < w * h; idx++) {
    if (used[idx] || !has_any(rules_map, idx))
      continue;
    TileVec *by = xcalloc(rules_map->len, sizeof(TileVec));
    collect(rules_map, idx, w, h, used, by);
    int minx = INT32_MAX, miny = INT32_MAX;
    for (size_t li = 0; li < rules_map->len; li++)
      for (size_t t = 0; t < by[li].len; t++) {
        if (by[li].items[t].p.x < minx)
          minx = by[li].items[t].p.x;
        if (by[li].items[t].p.y < miny)
          miny = by[li].items[t].p.y;
      }
    InputLayer *ils = NULL;
    OutputLayer *ols = NULL;
    size_t ilen = 0, icap = 0, olen = 0, ocap = 0;
    for (size_t li = 0; li < rules_map->len; li++) {
      if (!by[li].len)
        continue;
      Layer *l = &rules_map->layers[li];
      const char *role = prop_get(&l->props, "rule_role");
      const char *target = prop_get(&l->props, "rule_target_layer");
      if (!*role) {
        char b[80];
        snprintf(b, sizeof(b), "layer %zu missing rule_role property", li);
        set_err(err, b);
        return false;
      }
      if (!*target) {
        char b[96];
        snprintf(b, sizeof(b), "layer %zu missing rule_target_layer property",
                 li);
        set_err(err, b);
        return false;
      }
      Tile *norm = xcalloc(by[li].len, sizeof(Tile));
      for (size_t t = 0; t < by[li].len; t++)
        norm[t] =
            (Tile){{by[li].items[t].p.x - minx, by[li].items[t].p.y - miny},
                   by[li].items[t].value};
      if (is_input_role(role)) {
        inputlayer_push(
            &ils, &ilen, &icap,
            (InputLayer){norm, by[li].len, xstrdup0(target),
                         xstrdup0(prop_get(&l->props, "rule_input_index")),
                         strcmp(role, "inputnot") == 0});
      } else if (strcmp(role, "output") == 0) {
        outputlayer_push(
            &ols, &olen, &ocap,
            (OutputLayer){
                norm, by[li].len, xstrdup0(target),
                xstrdup0(prop_get(&l->props, "rule_output_index")),
                parse_float(prop_get(&l->props, "rule_output_Probability"),
                            1.0),
                copy_non_rule_props(&l->props)});
      } else {
        char b[128];
        snprintf(b, sizeof(b), "layer %zu has invalid rule_role %s", li, role);
        set_err(err, b);
        return false;
      }
    }
    if (!(ilen > 0 && olen > 0)) {
      char b[128];
      snprintf(b, sizeof(b),
               "rule at position %d has only inputs=%zu or only outputs=%zu "
               "(need both)",
               idx, ilen, olen);
      set_err(err, b);
      return false;
    }
    Rule r = {0};
    if (!build_groups(ils, ilen, cfg, &r.groups, &r.group_len, err))
      return false;
    r.outputs = build_outputs(ols, olen);
    r.mod_x =
        imax(parse_int(first_layer_prop(ils, ilen, rules_map, "rule_ModX"),
                       cfg->mod_x),
             1);
    r.mod_y =
        imax(parse_int(first_layer_prop(ils, ilen, rules_map, "rule_ModY"),
                       cfg->mod_y),
             1);
    r.offset_x = parse_int(
        first_layer_prop(ils, ilen, rules_map, "rule_OffsetX"), cfg->offset_x);
    r.offset_y = parse_int(
        first_layer_prop(ils, ilen, rules_map, "rule_OffsetY"), cfg->offset_y);
    r.probability = cfg->probability;
    r.order_x = minx;
    r.order_y = miny;
    r.config = cfg;
    if (rv->len == rv->cap) {
      rv->cap = rv->cap ? rv->cap * 2 : 8;
      rv->items = xrealloc(rv->items, rv->cap * sizeof(Rule));
    }
    rv->items[rv->len++] = r;
  }
  qsort(rv->items, rv->len, sizeof(Rule), rule_cmp);
  return true;
}

static int rel_abs(int start, int w, int h, int rx, int ry) {
  int sx = start % w, sy = start / w, ax = sx + rx, ay = sy + ry;
  if (ax < 0 || ax >= w || ay < 0 || ay >= h)
    return -1;
  return ay * w + ax;
}
static bool group_contains(InputGroup *g, uint32_t v, Config *c) {
  if (v == 0)
    return false;
  for (size_t i = 0; i < g->len; i++)
    for (size_t j = 0; j < g->cells[i].len; j++) {
      uint32_t mv = g->cells[i].matchers[j].value;
      if (mv == 0 || is_special(c, mv))
        continue;
      if (mv == v)
        return true;
    }
  return false;
}
static bool matches_other(InputGroup *g, uint32_t v, Config *c) {
  if (v == 0)
    return !g->has_empty_matcher;
  return !group_contains(g, v, c);
}
static bool match_tile(uint32_t rv, uint32_t iv, Config *c, InputGroup *g,
                       uint32_t ref, bool has_ref) {
  if (rv == c->ignore)
    return true;
  if (rv == c->empty)
    return iv == 0;
  if (rv == c->non_empty)
    return iv != 0;
  if (rv == c->other)
    return matches_other(g, iv, c);
  if (rv == c->different)
    return iv != 0;
  if (rv == c->same)
    return has_ref && iv == ref;
  return rv == iv;
}
static double rand01(void) {
  return (double)(wasi_random_random_get_random_u64() >> 11) /
         (double)(UINT64_C(1) << 53);
}
static bool pos_constraints(Rule *r, Map *m, int idx) {
  if (!m->len)
    return false;
  int w = m->layers[0].width, x = idx % w, y = idx / w, mx = imax(r->mod_x, 1),
      my = imax(r->mod_y, 1);
  if ((x - r->offset_x) % mx != 0 || (y - r->offset_y) % my != 0)
    return false;
  if (r->probability <= 0)
    return false;
  if (r->probability >= 1)
    return true;
  return rand01() <= r->probability;
}

static bool selector_match(Layer *l, const char *sel);
static Layer *find_layer(Map *m, const char *sel) {
  if (!m || !m->len)
    return NULL;
  char *t = trim_copy(sel);
  Layer *ret = NULL;
  if (strcmp(t, "*") == 0)
    ret = &m->layers[0];
  else if (t[0] == '#') {
    int idx = parse_int(t + 1, -1);
    if (idx >= 0 && (size_t)idx < m->len)
      ret = &m->layers[idx];
  } else if (t[0] == '[' && t[strlen(t) - 1] == ']') {
    for (size_t i = 0; i < m->len; i++)
      if (selector_match(&m->layers[i], t)) {
        ret = &m->layers[i];
        break;
      }
  }
  free(t);
  return ret;
}
static bool attr_match(Layer *l, const char *expr) {
  char *e = trim_copy(expr);
  char *p = strstr(e, "!=");
  bool neg = false;
  if (!p) {
    p = strchr(e, '=');
  } else
    neg = true;
  bool ok = false;
  if (p) {
    *p = '\0';
    char *key = trim_copy(e), *val = trim_copy(p + (neg ? 2 : 1));
    size_t n = strlen(val);
    if (n >= 2 && ((val[0] == '"' && val[n - 1] == '"') ||
                   (val[0] == '\'' && val[n - 1] == '\''))) {
      memmove(val, val + 1, n - 2);
      val[n - 2] = '\0';
    }
    const char *lv = prop_get(&l->props, key);
    bool exists = prop_has(&l->props, key);
    ok = neg ? (!exists || strcmp(lv, val) != 0) : (strcmp(lv, val) == 0);
    free(key);
    free(val);
  } else {
    char *key = trim_copy(e);
    ok = prop_has(&l->props, key);
    free(key);
  }
  free(e);
  return ok;
}
static bool selector_match(Layer *l, const char *sel) {
  size_t n = strlen(sel);
  if (n < 2)
    return false;
  char *expr = xstrndup_u8((const uint8_t *)sel + 1, n - 2);
  bool ok = attr_match(l, expr);
  free(expr);
  return ok;
}
static Layer *empty_layer(int w, int h) {
  Layer *l = xcalloc(1, sizeof(Layer));
  l->width = w;
  l->data_len = (size_t)w * h;
  l->data = xcalloc(l->data_len, sizeof(uint32_t));
  return l;
}
static Layer *find_or_empty(Map *m, const char *sel, int w, int h) {
  Layer *l = find_layer(m, sel);
  return l ? l : empty_layer(w, h);
}

static bool group_match(InputGroup *g, Layer *layer, int idx, int w, int h,
                        Config *c) {
  uint32_t ref = 0;
  bool has_ref = false;
  uint32_t *diff = xcalloc(g->len ? g->len : 1, sizeof(uint32_t));
  size_t diff_len = 0;
  uint32_t *nond = xcalloc(g->len ? g->len : 1, sizeof(uint32_t));
  size_t nond_len = 0;
  for (size_t ci = 0; ci < g->len; ci++) {
    InputCell *cell = &g->cells[ci];
    int ai = rel_abs(idx, w, h, cell->p.x, cell->p.y);
    if (ai < 0)
      return false;
    uint32_t iv = layer->data[ai];
    bool has_neg = false, matched = false, bind = false, by_diff = false,
         by_nond = false;
    for (size_t mi = 0; mi < cell->len; mi++) {
      Matcher *m = &cell->matchers[mi];
      if (m->value == c->negate) {
        has_neg = true;
        continue;
      }
      bool cm = match_tile(m->value, iv, c, g, ref, has_ref);
      if (m->negated)
        cm = !cm;
      if (cm) {
        matched = true;
        if (!has_ref && !m->negated && reference_binder(m->value, c))
          bind = true;
        if (!m->negated) {
          if (m->value == c->different)
            by_diff = true;
          else
            by_nond = true;
        }
      }
    }
    if (has_neg) {
      matched = !matched;
      bind = false;
      by_diff = false;
      by_nond = false;
    }
    if (!matched)
      return false;
    if (bind) {
      ref = iv;
      has_ref = true;
    }
    if (by_diff)
      diff[diff_len++] = iv;
    if (by_nond)
      nond[nond_len++] = iv;
  }
  for (size_t i = 0; i < diff_len; i++)
    for (size_t j = 0; j < nond_len; j++)
      if (diff[i] == nond[j])
        return false;
  return true;
}
static bool rule_match(Rule *r, Map *m, int idx) {
  if (!pos_constraints(r, m, idx))
    return false;
  int w = m->layers[0].width, h = layer_height(&m->layers[0]);
  for (size_t i = 0; i < r->group_len; i++) {
    Layer *l = find_or_empty(m, r->groups[i].selector, w, h);
    if (!group_match(&r->groups[i], l, idx, w, h, r->config))
      return false;
  }
  return true;
}

static void ensure_layer(Layer *l, int w, int h) {
  if ((int)l->width == w && layer_height(l) == h)
    return;
  uint32_t *nd = xcalloc((size_t)w * h, sizeof(uint32_t));
  int cw = imin(l->width, w), ch = imin(layer_height(l), h);
  for (int y = 0; y < ch; y++)
    for (int x = 0; x < cw; x++)
      nd[y * w + x] = l->data[y * l->width + x];
  l->width = w;
  l->data = nd;
  l->data_len = (size_t)w * h;
}
static const char *default_name(const char *sel, int idx) {
  if (!sel || !*sel || strcmp(sel, "*") == 0 || sel[0] == '[') {
    char buf[32];
    snprintf(buf, sizeof(buf), "layer_%d", idx);
    return xstrdup0(buf);
  }
  return sel;
}
static Layer *get_or_create(Map *m, int w, int h, const char *sel,
                            Props *props) {
  Layer *l = find_layer(m, sel);
  if (l) {
    ensure_layer(l, w, h);
    for (size_t i = 0; i < props->len; i++) {
      char *k = wit_to_cstr(&props->ptr[i].f0);
      char *v = wit_to_cstr(&props->ptr[i].f1);
      prop_set(&l->props, k, v);
    }
    return l;
  }
  if (sel && sel[0] == '#') {
    int idx = parse_int(sel + 1, -1);
    if (idx >= 0) {
      if ((size_t)(idx + 1) > m->len) {
        m->layers = xrealloc(m->layers, sizeof(Layer) * (idx + 1));
        for (size_t i = m->len; i < (size_t)(idx + 1); i++) {
          memset(&m->layers[i], 0, sizeof(Layer));
          m->layers[i].width = w;
          m->layers[i].data_len = (size_t)w * h;
          m->layers[i].data = xcalloc(m->layers[i].data_len, sizeof(uint32_t));
        }
        m->len = idx + 1;
      }
      l = &m->layers[idx];
      ensure_layer(l, w, h);
      for (size_t i = 0; i < props->len; i++) {
        char *k = wit_to_cstr(&props->ptr[i].f0);
        char *v = wit_to_cstr(&props->ptr[i].f1);
        prop_set(&l->props, k, v);
      }
      return l;
    }
  }
  m->layers = xrealloc(m->layers, sizeof(Layer) * (m->len + 1));
  Layer nl = {0};
  nl.width = w;
  nl.data_len = (size_t)w * h;
  nl.data = xcalloc(nl.data_len, sizeof(uint32_t));
  nl.props = clone_props(props);
  if (!*prop_get(&nl.props, "name"))
    prop_set(&nl.props, "name", default_name(sel, (int)m->len));
  m->layers[m->len] = nl;
  return &m->layers[m->len++];
}
static bool occ_has(OccVec *o, const char *s, int x, int y) {
  for (size_t i = 0; i < o->len; i++)
    if (o->items[i].x == x && o->items[i].y == y &&
        streq(o->items[i].selector, s))
      return true;
  return false;
}
static void occ_mark(OccVec *o, const char *s, int x, int y) {
  if (o->len == o->cap) {
    o->cap = o->cap ? o->cap * 2 : 32;
    o->items = xrealloc(o->items, o->cap * sizeof(Occ));
  }
  o->items[o->len++] = (Occ){xstrdup0(s), x, y};
}
static OutputVariant *choose_variant(RuleOutputs *o) {
  double total = 0;
  for (size_t i = 0; i < o->var_len; i++)
    if (o->variants[i].probability > 0)
      total += o->variants[i].probability;
  if (total <= 0)
    return NULL;
  double roll = rand01() * total;
  for (size_t i = 0; i < o->var_len; i++) {
    if (o->variants[i].probability <= 0)
      continue;
    roll -= o->variants[i].probability;
    if (roll <= 0)
      return &o->variants[i];
  }
  return &o->variants[o->var_len - 1];
}
static bool output_would_overlap(int idx, int w, int h, OutputLayer **selected,
                                 size_t slen, OccVec *occ) {
  for (size_t si = 0; si < slen; si++)
    for (size_t ti = 0; ti < selected[si]->len; ti++) {
      int ai = rel_abs(idx, w, h, selected[si]->tiles[ti].p.x,
                       selected[si]->tiles[ti].p.y);
      if (ai < 0)
        continue;
      if (occ_has(occ, selected[si]->selector, ai % w, ai / w))
        return true;
    }
  return false;
}
static void mark_output_region(int idx, int w, int h, OutputLayer **selected,
                               size_t slen, OccVec *occ) {
  for (size_t si = 0; si < slen; si++)
    for (size_t ti = 0; ti < selected[si]->len; ti++) {
      int ai = rel_abs(idx, w, h, selected[si]->tiles[ti].p.x,
                       selected[si]->tiles[ti].p.y);
      if (ai < 0)
        continue;
      if (!occ_has(occ, selected[si]->selector, ai % w, ai / w))
        occ_mark(occ, selected[si]->selector, ai % w, ai / w);
    }
}
static void apply_tiles(Layer *t, int idx, OutputLayer *ol, Config *c) {
  int w = t->width, h = layer_height(t);
  for (size_t i = 0; i < ol->len; i++) {
    uint32_t out = ol->tiles[i].value;
    if (out == c->empty)
      out = 0;
    int ai = rel_abs(idx, w, h, ol->tiles[i].p.x, ol->tiles[i].p.y);
    if (ai < 0)
      continue;
    t->data[ai] = out;
  }
  for (size_t i = 0; i < ol->props.len; i++) {
    char *k = wit_to_cstr(&ol->props.ptr[i].f0);
    char *v = wit_to_cstr(&ol->props.ptr[i].f1);
    prop_set(&t->props, k, v);
  }
}
static void clear_region(Layer *t, int idx, Rule *r) {
  int w = t->width, h = layer_height(t);
  int *cleared = NULL;
  size_t len = 0, cap = 0;
  for (size_t gi = 0; gi < r->group_len; gi++)
    for (size_t ci = 0; ci < r->groups[gi].len; ci++) {
      int ai = rel_abs(idx, w, h, r->groups[gi].cells[ci].p.x,
                       r->groups[gi].cells[ci].p.y);
      if (ai < 0)
        continue;
      bool seen = false;
      for (size_t k = 0; k < len; k++)
        if (cleared[k] == ai)
          seen = true;
      if (seen)
        continue;
      if (len == cap) {
        cap = cap ? cap * 2 : 8;
        cleared = xrealloc(cleared, cap * sizeof(int));
      }
      cleared[len++] = ai;
      t->data[ai] = 0;
    }
}
static void apply_rule(Map *res, int w, int h, int idx, Rule *r,
                       OccVec *rule_occ) {
  OutputLayer **selected = NULL;
  size_t slen = 0, scap = 0;
  for (size_t i = 0; i < r->outputs.always_len; i++)
    outptr_push(&selected, &slen, &scap, r->outputs.always[i]);
  OutputVariant *v = choose_variant(&r->outputs);
  if (v)
    for (size_t i = 0; i < v->len; i++)
      outptr_push(&selected, &slen, &scap, v->layers[i]);
  if (r->config->no_overlap) {
    if (output_would_overlap(idx, w, h, selected, slen, rule_occ))
      return;
    mark_output_region(idx, w, h, selected, slen, rule_occ);
  }
  if (r->config->delete_tiles)
    for (size_t i = 0; i < slen; i++) {
      Layer *t =
          get_or_create(res, w, h, selected[i]->selector, &selected[i]->props);
      clear_region(t, idx, r);
    }
  for (size_t i = 0; i < slen; i++) {
    Layer *t =
        get_or_create(res, w, h, selected[i]->selector, &selected[i]->props);
    apply_tiles(t, idx, selected[i], r->config);
  }
}

static Map extend_layers(Map *m, int pl, int pt, int pr, int pb) {
  if (!m->len)
    return *m;
  Map e = {0};
  e.len = m->len;
  e.layers = xcalloc(e.len, sizeof(Layer));
  for (size_t i = 0; i < m->len; i++) {
    Layer *l = &m->layers[i];
    int ow = l->width, oh = layer_height(l), nw = ow + pl + pr,
        nh = oh + pt + pb;
    e.layers[i].width = nw;
    e.layers[i].data_len = (size_t)nw * nh;
    e.layers[i].data = xcalloc(e.layers[i].data_len, sizeof(uint32_t));
    for (int y = 0; y < oh; y++)
      for (int x = 0; x < ow; x++)
        e.layers[i].data[(y + pt) * nw + x + pl] = l->data[y * ow + x];
    e.layers[i].props = clone_props(&l->props);
  }
  e.props = clone_props(&m->props);
  return e;
}
static void fill_overflow(Map *m, int px, int py, int ow, int oh) {
  for (size_t i = 0; i < m->len; i++) {
    Layer *l = &m->layers[i];
    int nw = l->width, nh = layer_height(l);
    for (int y = 0; y < nh; y++)
      for (int x = 0; x < nw; x++) {
        if (x >= px && x < px + ow && y >= py && y < py + oh)
          continue;
        int ox = x - px, oy = y - py;
        if (ox < 0)
          ox = 0;
        else if (ox >= ow)
          ox = ow - 1;
        if (oy < 0)
          oy = 0;
        else if (oy >= oh)
          oy = oh - 1;
        l->data[y * nw + x] = l->data[(oy + py) * nw + ox + px];
      }
  }
}
static void fill_wrap(Map *m, int px, int py, int ow, int oh) {
  for (size_t i = 0; i < m->len; i++) {
    Layer *l = &m->layers[i];
    int nw = l->width, nh = layer_height(l);
    for (int y = 0; y < nh; y++)
      for (int x = 0; x < nw; x++) {
        if (x >= px && x < px + ow && y >= py && y < py + oh)
          continue;
        int ox = x - px, oy = y - py, wx = ((ox % ow) + ow) % ow,
            wy = ((oy % oh) + oh) % oh;
        l->data[y * nw + x] = l->data[(wy + py) * nw + wx + px];
      }
  }
}
static void rule_bounds(Rule *r, int *w, int *h) {
  int mx = 0, my = 0;
  for (size_t gi = 0; gi < r->group_len; gi++)
    for (size_t ci = 0; ci < r->groups[gi].len; ci++) {
      if (r->groups[gi].cells[ci].p.x > mx)
        mx = r->groups[gi].cells[ci].p.x;
      if (r->groups[gi].cells[ci].p.y > my)
        my = r->groups[gi].cells[ci].p.y;
    }
  *w = mx + 1;
  *h = my + 1;
}
static Map prepare_edges(Map *rules, Map *input, RuleVec *rv, EdgeCtx *ctx) {
  bool match =
           parse_bool(prop_get(&rules->props, "rule_MatchOutsideMap"), false),
       over = parse_bool(prop_get(&rules->props, "rule_OverflowBorder"), false),
       wrap = parse_bool(prop_get(&rules->props, "rule_WrapBorder"), false);
  if (over || wrap)
    match = true;
  if (!match) {
    ctx->extended = false;
    return *input;
  }
  int mw = 0, mh = 0;
  for (size_t i = 0; i < rv->len; i++) {
    int w, h;
    rule_bounds(&rv->items[i], &w, &h);
    if (w > mw)
      mw = w;
    if (h > mh)
      mh = h;
  }
  if (!mw || !mh) {
    ctx->extended = false;
    return *input;
  }
  ctx->original_w = input->layers[0].width;
  ctx->original_h = layer_height(&input->layers[0]);
  ctx->pad_x = mw - 1;
  ctx->pad_y = mh - 1;
  ctx->extended = true;
  Map ex = extend_layers(input, ctx->pad_x, ctx->pad_y, ctx->pad_x, ctx->pad_y);
  if (wrap)
    fill_wrap(&ex, ctx->pad_x, ctx->pad_y, ctx->original_w, ctx->original_h);
  else if (over)
    fill_overflow(&ex, ctx->pad_x, ctx->pad_y, ctx->original_w,
                  ctx->original_h);
  return ex;
}
static Map crop_layers(Map *m, int cl, int ct, int nw, int nh) {
  Map c = {0};
  c.len = m->len;
  c.layers = xcalloc(c.len, sizeof(Layer));
  for (size_t i = 0; i < m->len; i++) {
    Layer *l = &m->layers[i];
    int ew = l->width, eh = layer_height(l);
    c.layers[i].width = nw;
    c.layers[i].data_len = (size_t)nw * nh;
    c.layers[i].data = xcalloc(c.layers[i].data_len, sizeof(uint32_t));
    if (cl >= 0 && ct >= 0 && cl + nw <= ew && ct + nh <= eh)
      for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++)
          c.layers[i].data[y * nw + x] = l->data[(y + ct) * ew + x + cl];
    c.layers[i].props = clone_props(&l->props);
  }
  c.props = clone_props(&m->props);
  return c;
}

bool exports_gams_automap_automap_apply(WitMap *rules_w, WitMap *input_w,
                                        WitMap *maybe_target, WitMap *ret,
                                        Str *err) {
  Map rules = map_from_wit(rules_w), input = map_from_wit(input_w);
  if (input.len == 0) {
    set_err(err, "input map has no layers");
    return false;
  }
  Config cfg = parse_config(&rules.props);
  RuleVec rv = {0};
  if (!extract_rules(&rules, &cfg, &rv, err))
    return false;
  if (rv.len == 0) {
    set_err(err, "no rules found in rules map");
    return false;
  }
  EdgeCtx edge = {0};
  Map working = prepare_edges(&rules, &input, &rv, &edge);
  Map target;
  if (maybe_target)
    target = map_from_wit(maybe_target);
  else {
    target = (Map){0};
    target.props = clone_props(&input.props);
  }
  Map result = clone_map(&target);
  if (result.props.len == 0)
    result.props = clone_props(&input.props);
  if (edge.extended) {
    Map ex =
        extend_layers(&result, edge.pad_x, edge.pad_y, edge.pad_x, edge.pad_y);
    result = ex;
  }
  int w = working.layers[0].width, h = layer_height(&working.layers[0]);
  bool matched = false;
  typedef struct {
    int *idx;
    size_t len, cap;
  } MatchList;
  MatchList *ml = xcalloc(rv.len, sizeof(MatchList));
  for (size_t ri = 0; ri < rv.len; ri++)
    for (int i = 0; i < w * h; i++)
      if (rule_match(&rv.items[ri], &working, i)) {
        if (ml[ri].len == ml[ri].cap) {
          ml[ri].cap = ml[ri].cap ? ml[ri].cap * 2 : 32;
          ml[ri].idx = xrealloc(ml[ri].idx, ml[ri].cap * sizeof(int));
        }
        ml[ri].idx[ml[ri].len++] = i;
      }
  for (size_t ri = 0; ri < rv.len; ri++) {
    OccVec rule_occ = {0};
    for (size_t mi = 0; mi < ml[ri].len; mi++) {
      apply_rule(&result, w, h, ml[ri].idx[mi], &rv.items[ri], &rule_occ);
      matched = true;
    }
  }
  if (!matched) {
    set_err(err, "no rules match");
    return false;
  }
  if (edge.extended)
    result = crop_layers(&result, edge.pad_x, edge.pad_y, edge.original_w,
                         edge.original_h);
  map_to_wit(&result, ret);
  return true;
}
