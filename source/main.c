/*
 * Vitrine Alvim — slideshow de fotos para Nintendo Switch (homebrew)
 *
 * Fotos:  sd:/switch/vitrine/fotos/   (jpg, jpeg, png, webp — qualquer nome,
 *         em ordem numérica: 1, 2, ... 10, 11 ... 120)
 * Config: sd:/switch/vitrine/config.txt (criado sozinho)
 *
 * Controles
 *   Toque na lateral / deslizar / D-pad / L R ZL ZR / A B : trocar foto
 *   Toque no meio ou Y : pausar / continuar
 *   X : girar (vertical, vertical inverso, horizontal)
 *   + : sair
 */
#include <switch.h>
#include <SDL.h>
#include <SDL_image.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>

#define BASE    "sdmc:/switch/vitrine"
#define FOTOS   BASE "/fotos"
#define CONFIG  BASE "/config.txt"
#define SW 1280
#define SH 720
#define FADE_MS 700
#define HOLD_MS 12000
#define MAXF 4000
#define CACHE 4

/* ---------- configuração ---------- */
static int segundos = 6;
static int orient = 0;   /* 0 vertical (lado esquerdo p/ baixo), 1 vertical-inverso, 2 horizontal */
static const char *ORIENT_NAMES[3] = { "vertical", "vertical-inverso", "horizontal" };

static void load_config(void) {
    FILE *f = fopen(CONFIG, "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof line, f)) {
        char k[64], v[64];
        if (line[0] == '#') continue;
        if (sscanf(line, " %63[^= ] = %63s", k, v) == 2) {
            if (!strcmp(k, "segundos")) {
                int s = atoi(v);
                if (s >= 1 && s <= 600) segundos = s;
            } else if (!strcmp(k, "orientacao")) {
                for (int i = 0; i < 3; i++)
                    if (!strcmp(v, ORIENT_NAMES[i])) orient = i;
            }
        }
    }
    fclose(f);
}

static void save_config(void) {
    FILE *f = fopen(CONFIG, "w");
    if (!f) return;
    fprintf(f,
        "# Vitrine Alvim\n"
        "# orientacao: vertical, vertical-inverso ou horizontal\n"
        "orientacao = %s\n"
        "# tempo de cada foto, em segundos\n"
        "segundos = %d\n",
        ORIENT_NAMES[orient], segundos);
    fclose(f);
}

/* ---------- lista de fotos ---------- */
static char *files[MAXF];
static int nfiles = 0;

/* ordem "natural": 2.jpg vem antes de 10.jpg */
static int natcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            while (*a == '0' && isdigit((unsigned char)a[1])) a++;
            while (*b == '0' && isdigit((unsigned char)b[1])) b++;
            const char *sa = a, *sb = b;
            while (isdigit((unsigned char)*a)) a++;
            while (isdigit((unsigned char)*b)) b++;
            int la = (int)(a - sa), lb = (int)(b - sb);
            if (la != lb) return la - lb;
            int c = strncmp(sa, sb, la);
            if (c) return c;
        } else {
            int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
            if (ca != cb) return ca - cb;
            a++; b++;
        }
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int cmpf(const void *x, const void *y) {
    return natcmp(*(char * const *)x, *(char * const *)y);
}

static int is_img(const char *n) {
    const char *d = strrchr(n, '.');
    if (!d) return 0;
    return !strcasecmp(d, ".jpg") || !strcasecmp(d, ".jpeg") ||
           !strcasecmp(d, ".png") || !strcasecmp(d, ".webp");
}

