// GTA Vice City 3rd-person (pseudo-3D) for Flipper Zero
// UP run, DOWN slow, LEFT/RIGHT strafe, OK shoot / restart, BACK exit
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define HZ 22 // horizon y
#define SC 40.0f // horizontal scale
#define HH 40.0f // ground scale
#define NOBJ 10

enum { O_NONE, O_CAR, O_PED, O_CASH };

typedef struct {
    uint8_t type;
    bool cop, hit;
    float x, zw, vz;
} Obj;

typedef struct {
    float px, D, vp;
    int money, hp, pts, wtimer, clip, reserve, flash;
    bool up, dn, lt, rt, shoot, running, over;
    uint32_t tick;
    Obj o[NOBJ];
    FuriMutex* mtx;
} Game;

static inline uint32_t rnd(uint32_t n) {
    return furi_hal_random_get() % n;
}

static void reset(Game* g) {
    memset(g->o, 0, sizeof(g->o));
    g->px = 0;
    g->D = 0;
    g->vp = 0.1f;
    g->money = 500;
    g->hp = 100;
    g->pts = 0;
    g->wtimer = 0;
    g->clip = 12;
    g->reserve = 36;
    g->flash = 0;
    g->over = false;
}

static int wanted(Game* g) {
    int w = (g->pts + 1) / 2;
    return w > 5 ? 5 : w;
}
static void add_heat(Game* g, int n) {
    g->pts += n;
    if(g->pts > 10) g->pts = 10;
    g->wtimer = 500;
}

static void spawn(Game* g) {
    for(int i = 0; i < NOBJ; i++) {
        Obj* o = &g->o[i];
        if(o->type) continue;
        o->hit = false;
        o->cop = false;
        o->zw = g->D + 20 + rnd(6);
        int r = rnd(100);
        if(r < 40) {
            o->type = O_CAR;
            int lane = rnd(3);
            if(lane == 0) {
                o->x = -1.6f;
                o->vz = -0.14f;
            } else if(lane == 1) {
                o->x = 1.0f;
                o->vz = 0.07f;
            } else {
                o->x = 2.5f;
                o->vz = 0;
            }
            if(g->pts >= 3 && rnd(2)) {
                o->cop = true;
                o->x = g->px;
                o->vz = -0.1f;
            }
        } else if(r < 72) {
            o->type = O_PED;
            o->x = ((int)rnd(57) - 28) / 10.0f;
            o->vz = 0;
        } else {
            o->type = O_CASH;
            o->x = ((int)rnd(57) - 28) / 10.0f;
            o->vz = 0;
        }
        return;
    }
}

static void update(Game* g) {
    g->tick++;
    if(g->over) return;

    float tv = g->up ? 0.22f : (g->dn ? 0.04f : 0.10f);
    g->vp += (tv - g->vp) * 0.2f;
    g->D += g->vp;
    if(g->lt) g->px -= 0.08f;
    if(g->rt) g->px += 0.08f;
    if(g->px < -2.8f) g->px = -2.8f;
    if(g->px > 2.8f) g->px = 2.8f;
    if(g->flash > 0) g->flash--;

    for(int i = 0; i < NOBJ; i++) {
        Obj* o = &g->o[i];
        if(!o->type) continue;
        o->zw += o->vz;
        float z = o->zw - g->D;
        if(z < 0.4f || z > 30) {
            o->type = O_NONE;
            continue;
        }
        if(!o->hit && z < 1.4f && z > 0.7f) {
            float half = o->type == O_CAR ? 0.85f : (o->type == O_PED ? 0.3f : 0.45f);
            if(fabsf(o->x - g->px) < half + 0.3f) {
                if(o->type == O_CAR) {
                    o->hit = true;
                    g->hp -= o->cop ? 25 : 18;
                    g->px += (g->px > o->x ? 1 : -1) * 0.8f;
                    g->vp *= 0.3f;
                } else if(o->type == O_PED) {
                    o->type = O_NONE;
                    add_heat(g, 1);
                } else {
                    o->type = O_NONE;
                    g->money += 50;
                    g->reserve += 3;
                    if(g->reserve > 99) g->reserve = 99;
                }
            }
        }
    }

    if(rnd(100) < 7) spawn(g);

    if(g->shoot) {
        g->shoot = false;
        if(g->clip > 0) {
            g->clip--;
            g->flash = 3;
            int best = -1;
            float bz = 99;
            for(int i = 0; i < NOBJ; i++) {
                Obj* o = &g->o[i];
                float z = o->zw - g->D;
                bool tgt = (o->type == O_PED) || (o->type == O_CAR && o->cop);
                if(tgt && fabsf(o->x - g->px) < 0.6f && z > 1.5f && z < 14 && z < bz) {
                    bz = z;
                    best = i;
                }
            }
            if(best >= 0) {
                g->money += g->o[best].type == O_CAR ? 200 : 100;
                g->o[best].type = O_NONE;
                add_heat(g, 1);
            }
            if(g->clip == 0) {
                int t = g->reserve > 12 ? 12 : g->reserve;
                g->clip = t;
                g->reserve -= t;
            }
        }
    }

    if(g->pts > 0 && --g->wtimer <= 0) {
        g->pts--;
        g->wtimer = 300;
    }
    if(g->hp <= 0) {
        g->hp = 0;
        g->over = true;
    }
}

