#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>


#include <SDL2/SDL.h>
#define RGBA(hex) \
	(hex >> (3*8)) & 0xFF, \
	(hex >> (2*8)) & 0xFF, \
	(hex >> (1*8)) & 0xFF, \
	(hex >> (0*8)) & 0xFF

const int SCR_WIDTH  = 64 * 20;
const int SCR_HEIGHT = 32 * 20;


#define W(hextet) (hextet >> 12)
#define X(hextet) ((hextet & 0x0F00) >> 8)
#define Y(hextet) ((hextet & 0x00F0) >> 4)
#define Z(hextet) (hextet & 0x000F)
#define NN(hextet) (hextet & 0x00FF)
#define NNN(hextet) (hextet & 0x0FFF)

enum REGISTER {
	V0, V1, V2, V3, V4, V5, V6, V7, V8, V9, VA, VB, VC, VD, VE, VF
};
uint8_t reg[16];
uint16_t VI;
uint8_t ram[0x1000] = { // sprites 0-F
	0xF0, 0x90, 0x90, 0x90, 0xF0,
	0x20, 0x60, 0x20, 0x20, 0x70,
	0xF0, 0x10, 0xF0, 0x80, 0xF0,
	0xF0, 0x10, 0xF0, 0x10, 0xF0,
	0x90, 0x90, 0xF0, 0x10, 0x10,
	0xF0, 0x80, 0xF0, 0x10, 0xF0,
	0xF0, 0x80, 0xF0, 0x90, 0xF0,
	0xF0, 0x10, 0x20, 0x40, 0x40,
	0xF0, 0x90, 0xF0, 0x90, 0xF0,
	0xF0, 0x90, 0xF0, 0x10, 0xF0,
	0xF0, 0x90, 0xF0, 0x90, 0x90,
	0xE0, 0x90, 0xE0, 0x90, 0xE0,
	0xF0, 0x80, 0x80, 0x80, 0xF0,
	0xE0, 0x90, 0x90, 0x90, 0xE0,
	0xF0, 0x80, 0xF0, 0x80, 0xF0,
	0xF0, 0x80, 0xF0, 0x80, 0x80
};

struct {
	uint16_t pc, sp;
	uint8_t delay; // no sound support
} cpu;


#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte) \
  ((byte) & 0x80 ? '0' : ' '), \
  ((byte) & 0x40 ? '0' : ' '), \
  ((byte) & 0x20 ? '0' : ' '), \
  ((byte) & 0x10 ? '0' : ' '), \
  ((byte) & 0x08 ? '0' : ' '), \
  ((byte) & 0x04 ? '0' : ' '), \
  ((byte) & 0x02 ? '0' : ' '), \
  ((byte) & 0x01 ? '0' : ' ')

void print_byte(uint8_t byte) {
	printf(BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(byte));
}

void print_regs() {
	for (int VX = V0; VX <= VF; VX++)
		printf("V%X = %3d\n", VX, reg[VX]);
	printf("VI = 0x%3X\n", VI);
}

#define INRANGE(x, l, r) (((l) <= (x)) && ((x) <= (r)))

uint8_t KEYMAP[26] = {
	['o' - 'a'] = 0,
	['q' - 'a'] = 1,
	['w' - 'a'] = 2,
	['e' - 'a'] = 3,
	['a' - 'a'] = 4,
	['s' - 'a'] = 5,
	['d' - 'a'] = 6,
	['z' - 'a'] = 7,
	['x' - 'a'] = 8,
	['c' - 'a'] = 9,
	['r' - 'a'] = 10,
	['f' - 'a'] = 11,
	['v' - 'a'] = 12,
	['t' - 'a'] = 13,
	['g' - 'a'] = 14,
	['b' - 'a'] = 15
};

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("usage: %s <filename>\n", argv[0]);
		return 1;
	}

	FILE *fp = fopen(argv[1], "rb");
	if (!fp) {
		puts("Couldn't open file");
		return 1;
	}
	fseek(fp, 0, SEEK_END);
	size_t sz_bytes = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	fread(ram + 0x200, 1, sz_bytes, fp);
	fclose(fp);


	goto main;
err:
	puts(SDL_GetError());
	return 1;
