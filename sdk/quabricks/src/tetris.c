#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "logsys.h"
#include "input.h"
#include "graphics.h"
#include "sound.h"
#include "hiscore.h"

// Where the local high-score table is stored (in the working directory)
#define HISCORE_PATH "highscores.txt"

// Size of the stage
#define STAGE_W 10
#define STAGE_H 20
// Number of lines to clear before going to the next level
#define LINES_PER_LEVEL 10
// "SPEED" is actually number of frames here
// Initial speed is the "gravity" for level 1
#define INITIAL_SPEED 60
// Gravity for soft drop when player holds the down button
#define DROP_SPEED 4
// Size for each individual block, and also effects a number of other things
#define BLOCK_SIZE 32
// Minimum time a between a piece touching the bottom and locking
#define LOCK_DELAY 30
// For delayed auto shift, wait SHIFT_DELAY frames first,
// then wait SHIFT_SPEED while left/right continues to be held
#define SHIFT_DELAY 12
#define SHIFT_SPEED 3

// Starting level bounds for the selection screen
#define MIN_LEVEL 1
#define MAX_LEVEL 15

// Score amounts rewarded for various actions
#define SCORE_SINGLE 100
#define SCORE_DOUBLE 300
#define SCORE_TRIPLE 500
#define SCORE_TETRIS 800
#define SCORE_EZ_TSPIN 100
#define SCORE_EZ_TSPIN_SINGLE 200
#define SCORE_TSPIN 400
#define SCORE_TSPIN_SINGLE 800
#define SCORE_TSPIN_DOUBLE 1200
#define SCORE_SOFT_DROP 1
#define SCORE_HARD_DROP 2

// Game mode, like using screens except a single variable switch instead
#define MODE_TITLE 0
#define MODE_OPTIONS 1
#define MODE_STAGE 2
#define MODE_GAMEOVER 3

// ---- Modern layout (all pixel values derived from BLOCK_SIZE) ----
#define MARGIN 28
#define PANEL_W 176
#define GAP 24
#define HEADER_H 84
#define STAGE_PX_W (STAGE_W * BLOCK_SIZE)     // 320
#define STAGE_PX_H (STAGE_H * BLOCK_SIZE)     // 640
#define LEFT_X  MARGIN                        // 28
#define STAGE_X (LEFT_X + PANEL_W + GAP)      // 228
#define RIGHT_X (STAGE_X + STAGE_PX_W + GAP)  // 572
#define STAGE_Y HEADER_H                      // 84
#define SCREEN_W (RIGHT_X + PANEL_W + MARGIN) // 776
#define SCREEN_H (STAGE_Y + STAGE_PX_H + 40)  // 764

// ---- UI palette ----
#define UI_BG_TOP    0x1B1E3AFF
#define UI_BG_BOT    0x0A0B18FF
#define UI_PANEL     0xFFFFFF12
#define UI_PANEL_BRD 0xFFFFFF2E
#define UI_BOARD_BG  0x05060FE0
#define UI_GRID      0xFFFFFF10
#define UI_TEXT      0xFFFFFFFF
#define UI_MUTED     0x9AA0C8FF
#define UI_ACCENT    0x8FA0FFFF

typedef unsigned char Uint8;
typedef signed char Sint8;
typedef unsigned short Uint16;
typedef unsigned int Uint32;

typedef unsigned char bool;
enum {false,true};

// This array describes the block configuration of a piece, for each shape
// and rotation in a 4x4 grid ordered left to right, then top to bottom
// Indexed: PieceDB[type][flip]
Uint16 PieceDB[7][4] = { // O, I, J, L, S, Z, T
	{0b0000011001100000,0b0000011001100000,0b0000011001100000,0b0000011001100000},
	{0b0100010001000100,0b0000111100000000,0b0010001000100010,0b0000000011110000},
	{0b0110010001000000,0b0000111000100000,0b0100010011000000,0b1000111000000000},
	{0b0100010001100000,0b0000111010000000,0b1100010001000000,0b0010111000000000},
	{0b0110110000000000,0b0100011000100000,0b0000011011000000,0b1000110001000000},
	{0b1100011000000000,0b0010011001000000,0b0000110001100000,0b0100110010000000},
	{0b0100111000000000,0b0100011001000000,0b0000111001000000,0b0100110001000000}
};
// Helper function to single out a block based on x and y position
Uint16 blockmask(int x, int y) { return 0x8000>>(x+y*4); }

// Soft pastel palette with a subtle 3D bevel applied at draw time
// Order: O, I, J, L, S, Z, T (matches PieceDB / stage type index)
Uint32 PieceColor[7] = {
	0xF7D774FF, // O - warm gold
	0x76D6E3FF, // I - aqua
	0x7A93E8FF, // J - periwinkle blue
	0xF2A96BFF, // L - soft orange
	0x93DB84FF, // S - mint green
	0xEB8A8AFF, // Z - coral red
	0xC08BE0FF, // T - lavender
};

// Represents an "instance" of a piece
typedef struct {
	// X and Y position (in blocks) on the stage
	Sint8 x; Sint8 y;
	// Type and flip value to index the PieceDB array
	Uint8 type; Uint8 flip;
} Piece;

// Current game mode (title screen, stage, game over screen, etc)
int gameMode = MODE_TITLE;
// Contains blocks/pieces that have fallen and bacame part of the stage
Uint8 stage[STAGE_W][STAGE_H];
// Random bag used to decide piece order
Uint8 randomBag[7], bagCount = 0;
// Block speed is the falling speed measured in frames between motions
// The block time is the elapsed frames which counts up to block speed
int blockSpeed = INITIAL_SPEED, blockTime = 0;
// Player's stats, score, level, etc
int score = 0, linesCleared = 0, totalLines = 0, level = 1, nextLevel = LINES_PER_LEVEL;
// Chosen starting level on the options screen
int startLevel = 1;
// Current piece controlled by player, held for later, and the queue
Piece piece, hold, queue[5];
// Whether the player held the previous piece, and has held any piece yet
bool holded = false, heldSomething = false;
// Whether game is paused or running. Not running means the game will exit
bool paused = false, running = true;
// True if the player is holding down to soft drop a piece
bool dropping = false;
// Frame count for delayed auto shift, and the direction the piece is being shifted
int autoShift = SHIFT_DELAY, shiftDirection = 0;