static void proj(Game* g, float wx, float z, int* sx, int* gy) {
    if(z < 0.5f) z = 0.5f;
    *sx = (int)(64 + (wx - g->px) * SC / z);
    *gy = (int)(HZ + HH / z);
}

static void draw_palm(Canvas* c, int sx, int gy, float s) {
    int h = (int)(3.2f * s);
    int top = gy - h;
    int tx = sx + (int)(0.15f * s);
    canvas_draw_line(c, sx, gy, tx, top);
    if(s > 8) canvas_draw_line(c, sx + 1, gy, tx + 1, top);
    int f = (int)(1.2f * s);
    canvas_draw_line(c, tx, top, tx - f, top + f / 2);
    canvas_draw_line(c, tx, top, tx + f, top + f / 2);
    canvas_draw_line(c, tx, top, tx - f / 2, top - f / 3);
    canvas_draw_line(c, tx, top, tx + f / 2, top - f / 3);
    canvas_draw_line(c, tx, top, tx - f * 3 / 4, top + f);
    canvas_draw_line(c, tx, top, tx + f * 3 / 4, top + f);
}

static void draw_building(Canvas* c, Game* g, float x1, float x2, float hw, float z) {
    int a, b, gy;
    proj(g, x1, z, &a, &gy);
    proj(g, x2, z, &b, &gy);
    float s = SC / z;
    int l = a < b ? a : b;
    int w = abs(b - a);
    int hp = (int)(hw * s);
    int top = gy - hp;
    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, l, top, w, hp);
    canvas_set_color(c, ColorBlack);
    canvas_draw_frame(c, l, top, w + 1, hp + 1);
    if(s > 4) {
        int nf = (int)(hw / 1.3f);
        if(nf < 1) nf = 1;
        int ws = (int)(0.35f * s);
        if(ws < 1) ws = 1;
        for(int f = 0; f < nf; f++) {
            int wy = top + hp * (2 * f + 1) / (2 * nf);
            for(int i = 1; i <= 3; i++) canvas_draw_box(c, l + w * i / 4 - ws / 2, wy, ws, ws);
        }
    }
}

static void draw_car(Canvas* c, int sx, int gy, float s, bool cop, bool blink) {
    int w = (int)(1.7f * s), h = (int)(0.7f * s), ch = (int)(0.5f * s), cw = (int)(1.2f * s);
    if(w < 4) w = 4;
    if(h < 3) h = 3;
    if(ch < 2) ch = 2;
    if(cw < 3) cw = 3;
    int ww = (int)(0.3f * s);
    if(ww < 1) ww = 1;
    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, sx - cw / 2, gy - h - ch, cw, ch + 1);
    canvas_set_color(c, ColorBlack);
    canvas_draw_frame(c, sx - cw / 2, gy - h - ch, cw, ch + 1);
    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, sx - w / 2, gy - h, w, h);
    canvas_set_color(c, ColorBlack);
    canvas_draw_frame(c, sx - w / 2, gy - h, w, h);
    canvas_draw_box(c, sx - w / 2, gy - ww, ww, ww);
    canvas_draw_box(c, sx + w / 2 - ww, gy - ww, ww, ww);
    canvas_draw_box(c, sx - w / 2 + 1, gy - h + 1, ww, ww); // light
    canvas_draw_box(c, sx + w / 2 - ww - 1, gy - h + 1, ww, ww);
    if(cop && blink) canvas_draw_box(c, sx - cw / 4, gy - h - ch - 2, cw / 2 + 1, 2);
}

