#define _GNU_SOURCE
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <dirent.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *exts[] = {
	"png", "jpg", "jpeg", "jpe", "jfif", "gif", "bmp", "webp", "tif", "tiff", "svg", "ff", "qoi", "tga",
	"pcx", "lbm", "iff", "xpm", "xcf", "xv", "pnm", "ppm", "pgm", "pbm", "ico", "cur", "ani", "avif", "jxl",
};

static const char *help[] = {
	"Right Space PgDn  next image",
	"Left BkSp PgUp    previous image",
	"Home End          first, last image",
	"+ - wheel         zoom",
	"1                 actual size",
	"0 Z               fit to window",
	"drag              move",
	"F F11 dblclick    fullscreen",
	"P                 pause animation",
	"? F1              this help",
	"Q Esc             quit",
};

typedef struct {
	char **paths;
	int count;
	int index;
} List;

typedef struct {
	SDL_Texture **frames;
	int *delays;
	int count;
	int frame;
	int w, h;
	char error[256];
} Image;

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *checker;
static List list;
static Image img;
static float scale = 1, posx, posy;
static bool fit = true, show_help, paused, dragging;
static Uint64 next_frame_at;
static int max_tex = 0;

static bool has_image_ext(const char *name)
{
	const char *dot = strrchr(name, '.');
	if (!dot)
		return false;
	for (size_t i = 0; i < SDL_arraysize(exts); i++)
		if (!strcasecmp(dot + 1, exts[i]))
			return true;
	return false;
}

static int cmp_paths(const void *a, const void *b)
{
	return strverscmp(*(char *const *)a, *(char *const *)b);
}

static void list_add(const char *path)
{
	list.paths = realloc(list.paths, sizeof(char *) * (list.count + 1));
	list.paths[list.count++] = strdup(path);
}

static void list_dir(const char *dir, const char *select)
{
	DIR *d = opendir(dir);
	if (!d)
		return;
	int start = list.count;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.' || !has_image_ext(e->d_name))
			continue;
		char path[PATH_MAX];
		if (strcmp(dir, ".") == 0)
			snprintf(path, sizeof(path), "%s", e->d_name);
		else
			snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		struct stat st;
		if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
			list_add(path);
	}
	closedir(d);
	qsort(list.paths + start, list.count - start, sizeof(char *), cmp_paths);
	if (!select)
		return;
	for (int i = start; i < list.count; i++)
		if (!strcmp(list.paths[i], select)) {
			list.index = i;
			return;
		}
	list_add(select);
	list.index = list.count - 1;
}

static void build_list(int argc, char **argv)
{
	if (argc == 2) {
		struct stat st;
		if (stat(argv[1], &st) == 0 && S_ISDIR(st.st_mode)) {
			list_dir(argv[1], NULL);
			return;
		}
		char *copy = strdup(argv[1]);
		char *slash = strrchr(copy, '/');
		if (slash) {
			*slash = '\0';
			list_dir(slash == copy ? "/" : copy, argv[1]);
		} else {
			list_dir(".", argv[1]);
		}
		free(copy);
		return;
	}
	for (int i = 1; i < argc; i++) {
		struct stat st;
		if (stat(argv[i], &st) == 0 && S_ISDIR(st.st_mode))
			list_dir(argv[i], NULL);
		else
			list_add(argv[i]);
	}
}

static SDL_Surface *load_farbfeld(const char *path)
{
	SDL_IOStream *io = SDL_IOFromFile(path, "rb");
	if (!io)
		return NULL;
	Uint8 head[16];
	SDL_Surface *s = NULL;
	if (SDL_ReadIO(io, head, 16) == 16 && !memcmp(head, "farbfeld", 8)) {
		Uint32 w = (Uint32)head[8] << 24 | head[9] << 16 | head[10] << 8 | head[11];
		Uint32 h = (Uint32)head[12] << 24 | head[13] << 16 | head[14] << 8 | head[15];
		if (w && h && w <= 32768 && h <= 32768)
			s = SDL_CreateSurface((int)w, (int)h, SDL_PIXELFORMAT_RGBA32);
		Uint8 *row = s ? malloc((size_t)w * 8) : NULL;
		for (Uint32 y = 0; row && y < h; y++) {
			if (SDL_ReadIO(io, row, (size_t)w * 8) != (size_t)w * 8) {
				SDL_DestroySurface(s);
				s = NULL;
				break;
			}
			Uint8 *out = (Uint8 *)s->pixels + (size_t)y * s->pitch;
			for (Uint32 i = 0; i < w * 4; i++)
				out[i] = row[i * 2];
		}
		free(row);
	}
	SDL_CloseIO(io);
	if (!s)
		SDL_SetError("not a farbfeld image");
	return s;
}