static void scan(void) {
    DIR *d = opendir(FOTOS);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && nfiles < MAXF) {
        if (e->d_name[0] == '.') continue;          /* ignora ._arquivos do Mac */
        if (!is_img(e->d_name)) continue;
        files[nfiles++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(files, nfiles, sizeof(char *), cmpf);
}

/* ---------- cache de texturas (só algumas fotos na memória) ---------- */
typedef struct { int idx; SDL_Texture *t; int w, h; unsigned use; } Slot;
static Slot cache[CACHE];
static unsigned use_tick = 0;
static SDL_Renderer *R;

static Slot *get(int idx) {
    for (int i = 0; i < CACHE; i++)
        if (cache[i].t && cache[i].idx == idx) { cache[i].use = ++use_tick; return &cache[i]; }

    int v = 0;
    for (int i = 0; i < CACHE; i++) {
        if (!cache[i].t) { v = i; break; }
        if (cache[i].use < cache[v].use) v = i;
    }
    if (cache[v].t) { SDL_DestroyTexture(cache[v].t); cache[v].t = NULL; }

    char path[600];
    snprintf(path, sizeof path, "%s/%s", FOTOS, files[idx]);
    SDL_Texture *t = IMG_LoadTexture(R, path);
    if (!t) return NULL;
    SDL_QueryTexture(t, NULL, NULL, &cache[v].w, &cache[v].h);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    cache[v].t = t;
    cache[v].idx = idx;
    cache[v].use = ++use_tick;
    return &cache[v];
}

/* ---------- desenho ---------- */
static double angle(void) { return orient == 0 ? 90.0 : orient == 1 ? -90.0 : 0.0; }

static void draw_img(Slot *s, Uint8 a) {
    if (!s || !s->t) return;
    double ang = angle();
    SDL_SetTextureAlphaMod(s->t, a);

    if (ang != 0.0) {
        /* tela de quem segura em pé: 720 x 1280 */
        double sc = fmin(720.0 / s->w, 1280.0 / s->h);
        int dw = (int)(s->w * sc + 0.5), dh = (int)(s->h * sc + 0.5);
        SDL_Rect d = { SW / 2 - dw / 2, SH / 2 - dh / 2, dw, dh };
        SDL_SetTextureColorMod(s->t, 255, 255, 255);
        SDL_RenderCopyEx(R, s->t, NULL, &d, ang, NULL, SDL_FLIP_NONE);
    } else {
        /* horizontal: fundo com a própria foto escurecida + foto inteira no centro */
        double cs = fmax((double)SW / s->w, (double)SH / s->h);
        int cw = (int)(s->w * cs + 0.5), ch = (int)(s->h * cs + 0.5);
        SDL_Rect bg = { SW / 2 - cw / 2, SH / 2 - ch / 2, cw, ch };
        SDL_SetTextureColorMod(s->t, 40, 40, 44);
        SDL_RenderCopy(R, s->t, NULL, &bg);

        double sc = fmin((double)SW / s->w, (double)SH / s->h);
        int dw = (int)(s->w * sc + 0.5), dh = (int)(s->h * sc + 0.5);
        SDL_Rect d = { SW / 2 - dw / 2, SH / 2 - dh / 2, dw, dh };
        SDL_SetTextureColorMod(s->t, 255, 255, 255);
        SDL_RenderCopy(R, s->t, NULL, &d);
    }
}

/* barrinha de tempo no "topo" de quem está segurando */
static void draw_bar(float p) {
    if (p <= 0) return;
    if (p > 1) p = 1;
    SDL_SetRenderDrawColor(R, 241, 239, 233, 130);
    SDL_Rect r;
    if (orient == 0)      { r.x = SW - 4; r.y = 0; r.w = 4; r.h = (int)(SH * p); }
    else if (orient == 1) { int h = (int)(SH * p); r.x = 0; r.y = SH - h; r.w = 4; r.h = h; }
    else                  { r.x = 0; r.y = 0; r.w = (int)(SW * p); r.h = 4; }
    SDL_RenderFillRect(R, &r);
}

/* símbolo de pausa (duas barras) no centro */
static void draw_pause(Uint8 a) {
    SDL_SetRenderDrawColor(R, 8, 8, 10, (Uint8)(a * 0.6));
    SDL_Rect bgr = { SW / 2 - 60, SH / 2 - 60, 120, 120 };
    SDL_RenderFillRect(R, &bgr);
    SDL_SetRenderDrawColor(R, 241, 239, 233, a);
    if (orient == 2) {
        SDL_Rect b1 = { SW / 2 - 22, SH / 2 - 28, 14, 56 }, b2 = { SW / 2 + 8, SH / 2 - 28, 14, 56 };
        SDL_RenderFillRect(R, &b1); SDL_RenderFillRect(R, &b2);
    } else {
        SDL_Rect b1 = { SW / 2 - 28, SH / 2 - 22, 56, 14 }, b2 = { SW / 2 - 28, SH / 2 + 8, 56, 14 };
        SDL_RenderFillRect(R, &b1); SDL_RenderFillRect(R, &b2);
    }
}

/* posição horizontal (0..1) do toque, do ponto de vista de quem segura */
static float user_u(float x, float y) {
    if (orient == 0) return y / SH;
    if (orient == 1) return 1.0f - y / SH;
    return x / SW;
}

/* ---------- tela de erro sem fotos ---------- */
static void no_photos(void) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    printf("\n\n   VITRINE ALVIM\n\n"
           "   Nenhuma foto encontrada.\n\n"
           "   Coloque as fotos no cartao SD em:\n"
           "   /switch/vitrine/fotos/\n\n"
           "   Formatos: jpg, png ou webp.\n\n"
           "   Aperte + para sair.\n");
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    load_config();
    scan();
    if (nfiles == 0) { no_photos(); return 0; }
    save_config();   /* cria o config.txt na primeira vez */

    appletSetMediaPlaybackState(true);   /* não apaga a tela nem entra em espera */

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();

    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP);
    SDL_Window *win = SDL_CreateWindow("Vitrine Alvim", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SW, SH, 0);
    R = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);

    int cur = 0, prev = -1, paused = 0, preloaded = 0, touching = 0;
    Uint32 shown_at = SDL_GetTicks(), fade_at = shown_at, hold_until = 0, pause_flash = 0;
    float tx0 = 0, ty0 = 0, tx1 = 0, ty1 = 0;
    const Uint32 dur = (Uint32)segundos * 1000;

    /* acha a primeira foto que abre */
    for (int i = 0; i < nfiles && !get(cur); i++) cur = (cur + 1) % nfiles;

    #define GO(step, manual) do {                                        \
        int _n = cur;                                                     \
        for (int _i = 0; _i < nfiles; _i++) {                             \
            _n = ((_n + (step)) % nfiles + nfiles) % nfiles;              \
            if (get(_n)) break;                                           \
        }                                                                 \
        if (_n != cur) { prev = cur; cur = _n; }                          \
        fade_at = shown_at = SDL_GetTicks(); preloaded = 0;               \
        if (manual) hold_until = SDL_GetTicks() + HOLD_MS;                \
    } while (0)

    int running = 1;
    while (running && appletMainLoop()) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) if (ev.type == SDL_QUIT) running = 0;

        /* ---- botões ---- */
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;
        if (down & (HidNpadButton_Right | HidNpadButton_Down | HidNpadButton_R | HidNpadButton_ZR | HidNpadButton_A))
            GO(1, 1);
        if (down & (HidNpadButton_Left | HidNpadButton_Up | HidNpadButton_L | HidNpadButton_ZL | HidNpadButton_B))
            GO(-1, 1);
        if (down & HidNpadButton_Y) { paused = !paused; pause_flash = SDL_GetTicks(); shown_at = SDL_GetTicks(); hold_until = 0; }
        if (down & HidNpadButton_X) { orient = (orient + 1) % 3; save_config(); }

        /* ---- toque ---- */
        HidTouchScreenState ts = {0};
        if (hidGetTouchScreenStates(&ts, 1)) {
            if (ts.count > 0) {
                float x = (float)ts.touches[0].x, y = (float)ts.touches[0].y;
                if (!touching) { touching = 1; tx0 = x; ty0 = y; }
                tx1 = x; ty1 = y;
            } else if (touching) {
                touching = 0;
                float u0 = user_u(tx0, ty0), u1 = user_u(tx1, ty1);
                float width = orient == 2 ? (float)SW : (float)SH;
                float du = (u1 - u0) * width;
                if (du < -80)      GO(1, 1);         /* deslizou para a esquerda: próxima */
                else if (du > 80)  GO(-1, 1);
                else if (fabsf(tx1 - tx0) < 40 && fabsf(ty1 - ty0) < 40) {
                    if (u1 > 0.7f)      GO(1, 1);
                    else if (u1 < 0.3f) GO(-1, 1);
                    else { paused = !paused; pause_flash = SDL_GetTicks(); shown_at = SDL_GetTicks(); hold_until = 0; }
                }
            }
        }

        /* ---- tempo ---- */
        Uint32 now = SDL_GetTicks();
        if (paused || now < hold_until) {
            if (now < hold_until && !paused) shown_at = now - (dur > 1500 ? dur - 1500 : 0);
            else shown_at = now;
        } else if (now - shown_at >= dur) {
            GO(1, 0);
            now = SDL_GetTicks();
        }

        /* ---- desenho ---- */
        SDL_SetRenderDrawColor(R, 11, 11, 13, 255);
        SDL_RenderClear(R);

        float f = (float)(now - fade_at) / FADE_MS;
        if (f >= 1.0f) { f = 1.0f; prev = -1; }
        if (prev >= 0) draw_img(get(prev), 255);
        draw_img(get(cur), (Uint8)(255 * f));

        if (!paused && now >= hold_until) draw_bar((float)(now - shown_at) / dur);

        if (paused) {
            Uint32 t = now - pause_flash;
            draw_pause(t < 1500 ? 230 : 90);
        }

        SDL_RenderPresent(R);

        /* carrega a próxima foto enquanto a atual está parada na tela */
        if (!preloaded && prev < 0) { get((cur + 1) % nfiles); preloaded = 1; }
    }

    for (int i = 0; i < CACHE; i++) if (cache[i].t) SDL_DestroyTexture(cache[i].t);
    SDL_DestroyRenderer(R);
    SDL_DestroyWindow(win);
    IMG_Quit();
    SDL_Quit();
    appletSetMediaPlaybackState(false);
    for (int i = 0; i < nfiles; i++) free(files[i]);
    return 0;
}