static void draw_ped(Canvas* c, int sx, int gy, float s) {
    int r = (int)(0.2f * s);
    if(r < 1) r = 1;
    int bw = (int)(0.35f * s);
    if(bw < 1) bw = 1;
    int bh = (int)(0.9f * s);
    if(bh < 2) bh = 2;
    canvas_draw_box(c, sx - bw / 2, gy - bh, bw, bh);
    canvas_draw_disc(c, sx, gy - bh - r, r);
}

static void draw_player(Canvas* c, Game* g) {
    int sw = (g->vp > 0.06f) ? (((g->tick / 3) % 2) ? 2 : -2) : 0;
    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, 55, 34, 18, 29);
    canvas_set_color(c, ColorBlack);
    canvas_draw_disc(c, 64, 40, 3); // head
    canvas_draw_box(c, 59, 44, 10, 10); // torso
    canvas_draw_line(c, 59, 45, 57 - sw / 2, 53); // left arm
    canvas_draw_line(c, 69, 45, 68, 38); // right arm aimed
    canvas_draw_box(c, 67, 35, 3, 3); // pistol
    canvas_draw_line(c, 62, 54, 62 + sw, 62); // legs
    canvas_draw_line(c, 66, 54, 66 - sw, 62);
    canvas_draw_line(c, 63, 54, 63 + sw, 62);
    canvas_draw_line(c, 65, 54, 65 - sw, 62);
    if(g->flash > 0) {
        canvas_draw_line(c, 68, 34, 64, HZ);
        canvas_draw_line(c, 65, 33, 71, 31);
        canvas_draw_line(c, 65, 31, 71, 33);
    }
}

static void draw_cb(Canvas* c, void* ctx) {
    Game* g = ctx;
    furi_mutex_acquire(g->mtx, FuriWaitForever);
    char buf[24];

    canvas_clear(c);
    canvas_set_color(c, ColorBlack);

    // sun + skyline
    canvas_draw_circle(c, 30, 12, 6);
    for(int i = 0; i < 9; i++) {
        int x = i * 17 + 3 - (int)(g->px * 3);
        int w = 8 + (i * 5) % 7, h = 6 + (i * 7) % 11;
        canvas_draw_frame(c, x, HZ - h, w, h);
    }
    canvas_draw_line(c, 0, HZ, 127, HZ);

    // road edges + lane dashes
    for(int side = -1; side <= 1; side += 2) {
        int x1 = (int)(64 + (side * 3.0f - g->px) * SC / 24.0f);
        int y1 = (int)(HZ + HH / 24.0f);
        int x2 = (int)(64 + (side * 3.0f - g->px) * SC / 0.7f);
        int y2 = (int)(HZ + HH / 0.7f);
        canvas_draw_line(c, x1, y1, x2, y2);
    }
    for(int k = (int)((g->D + 24) / 2); k >= (int)((g->D + 0.8f) / 2); k--) {
        float z = k * 2.0f - g->D;
        if(z < 0.8f) continue;
        int a, ay, b, by;
        proj(g, 0, z, &a, &ay);
        proj(g, 0, z + 0.9f, &b, &by);
        canvas_draw_line(c, a, ay, b, by);
    }

    // roadside decor far -> near
    for(int k = (int)((g->D + 24) / 3.0f); k >= (int)((g->D + 0.9f) / 3.0f); k--) {
        float z = k * 3.0f - g->D;
        if(z < 0.8f) continue;
        uint32_t h = (uint32_t)k * 2654435761u;
        int sx, gy;
        float s = SC / z;
        float bh = 4.0f + ((h >> 12) & 3) * 1.5f;
        int tl = (h >> 4) & 3, tr = (h >> 8) & 3;
        if(tl == 0 || tl == 3) {
            proj(g, -3.6f, z, &sx, &gy);
            draw_palm(c, sx, gy, s);
        } else {
            draw_building(c, g, -7.0f, -4.0f, bh, z);
        }
        if(tr == 0 || tr == 3) {
            proj(g, 3.6f, z, &sx, &gy);
            draw_palm(c, sx, gy, s);
        } else {
            draw_building(c, g, 4.0f, 7.0f, bh, z);
            if(k % 7 == 0 && z < 9) {
                proj(g, 5.5f, z, &sx, &gy);
                canvas_draw_str_aligned(c, sx, gy - (int)(bh * s) - 4, AlignCenter, AlignCenter, "VICE CITY");
            }
        }
    }

    // dynamic objects far -> near
    int idx[NOBJ], n = 0;
    for(int i = 0; i < NOBJ; i++)
        if(g->o[i].type) idx[n++] = i;
    for(int i = 0; i < n; i++)
        for(int j = i + 1; j < n; j++)
            if(g->o[idx[j]].zw > g->o[idx[i]].zw) {
                int t = idx[i];
                idx[i] = idx[j];
                idx[j] = t;
            }
    for(int i = 0; i < n; i++) {
        Obj* o = &g->o[idx[i]];
        float z = o->zw - g->D;
        if(z < 0.8f) continue;
        int sx, gy;
        float s = SC / z;
        proj(g, o->x, z, &sx, &gy);
        if(o->type == O_CAR)
            draw_car(c, sx, gy, s, o->cop, (g->tick / 4) % 2);
        else if(o->type == O_PED)
            draw_ped(c, sx, gy, s);
        else {
            int r = (int)(0.3f * s);
            if(r < 2) r = 2;
            int bob = (g->tick / 5) % 2;
            canvas_draw_disc(c, sx, gy - r - bob, r);
        }
    }

    draw_player(c, g);

    // HUD
    canvas_set_font(c, FontSecondary);
    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, 80, 0, 48, 19);
    canvas_set_color(c, ColorBlack);
    canvas_draw_frame(c, 80, 0, 48, 19);
    snprintf(buf, sizeof(buf), "$%06d", g->money);
    canvas_draw_str(c, 83, 8, buf);
    snprintf(buf, sizeof(buf), "HP %d", g->hp);
    canvas_draw_str(c, 83, 17, buf);
    for(int i = 0; i < wanted(g); i++) canvas_draw_str(c, 110 + i * 3, 18, "*");

    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, 86, 51, 42, 13);
    canvas_set_color(c, ColorBlack);
    canvas_draw_frame(c, 86, 51, 42, 13);
    canvas_draw_box(c, 89, 54, 8, 2); // pistol icon
    canvas_draw_box(c, 89, 56, 3, 4);
    snprintf(buf, sizeof(buf), "%d-%d", g->clip, g->reserve);
    canvas_draw_str(c, 100, 61, buf);

    if(g->over) {
        canvas_set_color(c, ColorWhite);
        canvas_draw_box(c, 24, 18, 80, 28);
        canvas_set_color(c, ColorBlack);
        canvas_draw_frame(c, 24, 18, 80, 28);
        canvas_set_font(c, FontPrimary);
        canvas_draw_str_aligned(c, 64, 27, AlignCenter, AlignCenter, "WASTED");
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, 39, AlignCenter, AlignCenter, "OK - restart");
    }
    furi_mutex_release(g->mtx);
}