static SDL_Texture *make_texture(SDL_Surface *s)
{
	SDL_Surface *use = s;
	if (max_tex > 0 && (s->w > max_tex || s->h > max_tex)) {
		float f = (float)max_tex / (s->w > s->h ? s->w : s->h);
		int w = (int)(s->w * f), h = (int)(s->h * f);
		use = SDL_ScaleSurface(s, w > 0 ? w : 1, h > 0 ? h : 1, SDL_SCALEMODE_LINEAR);
		if (!use)
			return NULL;
	}
	SDL_Texture *t = SDL_CreateTextureFromSurface(ren, use);
	if (use != s)
		SDL_DestroySurface(use);
	return t;
}

static void free_image(void)
{
	for (int i = 0; i < img.count; i++)
		if (img.frames[i])
			SDL_DestroyTexture(img.frames[i]);
	free(img.frames);
	free(img.delays);
	memset(&img, 0, sizeof(img));
}

static void load_image(const char *path)
{
	free_image();
	const char *dot = strrchr(path, '.');
	SDL_Surface *still = NULL;
	IMG_Animation *anim = NULL;
	if (dot && !strcasecmp(dot, ".ff"))
		still = load_farbfeld(path);
	else if (!(anim = IMG_LoadAnimation(path)))
		still = IMG_Load(path);
	if (anim && anim->count > 0) {
		img.count = anim->count;
		img.frames = calloc(img.count, sizeof(SDL_Texture *));
		img.delays = calloc(img.count, sizeof(int));
		img.w = anim->w;
		img.h = anim->h;
		for (int i = 0; i < img.count; i++) {
			img.frames[i] = make_texture(anim->frames[i]);
			img.delays[i] = anim->delays[i] > 10 ? anim->delays[i] : 100;
			if (!img.frames[i]) {
				snprintf(img.error, sizeof(img.error), "%s", SDL_GetError());
				break;
			}
		}
		IMG_FreeAnimation(anim);
	} else if (still) {
		img.count = 1;
		img.frames = calloc(1, sizeof(SDL_Texture *));
		img.delays = calloc(1, sizeof(int));
		img.w = still->w;
		img.h = still->h;
		img.frames[0] = make_texture(still);
		if (!img.frames[0])
			snprintf(img.error, sizeof(img.error), "%s", SDL_GetError());
		SDL_DestroySurface(still);
	} else {
		if (anim)
			IMG_FreeAnimation(anim);
		snprintf(img.error, sizeof(img.error), "%s", SDL_GetError());
	}
	if (img.error[0]) {
		fprintf(stderr, "tinyhat-view: %s: %s\n", path, img.error);
		free(img.frames);
		free(img.delays);
		img.frames = NULL;
		img.delays = NULL;
		img.count = 0;
	}
	for (int i = 0; i < img.count; i++)
		SDL_SetTextureBlendMode(img.frames[i], SDL_BLENDMODE_BLEND);
	fit = true;
	next_frame_at = SDL_GetTicks() + (img.count > 1 ? (Uint64)img.delays[0] : 0);
}

static void output_size(int *w, int *h)
{
	SDL_GetCurrentRenderOutputSize(ren, w, h);
}

static void apply_fit(void)
{
	int W, H;
	output_size(&W, &H);
	if (img.w <= 0 || img.h <= 0)
		return;
	float s = fminf((float)W / img.w, (float)H / img.h);
	scale = s >= 1 ? floorf(s) : s;
	posx = (W - img.w * scale) / 2;
	posy = (H - img.h * scale) / 2;
}

static void zoom_at(float px, float py, float s2)
{
	if (s2 < 0.01f)
		s2 = 0.01f;
	if (s2 > 64)
		s2 = 64;
	posx = px - (px - posx) * s2 / scale;
	posy = py - (py - posy) * s2 / scale;
	scale = s2;
	fit = false;
}

static float zoom_step(bool in)
{
	if (in)
		return scale >= 1 ? floorf(scale) + 1 : fminf(1, scale * 1.25f);
	return scale > 1 ? ceilf(scale) - 1 : scale / 1.25f;
}