// ---- Presentation-only state (animations) ----
unsigned long frame = 0;   // global frame counter for animations
float clearFlash = 0;      // board flash intensity after a line clear
float dropShake = 0;       // small screen shake after a hard drop
float overAnim = 0;        // game-over sweep progress (0..1, then settles)
bool enteringName = false; // true while typing a name for a new high score
int recordRank = -1;       // rank of the score just inserted (-1 = none)
int menuSel = 0;           // cursor on the title menu
// Decorative slowly-falling blocks behind the menus
#define BG_BLOCKS 20
struct { float x, y, speed; int type; } bgBlocks[BG_BLOCKS];

// Function prototypes and order
void initialize();
void start_game();
void reset_game();
void fill_random_bag();
void move_piece_left();
void move_piece_right();
void move_piece_down();
void rotate_piece_left();
void rotate_piece_right();
void hard_drop();
void hold_piece();
void lock_piece();
bool validate_piece(Piece p);
bool check_lock(Piece p);
bool detect_tspin(Piece p);
bool wall_kick(Piece *p);
Piece ghost_piece(Piece p);
void reset_speed();
void clear_row();
void next_piece();
void trigger_game_over();
void update();
void update_title();
void update_options();
void update_stage();
void update_game_over();
void draw();
void draw_background();
void draw_panel(int x, int y, int w, int h);
void draw_piece_preview(Piece p, int cx, int cy, int cell);
void draw_stage();
void draw_hud();
void draw_title();
void draw_options();
void draw_pause();
void draw_game_over();
void draw_logo(int cx, int y, int size);

// Fill the board with an attractive demo stack (used for screenshots)
static void demo_fill_board() {
	for(int i = 0; i < STAGE_W; i++)
		for(int j = 0; j < STAGE_H; j++) stage[i][j] = 0;
	for(int j = STAGE_H - 9; j < STAGE_H; j++) {
		for(int i = 0; i < STAGE_W; i++) {
			if(i == 8 && j < STAGE_H - 1) continue; // keep a well open
			if((i * 5 + j * 3) % 11 == 0) continue; // scattered holes
			stage[i][j] = 1 + ((i * 3 + j * 2) % 7);
		}
	}
}

// Render one frame and save it to <dir>/<name>.bmp
static void capture(const char *dir, const char *name) {
	char path[512];
	snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
	graphics_request_screenshot(path);
	draw();
}

// Walk through every screen and save a screenshot of each, then exit
static void run_shots(const char *dir) {
	gameMode = MODE_TITLE;
	draw(); draw();
	capture(dir, "01_title");

	gameMode = MODE_OPTIONS; startLevel = 5;
	draw();
	capture(dir, "02_options");

	start_game();
	demo_fill_board();
	score = 12480; level = 5; totalLines = 42; nextLevel = 8;
	heldSomething = true; hold.type = 0; hold.flip = 0;   // held O piece
	piece.type = 6; piece.flip = 0; piece.x = 4; piece.y = 1; // T near top
	draw();
	capture(dir, "03_stage");

	paused = true;
	capture(dir, "04_pause");
	paused = false;

	gameMode = MODE_GAMEOVER;
	overAnim = 0.5f;   // mid-sweep, to show the losing animation
	capture(dir, "05_gameover_sweep");

	// Seed a few demo records only if none were loaded from disk
	if(hiscore_count() == 0) {
		hiscore_insert("ADA", 24000, 8, 61);
		hiscore_insert("LEO", 15200, 6, 40);
		hiscore_insert("SAM", 9800, 4, 25);
	}
	overAnim = 1.2f;

	// New-record name-entry screen
	enteringName = true; input_start_text();
	capture(dir, "06_gameover_name");
	enteringName = false; input_stop_text();

	// Settled game-over with the high-score table (our score highlighted)
	recordRank = hiscore_insert("YOU", score, level, totalLines);
	capture(dir, "07_gameover_scores");
}

// Headless-ish self test: auto-play pieces to exercise gravity, locking,
// line clears, scoring and game-over without needing user input
static void run_selftest() {
	// --- Verify clear_row() shifts the stack down and empties the top row ---
	for(int i = 0; i < STAGE_W; i++)
		for(int j = 0; j < STAGE_H; j++) stage[i][j] = 0;
	stage[3][5] = 7;          // a lone marker high up
	stage[7][0] = 4;          // a marker on the very top row
	clear_row(19);            // clears row 19, everything above shifts down one
	int ok = (stage[3][6] == 7) && (stage[3][5] == 0) && (stage[7][1] == 4);
	// top row must be fully empty after the shift
	for(int i = 0; i < STAGE_W; i++) if(stage[i][0] != 0) ok = 0;
	printf("SELFTEST clear_row: %s\n", ok ? "PASS" : "FAIL");

	// --- Regression: a piece locking above the top must not corrupt the board ---
	// (Old bug: stage[x][negative y] wrote into the previous column's bottom row,
	//  making the last piece "reappear at the bottom" on a top-out.)
	for(int i = 0; i < STAGE_W; i++)
		for(int j = 0; j < STAGE_H; j++) stage[i][j] = 0;
	gameMode = MODE_STAGE; score = 0;
	piece.type = 1; piece.flip = 0; piece.x = 3; piece.y = -2; // I above the top
	lock_piece();
	int lockOk = (stage[3][19] == 0) && (gameMode == MODE_GAMEOVER);
	printf("SELFTEST lockout: %s (stage[3][19]=%d gameover=%d)\n",
		lockOk ? "PASS" : "FAIL", stage[3][19], gameMode == MODE_GAMEOVER);
	if(enteringName) { input_stop_text(); enteringName = false; }

	// --- Verify level progression + speed curve ---
	// Clear two rows at a time with an O piece dropped into a 2-wide gap,
	// LINES_PER_LEVEL lines total should raise the level by exactly one.
	startLevel = 1;
	start_game();
	int spd_l1 = blockSpeed;
	int clears = LINES_PER_LEVEL / 2;   // 2 rows per O piece
	for(int k = 0; k < clears; k++) {
		for(int i = 0; i < STAGE_W; i++)
			for(int j = 0; j < STAGE_H; j++) stage[i][j] = 0;
		// Fill the bottom two rows except columns 4 and 5
		for(int j = STAGE_H - 2; j < STAGE_H; j++)
			for(int i = 0; i < STAGE_W; i++)
				if(i != 4 && i != 5) stage[i][j] = 1;
		piece.type = 0; piece.flip = 0; piece.x = 3; piece.y = 0; // O fills cols 4,5
		hard_drop();
	}
	int lvOk = (level == 2) && (totalLines == LINES_PER_LEVEL);
	int spd_l2 = INITIAL_SPEED - (2 - 1) * 4;
	printf("SELFTEST leveling: %s (level=%d lines=%d  speed L1=%d L2=%d)\n",
		(lvOk && spd_l2 < spd_l1) ? "PASS" : "FAIL",
		level, totalLines, spd_l1, spd_l2);

	start_game();
	int drops = 0;
	while(gameMode == MODE_STAGE && drops < 2000) {
		// nudge each piece toward a pseudo target column, then hard drop
		int target = (drops * 3) % STAGE_W;   // desired left edge of the piece
		if((drops & 3) == 0) rotate_piece_right();
		for(int step = 0; step < STAGE_W; step++) {
			int px = piece.x;
			if(piece.x > target) move_piece_left();
			else if(piece.x < target) move_piece_right();
			else break;
			if(piece.x == px) break; // movement blocked, stop nudging
		}
		hard_drop();
		drops++;
	}
	printf("SELFTEST: drops=%d score=%d level=%d lines=%d mode=%s\n",
		drops, score, level, totalLines,
		gameMode == MODE_GAMEOVER ? "GAMEOVER" : "STAGE");
}

