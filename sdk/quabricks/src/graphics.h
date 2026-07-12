#ifndef TETRIS_GRAPHICS
#define TETRIS_GRAPHICS

// Colors are packed 0xRRGGBBAA
#define COLOR_BLACK  0x000000FF
#define COLOR_WHITE  0xFFFFFFFF
#define COLOR_RED    0xFF5A5AFF

// Text weight (maps to a bundled Lato face)
enum { FONT_REG, FONT_BOLD, FONT_BLACK };
// Text alignment relative to the given x
enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

void graphics_init(int x, int y);
void graphics_quit();
void graphics_flip();

// Request that the next presented frame be saved to `path` (BMP). Captured
// just before present so the backbuffer is intact. Used for automated shots.
void graphics_request_screenshot(const char *path);

// --- Color helpers (operate on packed 0xRRGGBBAA) ---
// Multiply RGB by factor (darken < 1, brighten > 1), keep alpha
unsigned int color_scale(unsigned int c, float f);
// Blend RGB toward white by amount [0..1], keep alpha
unsigned int color_lighten(unsigned int c, float amt);
// Replace the alpha byte
unsigned int color_alpha(unsigned int c, unsigned char a);

// --- Primitives ---
void graphics_set_color(unsigned int color);
void graphics_draw_rect(int x, int y, int w, int h);
void graphics_fill_rect(int x, int y, int w, int h, unsigned int color);
// Vertical gradient fill from top color to bottom color
void graphics_fill_gradient(int x, int y, int w, int h, unsigned int top, unsigned int bottom);
// Filled rounded rectangle
void graphics_fill_round_rect(int x, int y, int w, int h, int radius, unsigned int color);
// Rounded rectangle outline of the given thickness
void graphics_round_rect_outline(int x, int y, int w, int h, int radius, int thickness, unsigned int color);
// Filled triangle from three points
void graphics_fill_triangle(int x1, int y1, int x2, int y2, int x3, int y3, unsigned int color);

// --- Game blocks ---
// A glossy, beveled 3D tetromino cell in the given base color
void graphics_draw_block(int x, int y, int size, unsigned int base, unsigned char alpha);
// A translucent outlined ghost cell
void graphics_draw_ghost(int x, int y, int size, unsigned int base);

// --- Text (Lato, rendered on demand and cached) ---
void graphics_text(const char *str, int x, int y, int size, int weight,
	unsigned int color, int align);
int graphics_text_width(const char *str, int size, int weight);
int graphics_text_height(int size, int weight);

#endif