main:
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		goto err;

	SDL_Window *window = SDL_CreateWindow(
		"HELLO",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		SCR_WIDTH,
		SCR_HEIGHT,
		0 //SDL_WINDOW_RESIZABLE
	);
	if (!window) goto err;
	SDL_Surface *screen = SDL_GetWindowSurface(window);

	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
		0,
		64 + 8, // extra for overflow
		32 + 16,
		1,
		SDL_PIXELFORMAT_INDEX8
	);
	if (!surface) goto err;
	SDL_Color colors[] = { {RGBA(0)}, {RGBA(0xFFFFFFFF)} };
	SDL_SetPaletteColors(surface->format->palette, colors, 0, 2);
	uint8_t (*pixels)[64 + 8] = (uint8_t (*)[64 + 8]) surface->pixels;


	cpu.pc = 0x200;
	cpu.sp = 0x200;

	SDL_Event e;
	uint64_t ms_per_frame = 1000 * 1.0/60; // 60 FPS
	uint64_t ms_per_sprite = 1000 * 1.0/60; // drawing at
	uint64_t start_sdl = SDL_GetPerformanceCounter();
	uint64_t start_sprite = start_sdl;

	int keystate[16] = {0};
	bool quit = false;

	for (; !quit; ) { // game loop
		uint64_t end_sdl = SDL_GetPerformanceCounter();
		uint64_t elapsed_sdl_ms = 1000 * (end_sdl - start_sdl) * 1.0/SDL_GetPerformanceFrequency();
		if (elapsed_sdl_ms >= ms_per_frame) {
			start_sdl = SDL_GetPerformanceCounter();

			cpu.delay -= !!(cpu.delay);
			printf("DT: %d\n", cpu.delay);

			SDL_FillRect(screen, NULL, 0x00000000);
			SDL_Rect rect = {0, 0, 64, 32};
			SDL_BlitScaled(surface, &rect, screen, NULL);
			SDL_UpdateWindowSurface(window);

			// uint64_t frametime = SDL_GetTicks() - tmpstart;
			// printf("FPS: %.3f\n", 1000.0/frametime);
			// tmpstart = SDL_GetTicks();
		}

		for (; SDL_PollEvent(&e); ) {
			switch (e.type) {
			case SDL_QUIT:
				quit = true;
				break;
			case SDL_KEYDOWN:
				if (strchr("qweasdzxcrfvtgbo", e.key.keysym.sym)) {
					keystate[KEYMAP[e.key.keysym.sym - 'a']] = 1;
					printf("press: %c\n", e.key.keysym.sym);
				}
				break;
			case SDL_KEYUP:
				if (strchr("qweasdzxcrfvtgbo", e.key.keysym.sym)) {
					keystate[KEYMAP[e.key.keysym.sym - 'a']] = 0;
					printf("press: %c\n", e.key.keysym.sym);
				}
				break;
			}
		}

		uint16_t inst = (ram[cpu.pc] << 8) + ram[cpu.pc+1]; // big endian
		uint8_t w = W(inst);
		uint8_t VX = X(inst);
		uint8_t VY = Y(inst);
		uint8_t z = Z(inst);
		uint8_t nn = NN(inst);
		uint16_t nnn = NNN(inst);

		if (inst == 0x00E0) {
			memset(pixels, 0, (32+16) * (64+8));
		}

		else if (inst == 0x00EE) {
			cpu.pc = *(uint16_t*)(ram + cpu.sp) - 2;
			cpu.sp += 2;
		}

		else switch (w) {
		case 0:
			cpu.sp -= 2;
			*(uint16_t*)(ram + cpu.sp) = cpu.pc + 2;
			cpu.pc = nnn - 2;
			break;

		case 1:
			cpu.pc = nnn - 2;
			break;

		case 2:
			cpu.sp -= 2;
			*(uint16_t*)(ram + cpu.sp) = cpu.pc + 2;
			cpu.pc = nnn - 2;
			break;

		case 3:
			if (reg[VX] == nn)
				cpu.pc += 2;
			break;

		case 4:
			if (reg[VX] != nn)
				cpu.pc += 2;
			break;

		case 5:
			if (reg[VX] == reg[VY])
				cpu.pc += 2;
			break;

		case 6:
			reg[VX] = nn;
			break;

		case 7:
			reg[VX] += nn;
			break;

		case 8:
			switch (z) {
			case 0:
				reg[VX] = reg[VY];
				break;
			case 1:
				reg[VX] |= reg[VY];
				break;
			case 2:
				reg[VX] &= reg[VY];
				break;
			case 3:
				reg[VX] ^= reg[VY];
				break;
			case 4:
				uint8_t sum = reg[VX] + reg[VY];
				reg[VF] = sum < reg[VX] || sum < reg[VY];
				reg[VX] = sum;
				break;
			case 5:
				reg[VF] = reg[VX] >= reg[VY];
				reg[VX] = reg[VX] - reg[VY];
				break;
			case 6:
				reg[VX] = reg[VY] >> 1;
				reg[VF] = reg[VY] & 0b00000001;
				break;
			case 7:
				reg[VF] = reg[VY] >= reg[VX];
				reg[VX] = reg[VY] - reg[VX];
				break;
			case 0xE:
				reg[VX] = reg[VY] << 1;
				reg[VF] = reg[VY] & 0b10000000;
				break;
			} break;

		case 9:
			if (reg[VX] != reg[VY])
				cpu.pc += 2;
			break;

		case 0xA:
			VI = nnn;
			break;

		case 0xB:
			cpu.pc = nnn + reg[V0] - 2;
			break;

		case 0xC:
			srand(time(NULL));
			reg[VX] = (rand() % 0xFF) & nn;
			break;

		case 0xD:
			uint64_t end_sprite = SDL_GetPerformanceCounter();
			uint64_t elapsed_sprite_ms = 1000 * (end_sprite - start_sprite) * 1.0/SDL_GetPerformanceFrequency();
			if (ms_per_sprite > elapsed_sprite_ms)
				SDL_Delay(ms_per_sprite - elapsed_sprite_ms);
			start_sprite = SDL_GetPerformanceCounter();

			reg[VX] %= 64;
			reg[VY] %= 32;
			reg[VF] = 0x00;
			for (int i = 0; i < z; i++) {
				for (int j = 0; j < 8; j++) {
					if (1 & (ram[VI + i] >> (7 - j))) {
						if (pixels[reg[VY] + i][reg[VX] + j]) {
							reg[VF] = 0x01;
							pixels[reg[VY] + i][reg[VX] + j] = 0x00;
						} else {
							pixels[reg[VY] + i][reg[VX] + j] = 0xFF;
						}
					}
				}
			}
			break;

		case 0xE:
			switch (nn) {
			case 0x9E:
				if (keystate[reg[VX]])
					cpu.pc += 2;
				break;
			case 0xA1:
				if (!keystate[reg[VX]])
					cpu.pc += 2;
				break;
			}
			break;

		case 0xF:
			switch (nn) {
			case 0x07:
				reg[VX] = cpu.delay;
				break;

			case 0x0A:
				// TODO
				int f = 1;
				for (int i = 0; i < 16; i++) {
					if (keystate[i]) {
						reg[VX] = i;
						f = 0;
						break;
					}
				}
				if (!f) break;

				for (; f && SDL_PollEvent(&e); ) {
					switch (e.type) {
					case SDL_QUIT:
						quit = true;
						break;
					case SDL_KEYDOWN:
						if (strchr("qweasdzxcrfvtgbo", e.key.keysym.sym)) {
							int key = KEYMAP[e.key.keysym.sym - 'a'];
							keystate[key] = 1;
							reg[VX] = key;
							f = 0;
							printf("press: %c\n", e.key.keysym.sym);
						}
						break;
					case SDL_KEYUP:
						if (strchr("qweasdzxcrfvtgbo", e.key.keysym.sym)) {
							keystate[KEYMAP[e.key.keysym.sym - 'a']] = 0;
							printf("press: %c\n", e.key.keysym.sym);
						}
						break;
					}
				}
				break;

			case 0x15:
				cpu.delay = reg[VX];
				break;

			case 0x18:
				// cpu.sound = reg[VX];
				break;

			case 0x1E:
				VI += reg[VX];
				break;

			case 0x29:
				VI = reg[VX] * 5;
				break;

			case 0x33:
				ram[VI] = reg[VX] / 100;
				ram[VI+1] = (reg[VX] / 10) % 10;
				ram[VI+2] = (reg[VX] % 100) % 10;
				break;

			case 0x55:
				for (int i = V0; i <= VX; i++)
					ram[VI + i] = reg[i];
				VI += VX + 1;
				break;

			case 0x65:
				for (int i = V0; i <= VX; i++)
					reg[i] = ram[VI + i];
				VI += VX + 1;
				break;
			}
			break;

		default:
			puts("INVALID INSTRUCTION");
			return 1;
			break;
		}

		// print_regs();
		// printf(
		// 	"V%X = %d\n"
		// 	"V%X = %d\n"
		// 	"VI = 0x%X\n"
		// 	"z  = %d\n"
		// 	"\n",
		// 	VX, reg[VX], VY, reg[VY], VI, z
		// );

		cpu.pc += 2;
		// if (getchar() == EOF)
		// 	quit = true;
	}
}