// Entry point
int main(int argc, char *argv[]) {
	log_open("error.log");
	initialize();
	log_msgf(INFO, "Startup success.\n");
	if(argc >= 2 && strcmp(argv[1], "--selftest") == 0) {
		run_selftest();
		sound_quit();
		graphics_quit();
		log_close();
		return 0;
	}
	// Automated screenshot mode: ./quabricks --shots [output_dir]
	if(argc >= 2 && strcmp(argv[1], "--shots") == 0) {
		run_shots(argc >= 3 ? argv[2] : ".");
		sound_quit();
		graphics_quit();
		log_close();
		return 0;
	}
	while(running) {
		update();
		draw();
	}
	sound_quit();
	graphics_quit();
	log_msgf(INFO, "Process exited cleanly.\n");
	log_close();
	return 0;
}

// Create the game window and start stuff
void initialize() {
	srand((unsigned)time(NULL));
	graphics_init(SCREEN_W, SCREEN_H);
	sound_init();
	hiscore_load(HISCORE_PATH);
	// Seed the decorative background blocks
	for(int i = 0; i < BG_BLOCKS; i++) {
		bgBlocks[i].x = rand() % SCREEN_W;
		bgBlocks[i].y = rand() % SCREEN_H;
		bgBlocks[i].speed = 0.3f + (rand() % 100) / 100.0f;
		bgBlocks[i].type = rand() % 7;
	}
	gameMode = MODE_TITLE;
}

// Begin a fresh game at the chosen starting level
void start_game() {
	reset_game();
	level = startLevel;
	nextLevel = LINES_PER_LEVEL;
	reset_speed();
	gameMode = MODE_STAGE;
}

// Put values back to their defaults and start over
void reset_game() {
	// Clear the stage
	for(int i = 0; i < STAGE_W; i++) {
		for(int j = 0; j < STAGE_H; j++) {
			stage[i][j] = 0;
		}
	}
	// reset bag, queue, piece
	fill_random_bag();
	piece.type = randomBag[bagCount++];
	piece.flip = 0;
	piece.y = -2;
	piece.x = 3;
	for(int i = 0; i < 5; i++) {
		queue[i].type = randomBag[bagCount++];
		queue[i].flip = 0;
	}
	// Default values
	heldSomething = false;
	holded = false;
	score = 0;
	level = 1;
	nextLevel = LINES_PER_LEVEL;
	linesCleared = 0;
	totalLines = 0;
	paused = false;
	clearFlash = 0;
	dropShake = 0;
	overAnim = 0;
	reset_speed();
	next_piece();
}

// Regenerate the random bag, it contains the next 7 pieces to go in the queue
// It always contains one of each type of tetromino
void fill_random_bag() {
	Uint8 pool[7] = { 0, 1, 2, 3, 4, 5, 6 };
	for(int i = 0; i < 7; i++) {
		int j = rand() % (7 - i);
		randomBag[i] = pool[j];
		for(; j < 6; j++) {
			pool[j] = pool[j+1];
		}
	}
	bagCount = 0;
	log_msgf(TRACE, "FillBag: %hhu, %hhu, %hhu, %hhu, %hhu, %hhu, %hhu\n",
		randomBag[0], randomBag[1], randomBag[2], randomBag[3],
		randomBag[4], randomBag[5], randomBag[6]);
}

// Move to the left if possible
void move_piece_left() {
	Piece p = piece;
	p.x--;
	if(validate_piece(p)) {
		piece = p;
		sound_play(SFX_MOVE);
		// Reset timer if next fall will lock
		if(check_lock(p)) blockTime = 0;
	}
}

// Move to the right if possible
void move_piece_right() {
	Piece p = piece;
	p.x++;
	if(validate_piece(p)) {
		piece = p;
		sound_play(SFX_MOVE);
		// Reset timer if next fall will lock
		if(check_lock(piece)) blockTime = 0;
	}
}

// Move down or lock
void move_piece_down() {
	if(check_lock(piece)) {
		lock_piece(piece);
	} else {
		piece.y++;
		if(dropping) score += SCORE_SOFT_DROP;
	}
	blockTime = 0;
}

// Rotate to the left if possible
void rotate_piece_left() {
	Piece p = piece;
	if(p.flip == 0) p.flip = 3; else p.flip--;
	// If rotating makes the piece overlap, try to wall kick
	if(validate_piece(p) || wall_kick(&p)) {
		piece = p;
		sound_play(SFX_ROTATE);
		// Reset timer if next fall will lock
		if(check_lock(piece)) blockTime = 0;
	}
}

// Rotate to the right if possible
void rotate_piece_right() {
	Piece p = piece;
	if(p.flip == 3) p.flip = 0; else p.flip++;
	// If rotating makes the piece overlap, try to wall kick
	if(validate_piece(p) || wall_kick(&p)) {
		piece = p;
		sound_play(SFX_ROTATE);
		// Reset timer if next fall will lock
		if(check_lock(piece)) blockTime = 0;
	}
}