static void update_title(void)
{
	char title[PATH_MAX + 64];
	const char *path = list.count ? list.paths[list.index] : "";
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	if (img.count)
		snprintf(title, sizeof(title), "%s  %dx%d  %d%%  %d/%d", base, img.w, img.h, (int)lroundf(scale * 100),
			 list.index + 1, list.count);
	else
		snprintf(title, sizeof(title), "%s  %d/%d", base, list.index + 1, list.count);
	SDL_SetWindowTitle(win, title);
}

static void draw_text(float x, float y, const char *s)
{
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
	SDL_RenderDebugText(ren, x + 1, y + 1, s);
	SDL_SetRenderDrawColor(ren, 230, 230, 230, 255);
	SDL_RenderDebugText(ren, x, y, s);
}

static void draw(void)
{
	if (fit)
		apply_fit();
	SDL_SetRenderDrawColor(ren, 32, 32, 36, 255);
	SDL_RenderClear(ren);
	if (img.count && img.frames[img.frame]) {
		SDL_FRect dst = {roundf(posx), roundf(posy), roundf(img.w * scale), roundf(img.h * scale)};
		SDL_Rect clip = {(int)dst.x, (int)dst.y, (int)dst.w, (int)dst.h};
		SDL_SetRenderClipRect(ren, &clip);
		SDL_RenderTextureTiled(ren, checker, NULL, 1, &dst);
		SDL_SetRenderClipRect(ren, NULL);
		SDL_Texture *t = img.frames[img.frame];
		float fs = (float)t->w / img.w;
		float eff = scale / fs;
		SDL_ScaleMode mode = SDL_SCALEMODE_LINEAR;
		if (eff >= 1)
			mode = eff == floorf(eff) ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_PIXELART;
		SDL_SetTextureScaleMode(t, mode);
		SDL_RenderTexture(ren, t, NULL, &dst);
	} else if (list.count) {
		draw_text(16, 16, "Cannot show this image:");
		draw_text(16, 32, img.error);
	} else {
		draw_text(16, 16, "No images found");
	}
	if (show_help) {
		float lh = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE + 6;
		SDL_FRect box = {8, 8, 40 * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE, lh * SDL_arraysize(help) + 16};
		SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(ren, 0, 0, 0, 200);
		SDL_RenderFillRect(ren, &box);
		for (size_t i = 0; i < SDL_arraysize(help); i++)
			draw_text(16, 16 + lh * i, help[i]);
	}
	SDL_RenderPresent(ren);
	update_title();
}

static void go_to(int index)
{
	if (list.count == 0)
		return;
	index = (index % list.count + list.count) % list.count;
	if (index == list.index && img.count)
		return;
	list.index = index;
	load_image(list.paths[index]);
}

static void make_checker(void)
{
	Uint32 px[16 * 16];
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++)
			px[y * 16 + x] = ((x < 8) ^ (y < 8)) ? 0xff505050 : 0xff3c3c3c;
	SDL_Surface *s = SDL_CreateSurfaceFrom(16, 16, SDL_PIXELFORMAT_ARGB8888, px, 64);
	checker = SDL_CreateTextureFromSurface(ren, s);
	SDL_SetTextureScaleMode(checker, SDL_SCALEMODE_NEAREST);
	SDL_DestroySurface(s);
}

static void initial_window_size(int *w, int *h)
{
	*w = 960;
	*h = 720;
	SDL_Rect usable;
	if (img.w <= 0 || !SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable))
		return;
	float s = fminf(1, fminf(usable.w * 0.9f / img.w, usable.h * 0.9f / img.h));
	*w = (int)(img.w * s) < 320 ? 320 : (int)(img.w * s);
	*h = (int)(img.h * s) < 240 ? 240 : (int)(img.h * s);
}