static void input_cb(InputEvent* e, void* ctx) {
    Game* g = ctx;
    furi_mutex_acquire(g->mtx, FuriWaitForever);
    bool on = (e->type == InputTypePress || e->type == InputTypeRepeat);
    bool rel = (e->type == InputTypeRelease);
    if(on || rel) {
        switch(e->key) {
        case InputKeyUp: g->up = on; break;
        case InputKeyDown: g->dn = on; break;
        case InputKeyLeft: g->lt = on; break;
        case InputKeyRight: g->rt = on; break;
        default: break;
        }
    }
    if(e->key == InputKeyOk && e->type == InputTypePress) {
        if(g->over)
            reset(g);
        else
            g->shoot = true;
    }
    if(e->key == InputKeyBack && (e->type == InputTypeShort || e->type == InputTypeLong))
        g->running = false;
    furi_mutex_release(g->mtx);
}

int32_t gta_flipper_app(void* p) {
    UNUSED(p);
    Game* g = malloc(sizeof(Game));
    memset(g, 0, sizeof(Game));
    g->mtx = furi_mutex_alloc(FuriMutexTypeNormal);
    g->running = true;
    reset(g);

    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, draw_cb, g);
    view_port_input_callback_set(vp, input_cb, g);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    while(g->running) {
        furi_mutex_acquire(g->mtx, FuriWaitForever);
        update(g);
        furi_mutex_release(g->mtx);
        view_port_update(vp);
        furi_delay_ms(33);
    }

    gui_remove_view_port(gui, vp);
    furi_record_close(RECORD_GUI);
    view_port_free(vp);
    furi_mutex_free(g->mtx);
    free(g);
    return 0;
}