// Drop piece to the bottom and lock it
void hard_drop() {
	while (!check_lock(piece)) {
		piece.y++;
		score += SCORE_HARD_DROP;
	}
	sound_play(SFX_HARDDROP);
	dropShake = 6.0f;
	lock_piece();
}

// Switch current and hold block
void hold_piece() {
	if(holded) return; // Don't hold twice in a row
	piece.x = 3; piece.y = 0;
	if(heldSomething) {
		Piece temp = piece;
		piece = hold;
		hold = temp;
	} else {
		hold = piece;
		heldSomething = true;
		next_piece();
	}
	holded = true;
	sound_play(SFX_HOLD);
}

// Enter the game-over state; start name entry if the score makes the table
void trigger_game_over() {
	gameMode = MODE_GAMEOVER;
	overAnim = 0;
	enteringName = false;
	recordRank = -1;
	sound_play(SFX_GAMEOVER);
	if(hiscore_qualifies(score)) {
		enteringName = true;
		input_start_text();
	}
}

// Lock piece into stage and spawn the next
void lock_piece() {
	sound_play(SFX_LOCK);
	// Push piece data into stage. Cells above the top of the well mean the
	// piece locked out (top-out) — never write them (a negative row index
	// would corrupt a neighbouring column), just flag the loss.
	bool lockout = false;
	for(int i = 0; i < 4; i++) {
		for(int j = 0; j < 4; j++) {
			if(PieceDB[piece.type][piece.flip]&blockmask(i, j)) {
				int x = piece.x + i, y = piece.y + j;
				if(y < 0) { lockout = true; continue; }
				if(x >= 0 && x < STAGE_W && y < STAGE_H) stage[x][y] = piece.type+1;
			}
		}
	}
	// Clear any completed rows
	int rows_cleared = 0;
	for(int i = 0; i < STAGE_H; i++) {
		int filled = 0;
		for(int j = 0; j < STAGE_W; j++) if (stage[j][i] > 0) filled++;
		if (filled == STAGE_W) {
			clear_row(i);
			rows_cleared++;
		}
	}
	// Score rewards
	int reward = 0;
	// 3-corner T-spin
	if(piece.type == 6 && detect_tspin(piece)) {
		switch(rows_cleared) {
			case 0: reward += SCORE_TSPIN * level; break;
			case 1: reward += SCORE_TSPIN_SINGLE * level; break;
			case 2: reward += SCORE_TSPIN_DOUBLE * level; break;
		}
	} else {
		// Immobile (EZ) T-spin
		Piece p = piece;
		if(piece.type == 6 && wall_kick(&p)) {
			switch(rows_cleared) {
				case 0: reward += SCORE_EZ_TSPIN * level; break;
				case 1: reward += SCORE_EZ_TSPIN_SINGLE * level; break;
			}
		} else {
			// Rows clear, no T-spin
			switch (rows_cleared) {
				case 1: reward += SCORE_SINGLE * level; break;
				case 2: reward += SCORE_DOUBLE * level; break;
				case 3: reward += SCORE_TRIPLE * level; break;
				case 4: reward += SCORE_TETRIS * level; break;
			}
		}
	}
	score += reward;
	// Line clear feedback
	if(rows_cleared > 0) {
		clearFlash = 1.0f;
		sound_play(rows_cleared >= 4 ? SFX_TETRIS : SFX_LINECLEAR);
	}
	// Update line total and level
	int oldLevel = level;
	linesCleared += rows_cleared;
	totalLines += rows_cleared;
	nextLevel -= rows_cleared;
	if (nextLevel <= 0) {
		nextLevel += LINES_PER_LEVEL;
		level++;
	}
	if(level > oldLevel) sound_play(SFX_LEVELUP);
	// A piece that locked above the top ends the game
	if(lockout) { trigger_game_over(); return; }
	next_piece();
}

// Checks if the piece is overlapping with anything
bool validate_piece(Piece p) {
	for(int i = 0; i < 4; i++) {
		for(int j = 0; j < 4; j++) {
			int x = p.x + i, y = p.y + j;
			if(PieceDB[p.type][p.flip]&blockmask(i, j)) {
				if (x < 0 || x >= STAGE_W || y >= STAGE_H) return false;
				if (y >= 0 && stage[x][y] > 0) return false;
			}
		}
	}
	return true;
}

// Check if piece can be moved down any further
bool check_lock(Piece p) {
	p.y++;
	return !validate_piece(p);
}

// A cell counts as "occupied" for T-spin purposes if it holds a block or lies
// outside the play area (walls and floor count as corners)
static int cell_occupied(int x, int y) {
	if(x < 0 || x >= STAGE_W || y >= STAGE_H) return 1;
	if(y < 0) return 0;
	return stage[x][y] > 0;
}

bool detect_tspin(Piece p) {
	return cell_occupied(p.x, p.y) + cell_occupied(p.x + 2, p.y) +
		cell_occupied(p.x + 2, p.y + 2) + cell_occupied(p.x, p.y + 2) == 3;
}

// Try to push the piece out of an obstacles way
bool wall_kick(Piece *p) {
	// Left
	p->x -= 1;
	if(validate_piece(*p)) return true;
	// Right
	p->x += 2;
	if(validate_piece(*p)) return true;
	// Up
	p->x -= 1;
	p->y -= 1;
	if(validate_piece(*p)) return true;
	// Unable to wall kick, return piece to the way it was
	p->y += 1;
	return false;
}

// Returns a shadow to display where the piece will drop
Piece ghost_piece(Piece p) {
	while(!check_lock(p)) p.y++;
	return p;
}

// Adjusts the fall speed based on the current level. The curve is spread so
// every level from 1 to MAX_LEVEL has a distinct fall speed (the old
// `level * 5` curve saturated at the floor around level 12).
void reset_speed() {
	blockSpeed = INITIAL_SPEED - (level - 1) * 4;
	if(blockSpeed < DROP_SPEED) blockSpeed = DROP_SPEED;
}

// Clear a row and move down above rows
void clear_row(int row) {
	for(int i = row; i > 0; i--) {
		for(int j = 0; j < STAGE_W; j++) {
			stage[j][i] = stage[j][i-1];
		}
	}
	for(int j = 0; j < STAGE_W; j++) stage[j][0] = 0;
}

