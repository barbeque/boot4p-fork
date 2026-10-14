//
// boot4p - RS-232 boot loader for TRS-80 Model 4P
//
// Model 4P ROM serial boot starts with machine in 64 x 16 screen
// with memory map 0.  And I assume 64K is the minimum.
//
// Working on some code some code to boot Model 4 Student Station.
// Will need somewhat of a proper null modem cable with at least
// DTR <-> DSR connected (plus the usual transmit/receive/ground).
// SYSTEM /12363 on TRS-80 then run with '-s'.
// (XXX - or maybe the other way around, time will tell)
// Not yet functional.

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#elif __APPLE__
#define Sleep sleep
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "boot2.h"

// load.cpp interface
extern int load(const char *fname, unsigned char *loadmap, unsigned char *runmap);
extern int get_loaded_byte(void);
extern int get_entry(void);

// listcom.cpp interface
extern int listcom(bool print);

int legal_baud[] = { 300, 1200, 2400, 4800, 9600, 19200 };

void usage(void)
{
	//printf("usage: boot4p [-c port#] [-b baudrate] [-s] file.cmd (or .hex or .cas)\n");
	printf("usage: boot4p [-c port#] [-b baudrate] file.cmd (or .hex or .cas)\n");
	printf("baud rate default 19200\n");
	printf("possible baud rates:");
	for (int i = 0; i < sizeof legal_baud / sizeof legal_baud[0]; i++)
		printf(" %d", legal_baud[i]);
	printf("\n");
	int defcom = listcom(true);
	if (defcom > 0)
		printf("COM port defaults to %d\n", defcom);
	exit(1);
}

HANDLE com_open(int port, int baud, int parity);
int com_getc(HANDLE hCom);
void com_putc(HANDLE hCom, char ch);
void com_puts(HANDLE hCom, unsigned char *buf, int length);

int main(int argc, char *argv[])
{
	int baud = 19200;
	int port = 0;
	int student_station = 0;

	while (argc > 1 && argv[1][0] == '-') {
		if (strcmp(argv[1], "-c") == 0) {
			argc--;
			argv++;
			if (argc < 2)
				usage();

			port = atoi(argv[1]);
		}
		else if (strcmp(argv[1], "-b") == 0) {
			argc--;
			argv++;
			if (argc < 2)
				usage();

			baud = atoi(argv[1]);
			bool found = false;
			for (int i = 0; i < sizeof legal_baud / sizeof legal_baud[0]; i++)
				if (baud == legal_baud[i])
					found = true;

			if (!found) {
				printf("illegal baud rate %d\n", baud);
				usage();
			}
		}
		else if (strcmp(argv[1], "-s") == 0) {
			student_station = 1;
		}
		else {
			printf("unknown option '%s'\n", argv[1]);
			usage();
		}
		argc--;
		argv++;
	}

	if (port == 0)
		port = listcom(false);

	if (student_station && baud != 9600) {
		printf("Changing to 9600 baud as required by Student Station.\n");
		baud = 9600;
	}

	// I could support multiple files but gets a bit complicated because
	// of patching the boot loader.
	if (argc != 2)
		usage();

	unsigned char loadmap, runmap;
	int loader_addr = load(argv[1], &loadmap, &runmap);
	if (loader_addr < 0)
		return 2;

	boot2[4] = loadmap;
	boot2[5] = runmap;
	boot2[6] = loader_addr >> 8;

	HANDLE hCom = com_open(port, baud, student_station ? 'n' : 'o');
	if (hCom == INVALID_HANDLE_VALUE)
		return 3;

	int use_boot2 = 1;
	int ch;
	if (!student_station) {
		while ((ch = com_getc(hCom)) < 0) {
			com_putc(hCom, 'U');
			printf(".");
			Sleep(100);
		}
		printf("\n%c", ch);
		while ((ch = com_getc(hCom)) >= 0)
			printf("%c", ch);

		com_putc(hCom, 0xff);
		while ((ch = com_getc(hCom)) < 0)
			;
		if (ch == 'E') {
			printf("\nERROR: %c", ch);
			while ((ch = com_getc(hCom)) >= 0)
				printf("%c", ch);
			exit(3);
		}

		if (ch == 'l')
			use_boot2 = 0;

		printf("\n%c", ch);
		while ((ch = com_getc(hCom)) >= 0)
			printf("%c", ch);
		printf("\n");
		if (!use_boot2)
			printf("Downloader requests no secondary downloader.  boot2 skipped.\n");
		
	}
	else {
		// Generally it seems the TRS-80 will see bit DSR=0 by default.
		// So just start by waiting for boot request.

		// But this isn't working at all.  Never sees byte byte 1.
		// Better get clever and make my own copy of the Student Station
		// boot but with state showing so I can see what is going wrong.

		// This is now working with the "kbhit()" hack to get things going.
		// I think a simpler loops that feeds non-zero bytes and
		// toggle DTR occassionally until we see the non-zero byte get
		// echoed back will do.

		EscapeCommFunction(hCom, SETDTR);
		Sleep(100);
		EscapeCommFunction(hCom, CLRDTR);
		Sleep(100);

		for (char seq = 0; ; seq++) {
			int to = 0;
			for (;;) {
				ch = com_getc(hCom);
				if (ch < 0) {
					Sleep(10);
					to++;
					if (to == 5) {
						to = 0;
						printf(".");
						com_putc(hCom, 9);
					}
				}
				else {
					printf("<%02x>", ch);
					if (ch == 0x0f || ch == 9)
						break;
				}
				if (_kbhit()) {
					_getch();
					printf("!");
					EscapeCommFunction(hCom, SETDTR);
					Sleep(100);
					EscapeCommFunction(hCom, CLRDTR);
					Sleep(100);
				}
			}

			if (ch == 9)
				break;

			if (ch == 0xf) {
				// Probably already have a '9' in the pipe.
				//com_putc(hCom, ~seq); // ack command (value ignored)

				com_putc(hCom, seq); // send any old byte
				while ((ch = com_getc(hCom)) < 0)
					Sleep(1);

				if (ch == seq)
					break;
			}

			printf(".");
		}
		printf("\n");

		com_putc(hCom, 0);

		// Hmmm, we can't use "boot2".  It is in CMD format and all
		// we can do is load 256 bytes to $4300.
		// Test program for now.  Should return from SYSTEM.
		unsigned char prog[256] = {
			0x21,0x00,0x3c,
			0x11,0x01,0x3c,
			0x01,0xff,0x3f,
			0x36,191,
			0xed,0xb0,
			0xc9
		};
		com_puts(hCom, prog, 256);
		printf("And that's all for me!\n");
		CloseHandle(hCom);
		return 0;
	}

	if (use_boot2) {
		com_puts(hCom, boot2, sizeof boot2);

		// Give boot2 a little bit of time to relocate
		// (ugh, wait for an ack character)
		Sleep(250);
	}

Sleep(250);//?

	while ((ch = get_loaded_byte()) >= 0)
		com_putc(hCom, ch);

	CloseHandle(hCom);

	return 0;
}

