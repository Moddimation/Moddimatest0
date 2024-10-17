#include "3ds.h"
#include "stdio.h"

int main(int argc, char *argv[]) {
	gfxInitDefault();

	consoleInit(GFX_TOP, NULL);

	puts("Hello World!");

	puts("\nPress Start to exit");

	while (aptMainLoop()) {
		hidScanInput();

		u32 down = hidKeysDown();

		if (down & KEY_START)
			break;
	}

	gfxExit();

	return (0);
}