// Shift to the next block in the queue
void next_piece() {
	piece = queue[0];
	piece.y = -2;
	piece.x = 3;
	for(int i = 0; i < 4; i++) queue[i] = queue[i+1];
	// Grab piece from the bag, refill if it becomes empty
	queue[4].type = randomBag[bagCount++];
	if(bagCount == 7) fill_random_bag();
	holded = false; // Allow player to hold the next piece
	// End the game if the next piece overlaps
	if(!validate_piece(piece)) {
		trigger_game_over();
	}
	reset_speed();
}

// =====================================================================
//  Update
// =====================================================================

// Main update, handles events and calls relevant game mode update function
void update() {
	// Update keyboard input and events; close on window close
	if(input_update()) running = false;
	switch(gameMode) {
		case MODE_TITLE:    update_title();     break;
		case MODE_OPTIONS:  update_options();   break;
		case MODE_STAGE:    update_stage();     break;
		case MODE_GAMEOVER: update_game_over(); break;
	}
}

void update_title() {
	if((key.enter && !oldKey.enter) || (key.space && !oldKey.space)) {
		sound_play(SFX_MENU_SELECT);
		gameMode = MODE_OPTIONS;
	}
	if(key.esc && !oldKey.esc) running = false;
}

void update_options() {
	if(key.left && !oldKey.left) {
		if(startLevel > MIN_LEVEL) { startLevel--; sound_play(SFX_MENU_MOVE); }
	}
	if(key.right && !oldKey.right) {
		if(startLevel < MAX_LEVEL) { startLevel++; sound_play(SFX_MENU_MOVE); }
	}
	if(key.down && !oldKey.down) {
		if(startLevel > MIN_LEVEL) { startLevel--; sound_play(SFX_MENU_MOVE); }
	}
	if(key.up && !oldKey.up) {
		if(startLevel < MAX_LEVEL) { startLevel++; sound_play(SFX_MENU_MOVE); }
	}
	if(key.enter && !oldKey.enter) {
		sound_play(SFX_MENU_SELECT);
		start_game();
	}
	if(key.esc && !oldKey.esc) {
		sound_play(SFX_MENU_MOVE);
		gameMode = MODE_TITLE;
	}
}

// Update actions when the game is being played
void update_stage() {
	if(key.esc && !oldKey.esc) { gameMode = MODE_TITLE; return; }
	if(key.enter && !oldKey.enter) { paused = !paused; sound_play(SFX_PAUSE); }
	// Don't update the rest if the game is paused
	if(paused) return;
	// Moving left and right
	if(key.left && !oldKey.left) {
		move_piece_left();
		shiftDirection = -1;
		autoShift = SHIFT_DELAY;
	} else if(key.right && !oldKey.right) {
		move_piece_right();
		shiftDirection = 1;
		autoShift = SHIFT_DELAY;
	}
	// Delayed Auto Shift
	if(key.right - key.left == shiftDirection) {
		autoShift--;
		if(autoShift == 0) {
			autoShift = SHIFT_SPEED;
			if(key.left) move_piece_left();
			else if(key.right) move_piece_right();
		}
	}
	// Rotating block
	if(key.z && !oldKey.z) rotate_piece_left();
	if(key.x && !oldKey.x) rotate_piece_right();
	if(key.up && !oldKey.up) rotate_piece_right();
	// Drop and Lock
	if(key.space && !oldKey.space) hard_drop();
	// Hold a block and save it for later (C or Shift)
	if((key.shift && !oldKey.shift) || (key.c && !oldKey.c)) hold_piece();
	// If we hold the down key fall faster
	if(key.down && !oldKey.down) {
		blockSpeed = DROP_SPEED;
		dropping = true;
		sound_play(SFX_SOFTDROP);
		move_piece_down();
	} else if(!key.down && oldKey.down) {
		reset_speed();
		dropping = false;
	}
	// Push block down according to speed
	blockTime++;
	if(blockTime >= blockSpeed) {
		// No matter the gravity, always wait at least half a second
		// before locking
		if(!check_lock(piece) || blockTime >= LOCK_DELAY || key.down) {
			move_piece_down();
		}
	}
}

// Game over screen
void update_game_over() {
	if(enteringName) {
		// Enter (or Esc) commits the name — blank is fine, it becomes "---"
		if((key.enter && !oldKey.enter) || (key.esc && !oldKey.esc)) {
			recordRank = hiscore_insert(input_text(), score, level, totalLines);
			hiscore_save(HISCORE_PATH);
			input_stop_text();
			enteringName = false;
			sound_play(SFX_MENU_SELECT);
		}
		return;
	}
	if(key.enter && !oldKey.enter) {
		sound_play(SFX_MENU_SELECT);
		gameMode = MODE_TITLE;
	}
	if(key.esc && !oldKey.esc) running = false;
}

// =====================================================================
//  Draw
// =====================================================================

void draw() {
	frame++;
	if(clearFlash > 0) clearFlash -= 0.06f;
	if(clearFlash < 0) clearFlash = 0;
	if(dropShake > 0) dropShake -= 0.6f;
	if(dropShake < 0) dropShake = 0;
	if(gameMode == MODE_GAMEOVER && overAnim < 1.2f) overAnim += 0.045f;

	draw_background();
	switch(gameMode) {
		case MODE_TITLE:    draw_title();     break;
		case MODE_OPTIONS:  draw_options();   break;
		case MODE_STAGE:
			draw_stage();
			draw_hud();
			if(paused) draw_pause();
			break;
		case MODE_GAMEOVER:
			draw_stage();
			draw_hud();
			draw_game_over();
			break;
	}
	graphics_flip();
}

// Gradient backdrop with slowly drifting translucent tetromino blocks
void draw_background() {
	graphics_fill_gradient(0, 0, SCREEN_W, SCREEN_H, UI_BG_TOP, UI_BG_BOT);
	// Animated blocks only behind the menus (keeps the play area clean)
	if(gameMode == MODE_TITLE || gameMode == MODE_OPTIONS) {
		for(int i = 0; i < BG_BLOCKS; i++) {
			bgBlocks[i].y += bgBlocks[i].speed;
			if(bgBlocks[i].y > SCREEN_H + 40) {
				bgBlocks[i].y = -40;
				bgBlocks[i].x = rand() % SCREEN_W;
				bgBlocks[i].type = rand() % 7;
			}
			unsigned int c = color_alpha(PieceColor[bgBlocks[i].type], 26);
			graphics_fill_round_rect((int)bgBlocks[i].x, (int)bgBlocks[i].y,
				28, 28, 6, c);
		}
	}
}

