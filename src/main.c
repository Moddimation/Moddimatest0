#include <3ds.h>
#include <stdio.h>

#include "render.h"

static bool running = true;

static void loop();
static void init();
static void exit();

int main() {
	aptSetHomeAllowed(false);

	osSetSpeedupEnable(true);
	gfxInitDefault();
	gfxSet3D(true);

	// Initialize game
	init();

	// Run game
	loop();

	// Destroy game
	exit();

	gfxExit();

	return (0);
}

static void init() {
	Render_Init();

	aptSetHomeAllowed(true);
}

static void exit() {
}

static void loop() {
	while (aptMainLoop() && running) {
		hidScanInput();

		u32 down = hidKeysDown();

		if (down & KEY_START)
			break;
	}
}