HANDLE com_open(int port, int baud, int parity)
{
	char com_param[128];
	sprintf(com_param, "%d,%c,8,1", baud, parity);
	DCB dcb;
	memset(&dcb, 0, sizeof(dcb));
	dcb.DCBlength = sizeof(dcb);
	if (!BuildCommDCB(com_param, &dcb)) {
		printf("boot4p: could not create a DCB\n");
		return INVALID_HANDLE_VALUE;
	}

	char com_port[32];
	sprintf(com_port, "\\\\.\\COM%d", port); // so we can work with COM ports > 9

	HANDLE hCom = CreateFile(com_port, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
	if (hCom == INVALID_HANDLE_VALUE) {
		printf("boot4p: could not open %s: err 0x%x\n", com_port, GetLastError());
		return INVALID_HANDLE_VALUE;
	}

	if (!SetCommState(hCom, &dcb)) {
		printf("boot4p: could not set baud rate, etc.\n");
		CloseHandle(hCom);
		return INVALID_HANDLE_VALUE;
	}

	COMMTIMEOUTS timeouts;
	timeouts.ReadIntervalTimeout = 1;
	timeouts.ReadTotalTimeoutMultiplier = 1;
	timeouts.ReadTotalTimeoutConstant = 1;
	timeouts.WriteTotalTimeoutMultiplier = 1;
	timeouts.WriteTotalTimeoutConstant = 1;
	if (!SetCommTimeouts(hCom, &timeouts)) {
		printf("boot4p: could not set timeouts.\n");
		CloseHandle(hCom);
		return INVALID_HANDLE_VALUE;
	}

	return hCom;
}

int com_getc(HANDLE hCom)
{
	unsigned char buf[1];
	DWORD n;

	if (!ReadFile(hCom, buf, sizeof(buf), &n, NULL)) {
		printf("boot4p: error reading from COM port\n");
		exit(1);
	}

	return n ? buf[0] : -1;
}

void com_putc(HANDLE hCom, char ch)
{
	DWORD n = 1;
	if (!WriteFile(hCom, &ch, 1, &n, NULL)) {
		printf("boot4p: error writing to COM port\n");
		exit(2);
	}
}

void com_puts(HANDLE hCom, unsigned char *buf, int length)
{
	DWORD n = length;
	if (!WriteFile(hCom, buf, length, &n, NULL)) {
		printf("boot4p: error writing to COM port\n");
		exit(2);
	}
}