// A translucent rounded card with a subtle border
void draw_panel(int x, int y, int w, int h) {
	graphics_fill_round_rect(x, y, w, h, 12, UI_PANEL);
	graphics_round_rect_outline(x, y, w, h, 12, 1, UI_PANEL_BRD);
}

// Compute the occupied bounding box of a piece within its 4x4 grid
static void piece_bounds(Piece p, int *mi, int *ma, int *mj, int *mb) {
	int minx = 4, maxx = -1, miny = 4, maxy = -1;
	for(int i = 0; i < 4; i++) {
		for(int j = 0; j < 4; j++) {
			if(PieceDB[p.type][p.flip] & blockmask(i, j)) {
				if(i < minx) minx = i;
				if(i > maxx) maxx = i;
				if(j < miny) miny = j;
				if(j > maxy) maxy = j;
			}
		}
	}
	*mi = minx; *ma = maxx; *mj = miny; *mb = maxy;
}

// Draw a piece centered around (cx, cy) using the given cell size
void draw_piece_preview(Piece p, int cx, int cy, int cell) {
	int mi, ma, mj, mb;
	piece_bounds(p, &mi, &ma, &mj, &mb);
	int gridW = (ma - mi + 1) * cell;
	int gridH = (mb - mj + 1) * cell;
	int originX = cx - gridW / 2 - mi * cell;
	int originY = cy - gridH / 2 - mj * cell;
	for(int i = 0; i < 4; i++) {
		for(int j = 0; j < 4; j++) {
			if(PieceDB[p.type][p.flip] & blockmask(i, j)) {
				graphics_draw_block(originX + i * cell, originY + j * cell,
					cell, PieceColor[p.type], 255);
			}
		}
	}
}

void draw_stage() {
	int shakeX = 0, shakeY = 0;
	if(dropShake > 0 && gameMode == MODE_STAGE)
		shakeY = (int)(dropShake * ((frame & 1) ? 1 : -1));

	int bx = STAGE_X + shakeX, by = STAGE_Y + shakeY;
	// Board backing card with a soft outer glow
	graphics_fill_round_rect(bx - 6, by - 6, STAGE_PX_W + 12, STAGE_PX_H + 12, 14,
		color_alpha(UI_ACCENT, 40));
	graphics_fill_round_rect(bx, by, STAGE_PX_W, STAGE_PX_H, 10, UI_BOARD_BG);

	// Faint grid lines
	for(int i = 1; i < STAGE_W; i++)
		graphics_fill_rect(bx + i * BLOCK_SIZE, by, 1, STAGE_PX_H, UI_GRID);
	for(int j = 1; j < STAGE_H; j++)
		graphics_fill_rect(bx, by + j * BLOCK_SIZE, STAGE_PX_W, 1, UI_GRID);

	// Locked blocks
	for (int i = 0; i < STAGE_W; i++) {
		for (int j = 0; j < STAGE_H; j++) {
			if (stage[i][j] == 0) continue;
			int c = stage[i][j] - 1;
			graphics_draw_block(i * BLOCK_SIZE + bx, j * BLOCK_SIZE + by,
				BLOCK_SIZE, PieceColor[c], 255);
		}
	}
	// Only draw the falling/ghost pieces while actively playing
	if(gameMode == MODE_STAGE) {
		Piece shadow = ghost_piece(piece);
		for(int i = 0; i < 4; i++) {
			for(int j = 0; j < 4; j++) {
				if(!(PieceDB[shadow.type][shadow.flip] & blockmask(i, j))) continue;
				if(shadow.y + j < 0) continue;
				graphics_draw_ghost((shadow.x + i) * BLOCK_SIZE + bx,
					(shadow.y + j) * BLOCK_SIZE + by, BLOCK_SIZE,
					PieceColor[shadow.type]);
			}
		}
		for(int i = 0; i < 4; i++) {
			for(int j = 0; j < 4; j++) {
				if(!(PieceDB[piece.type][piece.flip] & blockmask(i, j))) continue;
				if(piece.y + j < 0) continue;
				graphics_draw_block((piece.x + i) * BLOCK_SIZE + bx,
					(piece.y + j) * BLOCK_SIZE + by, BLOCK_SIZE,
					PieceColor[piece.type], 255);
			}
		}
	}
	// Line-clear flash over the board
	if(clearFlash > 0) {
		graphics_fill_round_rect(bx, by, STAGE_PX_W, STAGE_PX_H, 10,
			color_alpha(0xFFFFFF00, (unsigned char)(clearFlash * 90)));
	}
}

