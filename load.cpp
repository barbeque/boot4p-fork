//
// load.cpp - utility wrapper for C++ file loading class
//

#include <stdio.h>
#include "loader.h"

static TRS80Loader loader;
static int bytepos;

int load(const char *fname, unsigned char *loadmap, unsigned char *runmap)
{
	loader.LoadFile(fname, 0);
	char *err = loader.GetErrorMessage();
	if (err) {
		fprintf(stderr, "boot4p: could not load '%s': %s\n", fname, err);
		return -1;
	}

	int low = loader.GetLowAddress();
	int high = loader.GetHighAddress();
	printf("'%s' loads over range $%04x to $%04x\n", fname, low, high);

	// If we load at 0 or we start loading at $3C00 or above, assume memory
	// map 0 (Model III compat).  Otherwise, we'll have to be in at least
	// memory map 1, but we'll do 2 so that there's keyboard/display access.
	int loader_addr = -1;
	if (low == 0 || low >= 0x3c00) {
		// Load with map 0 when possible for Model III programs that write
		// to the screen.
		*loadmap = low < 0x3c00 ? 2 : 0;
		*runmap = 0;
		loader_addr = *loadmap == 0 ? 0xff00 : 0xf300;
	}
	else {
		*loadmap = 2;
		*runmap = 2;
		if (low >= 0x100)
			loader_addr = 0;
		else
			loader_addr = 0xf300;
	}

	if (loader_addr >= low && loader_addr <= high) {
		printf("Sorry, can't figure out where to put the loader.\n");
		return -1;
	}

	printf("Boot2 relocate $%04x using map %d; program will run with map %d\n",
		loader_addr, *loadmap, *runmap);

	loader.Encode("xxx", TRS80Loader::cmd, TRS80Loader::justBytes);//, TRS80Loader::enc_data);
	loader.SetMediaBitPos(0);
	bytepos = 0;

	return loader_addr;
}

int get_loaded_byte(void)
{
	if (bytepos * 8 + 7 < loader.GetMediaSizeInBits())
		return loader.GetMediaByte(bytepos++) & 255;

	return -1;
}

int get_entry(void)
{
	return loader.GetEntry();
}