int main(int argc, char **argv)
{
	if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		printf("usage: tinyhat-view [FILE|DIR]...\n");
		for (size_t i = 0; i < SDL_arraysize(help); i++)
			printf("  %s\n", help[i]);
		return 0;
	}
	build_list(argc, argv);
	if (argc < 2)
		list_dir(".", NULL);
	SDL_SetAppMetadata("Tiny Hat Image Viewer", "1", "org.tinyhat.view");
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "tinyhat-view: %s\n", SDL_GetError());
		return 1;
	}
	if (!SDL_CreateWindowAndRenderer("tinyhat-view", 960, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN, &win,
					 &ren)) {
		fprintf(stderr, "tinyhat-view: %s\n", SDL_GetError());
		return 1;
	}
	max_tex = (int)SDL_GetNumberProperty(SDL_GetRendererProperties(ren), SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0);
	SDL_SetRenderVSync(ren, 1);
	make_checker();
	if (list.count)
		load_image(list.paths[list.index]);
	int ww, wh;
	initial_window_size(&ww, &wh);
	SDL_SetWindowSize(win, ww, wh);
	SDL_ShowWindow(win);
	bool running = true, redraw = true;
	while (running) {
		if (img.count > 1 && !paused && SDL_GetTicks() >= next_frame_at) {
			img.frame = (img.frame + 1) % img.count;
			next_frame_at = SDL_GetTicks() + (Uint64)img.delays[img.frame];
			redraw = true;
		}
		if (redraw) {
			draw();
			redraw = false;
		}
		Sint32 timeout = -1;
		if (img.count > 1 && !paused) {
			Uint64 now = SDL_GetTicks();
			timeout = next_frame_at > now ? (Sint32)(next_frame_at - now) : 0;
		}
		SDL_Event ev;
		if (!SDL_WaitEventTimeout(&ev, timeout))
			continue;
		do {
			float density = SDL_GetWindowPixelDensity(win);
			switch (ev.type) {
			case SDL_EVENT_QUIT:
				running = false;
				break;
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			case SDL_EVENT_WINDOW_EXPOSED:
				redraw = true;
				break;
			case SDL_EVENT_KEY_DOWN: {
				int W, H;
				output_size(&W, &H);
				switch (ev.key.key) {
				case SDLK_Q:
				case SDLK_ESCAPE:
					if (ev.key.key == SDLK_ESCAPE && show_help)
						show_help = false;
					else
						running = false;
					break;
				case SDLK_RIGHT:
				case SDLK_SPACE:
				case SDLK_PAGEDOWN:
				case SDLK_N:
					go_to(list.index + 1);
					break;
				case SDLK_LEFT:
				case SDLK_BACKSPACE:
				case SDLK_PAGEUP:
					go_to(list.index - 1);
					break;
				case SDLK_HOME:
					go_to(0);
					break;
				case SDLK_END:
					go_to(list.count - 1);
					break;
				case SDLK_PLUS:
				case SDLK_EQUALS:
				case SDLK_KP_PLUS:
					zoom_at(W / 2.0f, H / 2.0f, zoom_step(true));
					break;
				case SDLK_MINUS:
				case SDLK_KP_MINUS:
					zoom_at(W / 2.0f, H / 2.0f, zoom_step(false));
					break;
				case SDLK_1:
					zoom_at(W / 2.0f, H / 2.0f, 1);
					posx = (W - img.w) / 2.0f;
					posy = (H - img.h) / 2.0f;
					break;
				case SDLK_0:
				case SDLK_Z:
					fit = true;
					break;
				case SDLK_F:
				case SDLK_F11:
					SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
					break;
				case SDLK_P:
					paused = !paused;
					next_frame_at = SDL_GetTicks();
					break;
				case SDLK_SLASH:
				case SDLK_QUESTION:
				case SDLK_F1:
					show_help = !show_help;
					break;
				}
				redraw = true;
				break;
			}
			case SDL_EVENT_MOUSE_WHEEL: {
				if (ev.wheel.y == 0)
					break;
				zoom_at(ev.wheel.mouse_x * density, ev.wheel.mouse_y * density, zoom_step(ev.wheel.y > 0));
				redraw = true;
				break;
			}
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
				if (ev.button.button == SDL_BUTTON_LEFT && ev.button.clicks == 2)
					SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
				else if (ev.button.button == SDL_BUTTON_LEFT)
					dragging = true;
				else if (ev.button.button == SDL_BUTTON_X1)
					go_to(list.index - 1);
				else if (ev.button.button == SDL_BUTTON_X2)
					go_to(list.index + 1);
				redraw = true;
				break;
			case SDL_EVENT_MOUSE_BUTTON_UP:
				if (ev.button.button == SDL_BUTTON_LEFT)
					dragging = false;
				break;
			case SDL_EVENT_MOUSE_MOTION:
				if (dragging && (ev.motion.state & SDL_BUTTON_LMASK)) {
					posx += ev.motion.xrel * density;
					posy += ev.motion.yrel * density;
					fit = false;
					redraw = true;
				}
				break;
			}
		} while (running && SDL_PollEvent(&ev));
	}
	free_image();
	SDL_DestroyTexture(checker);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return 0;
}