void draw_hud() {
	char buf[32];
	// --- Header: big score centered over the board ---
	int cx = STAGE_X + STAGE_PX_W / 2;
	graphics_text("SCORE", cx, 14, 15, FONT_BOLD, UI_MUTED, ALIGN_CENTER);
	snprintf(buf, sizeof(buf), "%d", score);
	graphics_text(buf, cx, 30, 40, FONT_BLACK, UI_TEXT, ALIGN_CENTER);

	// --- HOLD card (top-left) ---
	int hx = LEFT_X, hy = STAGE_Y, hw = PANEL_W, hh = 132;
	draw_panel(hx, hy, hw, hh);
	graphics_text("HOLD", hx + 14, hy + 10, 15, FONT_BOLD, UI_MUTED, ALIGN_LEFT);
	if(heldSomething) {
		unsigned int save = PieceColor[hold.type];
		if(holded) PieceColor[hold.type] = color_scale(save, 0.55f); // dim if locked out
		draw_piece_preview(hold, hx + hw / 2, hy + 78, 22);
		PieceColor[hold.type] = save;
	}

	// --- STATS card (below hold) ---
	int sx = LEFT_X, sy = STAGE_Y + hh + GAP;
	int sh = (STAGE_Y + STAGE_PX_H) - sy;
	draw_panel(sx, sy, PANEL_W, sh);
	int tx = sx + 16, ty = sy + 16;
	graphics_text("LEVEL", tx, ty, 14, FONT_BOLD, UI_MUTED, ALIGN_LEFT);
	snprintf(buf, sizeof(buf), "%d", level);
	graphics_text(buf, sx + PANEL_W - 16, ty - 4, 26, FONT_BLACK, UI_ACCENT, ALIGN_RIGHT);
	ty += 40;
	graphics_text("LINES", tx, ty, 14, FONT_BOLD, UI_MUTED, ALIGN_LEFT);
	snprintf(buf, sizeof(buf), "%d", totalLines);
	graphics_text(buf, sx + PANEL_W - 16, ty - 4, 26, FONT_BLACK, UI_TEXT, ALIGN_RIGHT);
	ty += 40;
	graphics_text("NEXT LVL IN", tx, ty, 14, FONT_BOLD, UI_MUTED, ALIGN_LEFT);
	snprintf(buf, sizeof(buf), "%d", nextLevel);
	graphics_text(buf, sx + PANEL_W - 16, ty - 4, 26, FONT_BLACK, UI_TEXT, ALIGN_RIGHT);

	// Controls hint at the bottom of the stats card
	int cy2 = sy + sh - 154;
	graphics_text("CONTROLS", tx, cy2, 13, FONT_BOLD, UI_MUTED, ALIGN_LEFT);
	const char *rows[][2] = {
		{"Move", "\xE2\x86\x90 \xE2\x86\x92"},
		{"Soft drop", "\xE2\x86\x93"},
		{"Hard drop", "Space"},
		{"Rotate", "Z X \xE2\x86\x91"},
		{"Hold", "C / Shift"},
		{"Pause", "Enter"},
	};
	int ry = cy2 + 22;
	for(int i = 0; i < 6; i++) {
		graphics_text(rows[i][0], tx, ry, 14, FONT_REG, UI_MUTED, ALIGN_LEFT);
		graphics_text(rows[i][1], sx + PANEL_W - 16, ry, 14, FONT_BOLD, UI_TEXT, ALIGN_RIGHT);
		ry += 21;
	}

	// --- NEXT card (right) ---
	int nx = RIGHT_X, ny = STAGE_Y, nw = PANEL_W, nh = STAGE_PX_H;
	draw_panel(nx, ny, nw, nh);
	graphics_text("NEXT", nx + 14, ny + 10, 15, FONT_BOLD, UI_MUTED, ALIGN_LEFT);
	int slot0 = ny + 74;
	int slotH = 112;
	for(int q = 0; q < 5; q++) {
		int cell = q == 0 ? 24 : 20; // emphasize the upcoming piece
		draw_piece_preview(queue[q], nx + nw / 2, slot0 + q * slotH, cell);
	}
}

// Draw a chunky "QUABRICKS" wordmark with per-letter piece colors
// (colors wrap so names longer than the 7-piece palette still look right)
void draw_logo(int cx, int y, int size) {
	const char *letters = "QUABRICKS";
	// Measure total width first (letters + small spacing)
	int spacing = size / 12;
	int total = 0;
	char s[2] = {0, 0};
	for(int i = 0; letters[i]; i++) {
		s[0] = letters[i];
		total += graphics_text_width(s, size, FONT_BLACK) + spacing;
	}
	total -= spacing;
	int x = cx - total / 2;
	for(int i = 0; letters[i]; i++) {
		s[0] = letters[i];
		int w = graphics_text_width(s, size, FONT_BLACK);
		// subtle drop shadow
		graphics_text(s, x + 3, y + 4, size, FONT_BLACK, color_alpha(0x000000FF, 120), ALIGN_LEFT);
		graphics_text(s, x, y, size, FONT_BLACK, PieceColor[i % 7], ALIGN_LEFT);
		x += w + spacing;
	}
}

void draw_title() {
	int cx = SCREEN_W / 2;
	draw_logo(cx, SCREEN_H / 2 - 150, 66);
	graphics_text("MODERN EDITION", cx, SCREEN_H / 2 - 46, 22, FONT_BOLD, UI_ACCENT, ALIGN_CENTER);

	// Pulsing prompt
	float pulse = 0.6f + 0.4f * (float)((frame / 30) % 2);
	graphics_text("Press ENTER to Play", cx, SCREEN_H / 2 + 50, 26, FONT_BOLD,
		color_alpha(UI_TEXT, (unsigned char)(255 * pulse)), ALIGN_CENTER);
	graphics_text("Arrow keys move \xE2\x80\xA2 Z/X rotate \xE2\x80\xA2 Space hard-drop \xE2\x80\xA2 C hold",
		cx, SCREEN_H / 2 + 110, 15, FONT_REG, UI_MUTED, ALIGN_CENTER);

	// Best score, if any
	if(hiscore_best() > 0) {
		const HiScore *b = hiscore_get(0);
		char buf[48];
		snprintf(buf, sizeof(buf), "BEST   %d   %s", b->score, b->name);
		graphics_text(buf, cx, SCREEN_H / 2 + 168, 18, FONT_BOLD, UI_ACCENT, ALIGN_CENTER);
	}

	graphics_text("Esc to quit", cx, SCREEN_H - 44, 14, FONT_REG,
		color_alpha(UI_MUTED, 160), ALIGN_CENTER);
}

void draw_options() {
	int cx = SCREEN_W / 2;
	draw_logo(cx, 70, 52);
	graphics_text("SELECT STARTING LEVEL", cx, 190, 24, FONT_BOLD, UI_TEXT, ALIGN_CENTER);

	// Big selected number with arrows
	int midY = 250;
	char buf[8];
	snprintf(buf, sizeof(buf), "%d", startLevel);
	// Rounded pill behind the number
	graphics_fill_round_rect(cx - 70, midY, 140, 120, 18, UI_PANEL);
	graphics_round_rect_outline(cx - 70, midY, 140, 120, 18, 2, color_alpha(UI_ACCENT, 180));
	graphics_text(buf, cx, midY + 22, 72, FONT_BLACK, UI_ACCENT, ALIGN_CENTER);
	// Left / right selector triangles (drawn, since the font lacks these glyphs)
	int ay = midY + 60, ah = 22;
	unsigned int lcol = startLevel > MIN_LEVEL ? UI_TEXT : color_alpha(UI_MUTED, 70);
	unsigned int rcol = startLevel < MAX_LEVEL ? UI_TEXT : color_alpha(UI_MUTED, 70);
	graphics_fill_triangle(cx - 108, ay, cx - 108, ay + ah, cx - 108 - 18, ay + ah / 2, lcol);
	graphics_fill_triangle(cx + 108, ay, cx + 108, ay + ah, cx + 108 + 18, ay + ah / 2, rcol);

	// Level pips
	int pipW = 16, pipGap = 6;
	int totalPips = MAX_LEVEL - MIN_LEVEL + 1;
	int startX = cx - (totalPips * (pipW + pipGap) - pipGap) / 2;
	for(int i = 0; i < totalPips; i++) {
		int lvl = MIN_LEVEL + i;
		unsigned int c = lvl <= startLevel ? UI_ACCENT : UI_PANEL_BRD;
		graphics_fill_round_rect(startX + i * (pipW + pipGap), midY + 150, pipW, 8, 4, c);
	}

	graphics_text("\xE2\x86\x90 / \xE2\x86\x92  change     ENTER  start     ESC  back",
		cx, midY + 200, 16, FONT_REG, UI_MUTED, ALIGN_CENTER);
	graphics_text("Higher levels start faster and score more.",
		cx, midY + 232, 14, FONT_REG, color_alpha(UI_MUTED, 170), ALIGN_CENTER);
}

void draw_pause() {
	// Dim the whole screen
	graphics_fill_rect(0, 0, SCREEN_W, SCREEN_H, color_alpha(0x000000FF, 170));
	int cx = SCREEN_W / 2, cy = SCREEN_H / 2;
	graphics_text("PAUSED", cx, cy - 40, 56, FONT_BLACK, UI_TEXT, ALIGN_CENTER);
	graphics_text("Press ENTER to resume", cx, cy + 34, 20, FONT_BOLD, UI_MUTED, ALIGN_CENTER);
	graphics_text("Esc to return to menu", cx, cy + 66, 15, FONT_REG,
		color_alpha(UI_MUTED, 180), ALIGN_CENTER);
}

void draw_game_over() {
	// Sweep the playfield with muted blocks from the bottom up
	int rows = (int)(overAnim * (STAGE_H + 1));
	if(rows > STAGE_H) rows = STAGE_H;
	for(int r = 0; r < rows; r++) {
		int j = STAGE_H - 1 - r;
		for(int i = 0; i < STAGE_W; i++) {
			graphics_draw_block(i * BLOCK_SIZE + STAGE_X, j * BLOCK_SIZE + STAGE_Y,
				BLOCK_SIZE, 0x3A3F5AFF, 255);
		}
	}
	// Hold on the sweep until it reaches the top, then reveal the summary
	if(overAnim < 1.0f) return;

	graphics_fill_rect(0, 0, SCREEN_W, SCREEN_H, color_alpha(0x000000FF, 195));
	int cx = SCREEN_W / 2;
	char buf[64];

	if(enteringName) {
		// New-record name entry
		int y = 150;
		graphics_text("NEW RECORD!", cx, y, 48, FONT_BLACK, UI_ACCENT, ALIGN_CENTER);
		graphics_text("FINAL SCORE", cx, y + 78, 16, FONT_BOLD, UI_MUTED, ALIGN_CENTER);
		snprintf(buf, sizeof(buf), "%d", score);
		graphics_text(buf, cx, y + 98, 44, FONT_BLACK, UI_TEXT, ALIGN_CENTER);

		graphics_text("ENTER YOUR NAME  (optional)", cx, y + 178, 15, FONT_BOLD, UI_MUTED, ALIGN_CENTER);
		// Input field
		int fw = 300, fh = 56, fx = cx - fw / 2, fy = y + 202;
		graphics_fill_round_rect(fx, fy, fw, fh, 12, UI_PANEL);
		graphics_round_rect_outline(fx, fy, fw, fh, 12, 2, color_alpha(UI_ACCENT, 200));
		const char *nm = input_text();
		int tw = graphics_text_width(nm, 28, FONT_BOLD);
		int tx = cx - tw / 2;
		if(nm[0]) graphics_text(nm, tx, fy + 12, 28, FONT_BOLD, UI_TEXT, ALIGN_LEFT);
		// Blinking caret
		if((frame / 18) % 2 == 0)
			graphics_fill_rect(tx + tw + 2, fy + 14, 3, 30, UI_TEXT);

		graphics_text("Press ENTER to save", cx, fy + fh + 22, 16, FONT_BOLD, UI_ACCENT, ALIGN_CENTER);
		return;
	}

	// Standard game-over summary with the high-score table
	int y = 92;
	graphics_text("GAME OVER", cx, y, 52, FONT_BLACK, COLOR_RED, ALIGN_CENTER);
	graphics_text("FINAL SCORE", cx, y + 72, 15, FONT_BOLD, UI_MUTED, ALIGN_CENTER);
	snprintf(buf, sizeof(buf), "%d", score);
	graphics_text(buf, cx, y + 90, 40, FONT_BLACK, UI_TEXT, ALIGN_CENTER);
	snprintf(buf, sizeof(buf), "Level %d  \xE2\x80\xA2  %d lines", level, totalLines);
	graphics_text(buf, cx, y + 142, 16, FONT_REG, UI_MUTED, ALIGN_CENTER);

	// High-score table
	int n = hiscore_count();
	if(n > 0) {
		int tblY = y + 186;
		graphics_text("HIGH SCORES", cx, tblY, 15, FONT_BOLD, UI_ACCENT, ALIGN_CENTER);
		int colL = cx - 150, colR = cx + 150;
		int rowY = tblY + 30;
		for(int i = 0; i < n; i++) {
			const HiScore *e = hiscore_get(i);
			// highlight the row we just added
			unsigned int col = (i == recordRank) ? UI_ACCENT : UI_TEXT;
			int weight = (i == recordRank) ? FONT_BLACK : FONT_BOLD;
			snprintf(buf, sizeof(buf), "%d.", i + 1);
			graphics_text(buf, colL, rowY, 18, FONT_BOLD, UI_MUTED, ALIGN_LEFT);
			graphics_text(e->name, colL + 34, rowY, 18, weight, col, ALIGN_LEFT);
			snprintf(buf, sizeof(buf), "%d", e->score);
			graphics_text(buf, colR, rowY, 18, weight, col, ALIGN_RIGHT);
			rowY += 30;
		}
	}
	graphics_text("Press ENTER for menu", cx, SCREEN_H - 54, 18, FONT_BOLD, UI_ACCENT, ALIGN_CENTER);
}
