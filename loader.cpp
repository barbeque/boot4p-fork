//
// loader.cpp -- load most TRS-80 file formats.
//
// TODO: Return "\" escaping to ASCII file loading.  And implement a way to
//	translate BASIC files.
//
// My primary purpose is to bring all the file format interpretation and
// parsing routines into one spot.  And to add high-speed cassette file
// loading.
//
// Then we go to the trouble of getting trs80gp, zidis and the odd utilities
// to use them.
//
// The basic design principle is that we have more than enough memory to
// load the file several times over.  No need for streaming.
//
// I intend to expose the layers of the data.  First the entire file is
// loaded as a single data chunk.  Cassette files may then see the data
// transformed into a byte stream and broken into several chunks.  Then
// those chunks are broken down into more chunks of data corresponding
// to individual load records, execute addresses and file names.  Finally,
// the last layer will copy data into a memory space.
//
// Errors in the data and in our processing will be recorded as special
// data chunks.
//
// I'm hoping to preserve information between layers so that a higher layer
// can identify the data in the previous layer that generated it.  This
// is angling towards some kind of visual environment where a user may
// interactively apply data processing steps to decypher unknown or
// damaged data.
//
// Scripting may be a nice way to extend what data formats are supported.
// Especially those programs with custom tape loaders.  But they're very
// little harm in building those in for now.
//
// Wacky (but useful) directions:
//
//		Load disk images.
//		Load .wav cassette dumps
//		Disassembly
//		Generation (say zmac creating a list of load records that can be
//			output in several formats)
//
//
// Level II ROM Reference Manual.pdf has some handy documentation of
// tape formats.
//

#include <stdio.h>
#include <stdlib.h>	// for the old BASIC tokenization code.
#include <ctype.h>	// for the old BASIC tokenization code.
#include <string.h>
#include <assert.h>

#include "loader.h"

#ifdef _MSC_VER
#define stricmp _stricmp
#define strnicmp _strnicmp
#else
#define stricmp strcasecmp
#define strnicmp strncasecmp
#endif

Block::Block()
{
	m_payload = 0;
	m_bitSize = 0;
	m_type = valid;	// yeah, kinda
	m_source = 0;
}

Block::Block(const char *filename)
{
	m_payload = 0;
	m_bitSize = 0;
	m_type = valid;	// yeah, kinda
	m_source = 0;

	LoadFile(filename);
}

void Block::LoadFile(const char *filename, const char *data, int length)
{
	delete m_source;
	m_source = new Source(filename);
	delete [] m_payload;
	m_payload = 0;

	if (data)
	{
		m_payload = new unsigned char [length];
		memcpy(m_payload, data, length);
		m_type = valid;
		m_bitSize = length * 8;
	}
	else
	{
		FILE *fp = fopen(filename, "rb");
		if (!fp)
		{
			m_type = fileError;
			m_source->GetLastError();
		}
		else
		{
			fseek(fp, 0, SEEK_END);
			int size = ftell(fp);
			fseek(fp, 0, SEEK_SET);
			m_payload = new unsigned char[size];
			if (fread(m_payload, size, 1, fp) != 1)
			{
				m_type = fileError;
				m_source->GetLastError();
			}
			fclose(fp);

			m_type = valid;

			m_bitSize = size * 8;
		}
	}
}

Block::Block(const Block *base, int firstBitAddress, int lastBitAddress)
{
	delete m_source;
	m_source = 0;	// should point to the original block

	m_bitSize = (lastBitAddress - firstBitAddress) + 1;
	m_payload = new unsigned char[(m_bitSize + 7) / 8];
	m_type = valid;

	for (int i = 0; i < m_bitSize / 8; i++)
	{
		m_payload[i] = base->GetByte(i);
	}
	int rembits = m_bitSize % 8;
	if (rembits)
	{
		int bits = base->GetBits(m_bitSize - rembits, rembits);
		m_payload[m_bitSize / 8 + 1] = bits << (8 - rembits);
	}
}

Block::~Block()
{
	delete [] m_payload;
}

int Block::GetSize() const
{
	assert(m_type == valid);

	return m_bitSize;
}

void Block::SetSize(int bitSize)
{
	assert(bitSize >= 0);

	delete [] m_payload;
	m_payload = 0;
	if (bitSize > 0)
	{
		int byteSize = (bitSize + 7) / 8;
		m_payload = new unsigned char[byteSize];
		memset(m_payload, 0, byteSize);
	}
	m_bitSize = bitSize;
}

int Block::AvailableBits(int bitAddress) const
{
	if (bitAddress < 0 || bitAddress >= m_bitSize)
	{
		return 0;
	}
	else
	{
		return m_bitSize - bitAddress;
	}
}

int Block::AvailableBytes(int byteAddress) const
{
	return AvailableBits(byteAddress * 8) / 8;
}

int Block::GetBits(int bitAddress, int length) const
{
	assert(length <= 9);	// should really allow 32 or at least 16
	assert(bitAddress >= 0);
	assert(bitAddress < m_bitSize);
	assert(bitAddress + length >= 0);
	assert(bitAddress + length <= m_bitSize);

	const int bitMask = (1 << length) - 1;

	int byteAddress = bitAddress / 8;
	int bitPos = bitAddress % 8;

	// Workaround for ASAN error (prevent reading past end of valid data)
	if (bitPos == 0 && length == 8)  {
		return m_payload[byteAddress];
	}
	int bitChunk = (m_payload[byteAddress] << 8) | m_payload[byteAddress + 1];
	return (bitChunk >> (16 - length - bitPos)) & bitMask;
}

int Block::GetByte(int byteAddress) const
{
	assert(byteAddress >= 0);
	assert(byteAddress < (m_bitSize - 8) / 8);

	return m_payload[byteAddress];
}

void Block::PutBits(int bitAddress, int length, int bits)
{
	assert(length <= 9);	// should really allow 32 or at least 16
	assert(bitAddress >= 0);
	assert(bitAddress < m_bitSize);
	assert(bitAddress + length >= 0);
	assert(bitAddress + length <= m_bitSize);

	const int bitMask = (1 << length) - 1;

	int byteAddress = bitAddress / 8;
	int bitPos = bitAddress % 8;

	bits &= bitMask;
	int bitChunk = (m_payload[byteAddress] << 8) | m_payload[byteAddress + 1];

	bitChunk &= ~(bitMask << (16 - length - bitPos));
	bitChunk |= bits << (16 - length - bitPos);

	m_payload[byteAddress] = bitChunk >> 8;
	m_payload[byteAddress + 1] = bitChunk;
}

void Block::PutByte(int byteAddress, int byte)
{
	assert(byteAddress >= 0);
	assert(byteAddress < (m_bitSize - 8) / 8);

	m_payload[byteAddress] = byte;
}

unsigned char *Block::GetPayload()
{
	return m_payload;
}

Source::Source(const char *name)
{
	int bytesNeeded = strlen(name) + 1;
	m_name = new char[bytesNeeded];
	strcpy(m_name, name);
	m_errorMessage = 0;
	m_type = file;
}

Source::~Source()
{
	delete [] m_name;
	delete [] m_errorMessage;
}

TRS80Record::TRS80Record(Action action, int addr, int length, bool artificial)
{
	m_action = action;
	m_addr = addr;
	m_size = length;
	m_data = new unsigned char[length];
	m_next = 0;
	m_artificial = artificial;
}

TRS80Record::TRS80Record(const char *warningMessage)
{
	m_action = warning;
	m_addr = 0;
	m_size = strlen(warningMessage) + 1;
	m_data = new unsigned char[m_size];
	m_next = 0;
	m_artificial = true;

	strcpy((char *)m_data, warningMessage);
}

TRS80Record::~TRS80Record()
{
	delete [] m_data;
}

// loaded[] is non-zero if the corresponding memory location is loaded.
// And 2 explicitly if the load was artificial.

void TRS80Record::LoadAll(unsigned char *mem, int *pc, int *hl, unsigned char *loaded)
{
	for (TRS80Record *rec = this; rec; rec = rec->m_next)
	{
		if (rec->LoadsMemory())
		{
			for (int i = 0; i < rec->m_size; i++)
			{
				int addr = (rec->m_addr + i) & 0xffff;
				if (mem)
				{
					mem[addr] = rec->m_data[i];
				}
				if (loaded)
				{
					loaded[addr] = 1 + rec->m_artificial;
				}
			}
		}
		else if (rec->m_action == execute)
		{
			if (pc)
			{
				*pc = rec->m_addr;
			}
		}
		else if (rec->m_action == loadHL)
		{
			if (hl)
			{
				*hl = rec->m_addr;
			}
		}
	}
}

void TRS80Record::LoadRange(unsigned char *mem, int low, int high, unsigned char* loaded)
{
	for (TRS80Record *rec = this; rec; rec = rec->m_next)
	{
		if (rec->LoadsMemory())
		{
			for (int i = 0; i < rec->m_size; i++)
			{
				int addr = (rec->m_addr + i) & 0xffff;
				if (addr >= low && addr <= high)
				{
					if (mem)
					{
						mem[addr - low] = rec->m_data[i];
					}
					if (loaded)
					{
						loaded[addr - low]++;
						if (loaded[addr - low] == 0)
							loaded[addr - low]--;
					}
				}
			}
		}
	}
}

void TRS80Record::SetWord(int byteOffset, int value)
{
	assert(byteOffset >= 0);
	assert(byteOffset < m_size - 1);

	m_data[byteOffset] = value & 255;
	m_data[byteOffset + 1] = value >> 8;
}

void TRS80Record::Dump()
{
	int i;

	if (m_artificial)
		printf("Synthesised ");

	switch (m_action)
	{
	case TRS80Record::leader:
		printf("Cassette leader\n");
		break;
	case TRS80Record::name:
		printf("Name: \"");
		for (i = 0; i < m_size; i++)
			printf("%c", m_data[i]);
		printf("\"\n");
		break;
	case TRS80Record::loadmem:
		printf("Load $%04x - $%04x ($%x byte%s)\n",
			m_addr, m_addr + m_size - 1, m_size, m_size == 1 ? "" : "s");
		DumpData();
		break;
	case TRS80Record::originalAddr:
		printf("BASIC program was saved from $%04x\n", m_addr);
		break;
	case TRS80Record::eof:
		printf("EOF\n");
		break;
	case TRS80Record::cmdRecord:
		printf("CMD $%x record\n", m_addr);
		DumpData();
		break;
	case TRS80Record::loadHL:
		printf("HL=$%04x\n", m_addr);
		break;
	case TRS80Record::checksum1:
		printf("Checksum load $%04x - $%04x ($%x byte%s)\n",
			m_addr, m_addr + m_size - 1, m_size, m_size == 1 ? "" : "s");
		DumpData();
		break;
	case TRS80Record::execute:
		printf("entry $%04x\n", m_addr);
		break;
	case TRS80Record::warning:
		printf("Warning: %s\n", m_data);
		break;
	default:
		printf("Unknown record type (%d)\n", m_action);
		DumpData();
		break;
	}
}

void TRS80Record::DumpData()
{
	for (int n = 0; n < m_size; )
	{
		int len = m_size - n;
		if (len > 16)
			len = 16;

		int i;
		for (i = 0; i < 16; i++)
		{
			if (i < len)
				printf("%02x ", m_data[n + i]);
			else
				printf("   ");

			if ((i & 3) == 3)
				printf(" ");
		}

		printf("|");
		for (i = 0; i < len; i++)
		{
			int ch = m_data[n + i];
			printf("%c", ch >= ' ' && ch < 127 ? ch : '.');
		}
		printf("|\n");

		n += len;
	}
}

TRS80Loader::TRS80Loader()
{
	m_file = 0;
	m_errorMessage = 0;
	m_errorLine = -1;

	m_ignoreChecksumError = false;
	m_basicEntryAddress = -1;
}

TRS80Loader::~TRS80Loader()
{
	DisposeFile();
}

char *TRS80Loader::GetErrorMessage()
{
	const char *msg = 0;
	// Could use some accessor help from Block and Source, but whatever.
	if (m_errorMessage)
	{
		msg = m_errorMessage;
	}
	else if (m_data.m_source && m_data.m_source->m_type == Source::error)
	{
		msg = m_data.m_source->m_errorMessage;
	}

	if (msg)
	{
		int bytesNeeded = strlen(msg) + 1;
		if (m_errorLine >= 0)
			bytesNeeded += 16;

		char *err = new char[bytesNeeded];
		if (m_errorLine >= 0)
			snprintf(err, bytesNeeded, msg, m_errorLine);
		else
			strcpy(err, msg);

		return err;
	}

	return 0;
}

void TRS80Loader::DisposeFile()
{
	TRS80Record *next;

	for (TRS80Record *t = m_file; t; t = next)
	{
		next = t->m_next;
		delete t;
	}

	m_file = 0;
}

bool TRS80Loader::SetsPC() const
{
	return GetEntry() >= 0;
}

int TRS80Loader::GetEntry() const
{
	for (TRS80Record *rec = m_file; rec; rec = rec->m_next)
	{
		if (rec->m_action == TRS80Record::execute)
		{
			return rec->m_addr;
		}
	}

	return -1;
}

void TRS80Loader::SetEntry(int entry)
{
	bool set = false;

	for (TRS80Record *rec = m_file; rec; rec = rec->m_next)
	{
		if (rec->m_action == TRS80Record::execute)
		{
			rec->m_addr = entry;
			set = true;
		}
	}

	if (!set)
	{
		AddRecord(new TRS80Record(TRS80Record::execute, entry, 0));
	}
}

void TRS80Loader::DeleteRecords(TRS80Record::Action type)
{
	TRS80Record **prec = &m_file;
	while (*prec)
	{
		if ((*prec)->m_action == type)
		{
			TRS80Record *next = (*prec)->m_next;
			delete *prec;
			*prec = next;
		}
		else
		{
			prec = &(*prec)->m_next;
		}
	}
}

void TRS80Loader::AddRecord(TRS80Record *rec)
{
	TRS80Record **prec = &m_file;
	while (*prec)
	{
		prec = &(*prec)->m_next;
	}

	*prec = rec;
}

void TRS80Loader::DumpRecords(bool dataToo)
{
	const char *media = "unknown";

	switch (m_curMedia)
	{
	case justBytes:
		media = 0;
		break;
	case cassette250:
		media = "250 Baud cassette";
		break;
	case cassette500:
		media = "500 Baud cassette";
		break;
	case cassette1500:
		media = "1500 Baud cassette";
		break;
	default:
		break;
	}

	const char *format = "unknown";
	switch (m_detectedFormat)
	{
	case system:
		format = "SYSTEM";
		break;
	case cmd:
		format = "TRS-DOS /CMD";
		break;
	case hex:
		format = "Intel Hex records";
		break;
	case rom:
		format = "raw ROM";
		break;
	case basic:
		format = "Level II BASIC";
		break;
	case basic1:
		format = "Level I BASIC";
		break;
	case level1:
		format = "Level I";
		break;
	default:
		break;
	}

	printf("%s", format);
	if (media)
	{
		printf(" on %s", media);
	}

	if (!dataToo)
	{
		// Find and show name
		for (TRS80Record *rec = m_file; rec; rec = rec->m_next)
		{
			if (rec->m_action == TRS80Record::name)
			{
				printf(" Name: \"");
				for (int i = 0; i < rec->m_size; i++)
					printf("%c", rec->m_data[i]);
				printf("\"");
			}
		}

		int low = GetLowAddress();
		int high = GetHighAddress();
		if (low >= 0)
			printf(" loads $%04x .. $%04x", low, high);

		int entry = GetEntry();
		if (entry >= 0)
			printf(" entry point $%04x", entry);
	}

	printf("\n");

	if (dataToo)
	{
		for (TRS80Record *rec = m_file; rec; rec = rec->m_next)
		{
			rec->Dump();
		}
	}
}

// .wav output format is modelled on what "Play Cas" does.  The actual
// output for Model I formats is quite different but I'll assume Knut
// had some good reasons for being different.
// Output is always mono, 8-bit, and 22050 or 44100 samples/second.
// 250 baud has 84 samples/bit (about 262.5 baud) which matches emulator timing.
// 500 baud has 44 samples/bit - very close to 500 baud.
// 1500 baud has 16 samples for 0, 8 samples for 1.
// 1000 baud is just 500 baud at double speed.

// Run-length encoded pulses just avoid tedium of writing them literally.

static const unsigned char zero250[] = { 3, 0xff, 3, 0, 84 - 6, 0x80, 0 };
static const unsigned char one250[] = { 3, 0xff, 3, 0, 36, 0x80, 3, 0xff, 3, 0, 36, 0x80, 0 };

static const unsigned char zero500[] = { 3, 0xff, 3, 0, 44 - 6, 0x80, 0 };
static const unsigned char one500[] = { 3, 0xff, 3, 0, 16, 0x80, 3, 0xff, 3, 0, 16, 0x80, 0 };

static const unsigned char zero1500[] = { 8, 0, 8, 0xff, 0 };
static const unsigned char one1500[] = { 4, 0, 4, 0xff, 0 };

void TRS80Loader::SaveFile(const char *filename, Physical physical, const char *mode)
{
	m_errorMessage = 0;

	// Gets a little cheesy here borrowing m_data.m_source and all.
	// In some sense "Source" should be more sensible.

	m_data.m_source = new Source(filename);

	FILE *fp = fopen(filename, mode);
	if (!fp)
	{
		m_data.m_source->GetLastError();
		m_errorMessage = m_data.m_source->m_errorMessage;
		return;
	}

	const unsigned char *pulse[2] = { 0, 0 };
	int headPad = 0;
	int hertz = 22050;

	switch (physical)
	{
	default:
	case justBits:
		for (int bitPos = 0; bitPos < m_data.GetSize(); bitPos += 8)
		{
			int bitLen = m_data.GetSize() - bitPos;
			if (bitLen > 8)
			{
				bitLen = 8;
			}

			int byte = m_data.GetBits(bitPos, bitLen) << (8 - bitLen);
			if (!FilePutChar(fp, byte))
			{
				return;
			}
		}
		break;
	case baud250:
		pulse[0] = zero250;
		pulse[1] = one250;
		break;
	case baud500:
		pulse[0] = zero500;
		pulse[1] = one500;
		break;
	case baud1000:
		hertz = 44100;
		pulse[0] = zero500;
		pulse[1] = one500;
		break;
	case baud1500:
		pulse[0] = zero1500;
		pulse[1] = one1500;
		break;
	}

	if (pulse[0])
	{
		int pulseLen[2];

		for (int i = 0; i < 2; i++)
		{
			pulseLen[i] = 0;
			for (int j = 0; pulse[i][j]; j += 2)
			{
				pulseLen[i] += pulse[i][j];
			}
		}

		int headPad = 10, tailPad = hertz / 2;
		int len = headPad;

		for (int bitPos = 0; bitPos < m_data.GetSize(); bitPos++)
		{
			len += pulseLen[m_data.GetBits(bitPos, 1)];
		}

		len += tailPad;

		unsigned char hzlo = hertz & 255;
		unsigned char hzhi = hertz >> 8;

		unsigned char waveHeader[] = {
			'R', 'I', 'F', 'F',
			0, 0, 0, 0,
			'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
			16, 0, 0, 0, // wav information length
			1, 0, // PCM
			1, 0, // Single channel
			hzlo, hzhi, 0, 0, // samples/second
			hzlo, hzhi, 0, 0, // average bytes/second
			1, 0, // block alignment
			8, 0, // bits/sample
			'd', 'a', 't', 'a',
			0, 0, 0, 0
		};
		const int waveHeaderSize = sizeof waveHeader;

		waveHeader[waveHeaderSize - 4] = len;
		waveHeader[waveHeaderSize - 3] = len >> 8;
		waveHeader[waveHeaderSize - 2] = len >> 16;
		waveHeader[waveHeaderSize - 1] = len >> 24;

		waveHeader[4] = (len + 36);
		waveHeader[5] = (len + 36) >> 8;
		waveHeader[6] = (len + 36) >> 16;
		waveHeader[7] = (len + 36) >> 24;

		for (int i = 0; i < waveHeaderSize; i++)
		{
			if (!FilePutChar(fp, waveHeader[i]))
			{
				return;
			}
		}

		for (int i = 0; i < headPad; i++)
		{
			if (!FilePutChar(fp, 0x80))
			{
				return;
			}
		}

		for (int bitPos = 0; bitPos < m_data.GetSize(); bitPos++)
		{
			int bit = m_data.GetBits(bitPos, 1);

			for (int i = 0; pulse[bit][i]; i += 2)
			{
				for (int j = 0; j < pulse[bit][i]; j++)
				{
					if (!FilePutChar(fp, pulse[bit][i + 1]))
					{
						return;
					}
				}
			}
		}

		for (int i = 0; i < tailPad; i++)
		{
			if (!FilePutChar(fp, 0x80))
			{
				return;
			}
		}
	}

	if (fclose(fp) == EOF)
	{
		m_data.m_source->GetLastError();
		m_errorMessage = m_data.m_source->m_errorMessage;
	}
}

bool TRS80Loader::FilePutChar(FILE *fp, int ch)
{
	if (fputc(ch, fp) == EOF)
	{
		m_data.m_source->GetLastError();
		m_errorMessage = m_data.m_source->m_errorMessage;
		fclose(fp);
		return false;
	}
	return true;
}

void TRS80Loader::LoadFile(const char *filename, int basicLoadAddress, const char *data, int length)
{
	LoadFileData(filename, data, length);

	if (!m_errorMessage)
	{
		const char *p = filename ? strrchr(filename, '.') : 0;

		// Skip detection from ROMs
		if (p && stricmp(p, ".rom") == 0)
		{
			m_detectedFormat = rom;
			LoadROM();
		}
		else
		{
			LoadRecords(basicLoadAddress);
		}
	}
}

void TRS80Loader::LoadFileData(const char *filename, const char *data, int length)
{
	m_errorMessage = 0;

	DisposeFile();

	m_data.LoadFile(filename, data, length);

	// Some kind of low-level error?
	if (m_data.m_type != Block::valid)
	{
		m_errorMessage = m_data.m_source->m_errorMessage;
		return;
	}

	m_bitPos = 0;
}

void TRS80Loader::LoadROM()
{
	m_curMedia = justBytes;
	m_bitPos = 0;
	int size = m_data.AvailableBytes(0);
	TRS80Record *load = new TRS80Record(TRS80Record::loadmem, 0, size);
	for (int i = 0; i < size; i++)
	{
		load->m_data[i] = GetMediaByte(i);
	}
	AddRecord(load);
	AddRecord(new TRS80Record(TRS80Record::execute, 0, 0));
}

void TRS80Loader::LoadRecords(int basicLoadAddress)
{
	m_errorMessage = 0;

	// Determine the type of file we have.

	// First look at the media type.
	int leaderLength = DetectMedia();

	if (leaderLength > 0)
	{
		TRS80Record *leader = new TRS80Record(TRS80Record::leader, 0, 0);
		AddRecord(leader);
	}

	// Many messy decisions to make based on media type and subsequent detection
	// of format.

	int byte0 = GetMediaByte(0);

	m_detectedFormat = unknown;

	if (byte0 == 0x55)
	{
		m_detectedFormat = system;
		ParseSystemTape();
	}
	else if (m_curMedia == cassette250)
	{
		m_detectedFormat = level1;
		ParseLevel1Tape();
	}
	else if (byte0 == 0xD3)
	{
		m_detectedFormat = basic;
		ParseTokenizedBasicTape(basicLoadAddress);
	}
	else if (m_curMedia == justBytes && byte0 == ':')
	{
		m_detectedFormat = hex;
		ParseHexFile();
	}
	else if (m_curMedia == justBytes && byte0 <= 0x1f)
	{
		m_detectedFormat = cmd;
		ParseCmdFile();
	}
	else if (m_curMedia == justBytes && byte0 == 0xff)
	{
		m_detectedFormat = basic;
		ParseTokenizedBasic(basicLoadAddress, 1);
	}
	else if (m_curMedia == justBytes && (byte0 >= '0' && byte0 <= '9'))
	{
		ParseBasic(basicLoadAddress);
	}
	else
	{
		m_errorMessage = "Unrecognized format.";
	}
}

void TRS80Loader::LoadMemory(const unsigned char *memory, const unsigned char *loaded, int pc)
{
	DisposeFile();

	int low = 0;
	while (low < 65536)
	{
		if (!loaded[low])
		{
			low++;
			continue;
		}

		unsigned char type = loaded[low];
		int high = low;
		while (high < 65536 && loaded[high] == type)
		{
			high++;
		}
		TRS80Record *load = new TRS80Record(TRS80Record::loadmem, low, high - low, type == 2);
		for (int i = low; i < high; i++)
		{
			load->m_data[i - low] = memory[i];
		}
		AddRecord(load);

		low = high;
	}

	if (pc >= 0)
	{
		TRS80Record *exec = new TRS80Record(TRS80Record::execute, pc, 0);
		AddRecord(exec);
	}

	//printf("Load extents");
	//bool load = 0;
	//for (int i = 0; i < 65536; i++)
	//{
	//	if (load != !!loaded[i])
	//	{
	//		printf(" %04X", i - !loaded[i]);
	//	}
	//	load = !!loaded[i];
	//}
	//printf("\n");
}

void TRS80Loader::SetCheckChecksum(bool check)
{
	m_ignoreChecksumError = !check;
}

void TRS80Loader::SetBasicEntryAddress(int addr)
{
	m_basicEntryAddress = addr;
}

int TRS80Loader::GetLowAddress()
{
	int low = -1;
	for (TRS80Record *rec = m_file; rec; rec = rec->m_next)
	{
		if (rec->LoadsMemory() && rec->m_size > 0)
		{
			if (low == -1 || rec->m_addr < low)
			{
				low = rec->m_addr;
			}
		}
	}

	return low;
}

int TRS80Loader::GetHighAddress()
{
	int high = -1;
	for (TRS80Record *rec = m_file; rec; rec = rec->m_next)
	{
		if (rec->LoadsMemory() && rec->m_size > 0)
		{
			int addr = rec->m_addr + rec->m_size - 1;
			if (high == -1 || addr > high)
			{
				high = addr;
			}
		}
	}

	return high;
}

int TRS80Loader::GetFileName(char *name)
{
	for (TRS80Record *rec = m_file; rec; rec = rec->m_next)
	{
		if (rec->m_action == TRS80Record::name && rec->m_size > 0)
		{
			int len = rec->m_size;
			memcpy(name, rec->m_data, len);
			name[len] = '\0';
			// Trim trailing spaces from SYSTEM files.  Don't know
			// what to do with a name that is all spaces.
			if (m_detectedFormat == system)
			{
				while (len > 0 && name[len - 1] == ' ')
					name[--len] = '\0';
			}
			return len;
		}
	}

	return -1;
}

// Move all addresses and entry points 

void TRS80Loader::Move(int offset)
{
	for (TRS80Record *rec = m_file; rec; rec = rec->m_next)
	{
		rec->m_addr += offset;
	}
}

#define MOVE_CODE_SIZE (3+3+3+2+3) /* LD HL, LD DE, LD BC, LD[DI]R, JP */

static void GenMoveCode(unsigned char *code, int src_addr, int dst_addr, int size, int entry_addr)
{
	int move = 0xB0; // for LDIR
	if (dst_addr > src_addr)
	{
		src_addr += size - 1;
		dst_addr += size - 1;
		move = 0xB8; // for LDDR
	}

	*code++ = 0x21; *code++ = src_addr; *code++ = src_addr >> 8; // LD HL,src_addr
	*code++ = 0x11; *code++ = dst_addr; *code++ = dst_addr >> 8; // LD DE,dst_addr
	*code++ = 0x01; *code++ = size; *code++ = size >> 8; 		 // LD BC,size
	*code++ = 0xED; *code++ = move; // LDIR or LDIR
	*code++ = 0xC3; *code++ = entry_addr; *code++ = entry_addr >> 8; // JP entry
}

// "Relocate" executable to a new base address.  Doesn't actually relocate the
// Z-80 code, just loads at a different address and adds a memory move to
// put the code at the address it normally loads at.
//
// In other words, rather like what LMOFFSET does.  Though LMOFFSET always
// adds the memory move code at the end of the file as a separate record and
// pads it with NOPs to allow easy addtion of your own code.  It even can add
// more code to disable DOS.
//
// Instead, I add the code to the start or end of the executable depending
// on the relative destination of the relocation.  After if the code is
// moved down, before if it is moved up unless it is moved up a very small
// amount in which case I put the relocation slightly after the end.
// Seems like the important part is that the code never loads lower than
// the specified value.

void TRS80Loader::Relocate(int newBase)
{
	int entry = GetEntry();
	int low = GetLowAddress();
	int high = GetHighAddress();

	if (low < 0)
	{
		// Nothing loaded.  We'll still move the entry address.
		if (entry >= 0)
		{
			Move(newBase - entry);
		}
		return;
	}

	// Do nothing if the base is the same.
	if (newBase == low)
		return;

	int off = newBase - low;
	int size = high - low + 1;

	// No entry address?  Then just change the loading.
	if (entry < 0)
	{
		Move(off);
		return;
	}

	// The records are treated sequentially and anything after the entry
	// record is ignored.  There could be other important records, but it
	// sufficies to delete the entry address record and add it back.

	DeleteRecords(TRS80Record::execute);

	if (newBase > low)
	{
		// Moving down in memory; just stick at end.
		Move(off);
		TRS80Record *mv = new TRS80Record(TRS80Record::loadmem,
			newBase + size, MOVE_CODE_SIZE);
		GenMoveCode(mv->m_data, newBase, low, size, entry);
		AddRecord(mv);
		SetEntry(newBase + size);
	}
	else if (low - newBase < MOVE_CODE_SIZE)
	{
		// Moving up in memory just a little bit so no room for movement code
		// at the start.
		Move(off);
		TRS80Record *mv = new TRS80Record(TRS80Record::loadmem,
			high + 1, MOVE_CODE_SIZE);
		GenMoveCode(mv->m_data, newBase, low, size, entry);
		AddRecord(mv);
		SetEntry(high + 1);
	}
	else
	{
		// Moving far up in memory; put movement code at start.
		Move(off + MOVE_CODE_SIZE);
		TRS80Record *mv = new TRS80Record(TRS80Record::loadmem,
			newBase, MOVE_CODE_SIZE);
		GenMoveCode(mv->m_data, newBase + MOVE_CODE_SIZE, low, size, entry);
		AddRecord(mv);
		SetEntry(newBase);
	}
}

bool TRS80Loader::Encode(const char *name, Format format, Media media, int parts)
{
	int low = 65536;
	int size = 0;
	unsigned char mem[65536];

	if (format == rom || media == cassette250)
	{
		memset(mem, 0, sizeof(mem));

		if (parts & enc_data)
		{
			for (TRS80Record *rec = m_file; rec; rec = rec->m_next)
			{
				if (rec->m_action == TRS80Record::loadmem ||
					(rec->m_action == TRS80Record::checksum1 && media != cassette250))
				{
					// If we're translating to Model I BASIC on cassette, skip any generated records.
					if (rec->m_artificial && media == cassette250 && format == basic1)
						continue;

					int top = rec->m_addr + rec->m_size;
					if (top > size)
					{
						size = top;
					}

					for (int i = 0; i < rec->m_size; i++)
					{
						int addr = rec->m_addr + i;
						if (addr < 65536)
						{
							mem[addr] = rec->m_data[i];
						}
					}

					if (rec->m_addr < low)
					{
						low = rec->m_addr;
					}
				}
			}
		}

		if (size > 65536)
		{
			size = 65536;
		}

		if (format == rom)
		{
			m_data.SetSize(size * 8);
			m_bitPos = 0;
			m_curMedia = justBytes;
			for (int i = 0; i < size; i++)
			{
				PutMediaByte(mem[i]);
			}

			return true;
		}
	}

	int pc = GetEntry();

	if (media == cassette250)
	{
		// Do execution address via standard stack overwrite.
		// Unless we're overwriting the stack already.
		if (low >= 0x4200 && pc >= 0)
		{
			low = 0x41fe;
			mem[low] = pc;
			mem[low + 1] = pc >> 8;
		}
	}

	for (int pass = 0; pass < 2; pass++)
	{
		if (pass == 0)
		{
			// First pass we output nothing but compute size as side effect.
			m_data.SetSize(0);
			m_bitPos = 0;
		}
		else
		{
			// Second pass we output the data.
			m_data.SetSize(m_bitPos);
			m_bitPos = 0;
		}

		m_curMedia = media;

		// Output leader as required by media.

		if (parts & enc_header)
		{
			if (media == cassette1500)
			{
				for (int i = 0; i < 255; i++)
				{
					PutRawBits(8, 0x55);
				}
				PutRawBits(8, 0x7f);
			}
			else if (media == cassette250 || media == cassette500)
			{
				for (int i = 0; i < 255; i++)
				{
					PutRawBits(8, 0);
				}
				PutRawBits(8, 0xa5);
			}
		}

		// Output header byte and name as required
		if (format == system)
		{
			if (media == cassette250)
			{
				if (parts & enc_header)
				{
					PutMediaByte(low >> 8);
					PutMediaByte(low);
					PutMediaByte(size >> 8);
					PutMediaByte(size);
				}
			}
			else
			{
				if (parts & enc_header)
					PutMediaByte(0x55);

				// SYSTEM files have to have a name.
				bool nameEnded = false;
				for (int i = 0; i < 6; i++)
				{
					if (!nameEnded)
					{
						nameEnded = name[i] == '\0';
					}
					PutMediaByte(nameEnded ? ' ': name[i]);
				}
			}
		}
		else if (format == cmd && (parts & enc_name))
		{
			PutMediaByte(5);	// module name
			int len = strlen(name);
			if (len > 255)
			{
				len = 255;
			}
			PutMediaByte(len);
			for (int i = 0; i < len; i++)
			{
				PutMediaByte(name[i]);
			}
		}
		else if (format == basic || format == basic1)
		{
			if (media == cassette250)
			{
				if (parts & enc_header)
				{
					PutMediaByte(low >> 8);
					PutMediaByte(low);
					PutMediaByte(size >> 8);
					PutMediaByte(size);
				}
			}
			else if (media == cassette500 || media == cassette1500)
			{
				if (parts & enc_header)
				{
					PutMediaByte(0xD3);
					PutMediaByte(0xD3);
					PutMediaByte(0xD3);
				}
				PutMediaByte(name[0]);
			}
			else
			{
				PutMediaByte(0xFF);
			}
		}

		if (format == text || format == list)
		{
			char eol = format == text ? '\n' : '\r';
			for (TRS80Record *rec = m_file; rec; rec = rec->m_next)
			{
				if (rec->LoadsMemory() && (parts & enc_data) &&
					rec->m_addr >= 0x4100 && rec->m_size >= 2)
				{
					if (m_detectedFormat == basic1)
						ListLevel1(rec->m_size, rec->m_data, eol);
					else
						List(rec->m_size, rec->m_data, eol);
				}
			}
		}
		else if (media == cassette250)
		{
			// TODO - verify this handles BASIC
			if (parts & enc_data)
			{
				int checksum250 = 0;
				for (int i = low; i < size; i++)
				{
					PutMediaByte(mem[i]);
					checksum250 += mem[i];
				}
				PutMediaByte(-checksum250);
			}
		}
		else if (format == basic)
		{
			if (m_detectedFormat != basic)
			{
				m_errorMessage = "basic1 to basic2 no go";
				return false;
			}

			// Can dump the records pretty much straight out.  But
			// we do no error checking.
			for (TRS80Record *rec = m_file; rec; rec = rec->m_next)
			{
				if (rec->LoadsMemory() && (parts & enc_data) &&
					rec->m_addr >= 0x4100 && rec->m_size >= 2)
				{
					// We could change the load address but no need.
					//next = rec->m_data[0] | (rec->m_data[1] << 8);
					for (int i = 0; i < rec->m_size; i++)
					{
						PutMediaByte(rec->m_data[i]);
					}
				}
			}
		}
		else
		{
			const int maxRecordSize = format == hex ? 16 : 256;

			// Output records for loaded data, fragmenting as necessary.
			// By necessity, an execute record will end the output.

			for (TRS80Record *rec = m_file; rec; rec = rec->m_next)
			{
				if (rec->LoadsMemory() && (parts & enc_data))
				{
					for (int off = 0; off < rec->m_size; )
					{
						int addr = rec->m_addr + off;
						int len = rec->m_size - off;
						if (len > maxRecordSize)
						{
							len = maxRecordSize;
						}
						int checksum = addr + (addr >> 8);

						switch (format)
						{
						case system:
							PutMediaByte(0x3c);
							PutMediaByte(len & 255);
							PutMediaWord(addr);
							break;
						case cmd:
							PutMediaByte(1);
							PutMediaByte((len + 2) & 255);
							PutMediaWord(addr);
							break;
						case hex:
							PutRawBits(8, ':');
							PutHexByte(len);
							checksum += len;
							PutHexByte(addr >> 8);
							PutHexByte(addr & 255);
							PutHexByte(0);
							checksum += 0; // checksum all hex digits
							break;
						default:
							break;
						}

						for (int i = 0; i < len; i++)
						{
							int byte = rec->m_data[off + i];
							if (format == hex)
							{
								PutHexByte(byte);
							}
							else
							{
								PutMediaByte(byte);
							}
							checksum += byte;
						}

						switch (format)
						{
						case system:
							PutMediaByte(checksum);
							break;
						case cmd:
							break;
						case hex:
							PutHexByte(-checksum);
							PutRawBits(8, '\n');
							break;
						default:
							break;
						}

						off += len;
					}
				}
				else if (rec->m_action == TRS80Record::execute)
				{
					break;
				}
				// Ignore other records as irrelevant.
			}
		}

		if (parts & enc_entry)
		{
			if (media != cassette250)
			{
				switch (format)
				{
				case system:
					// SYSTEM tapes cannot lack an entry point.  Return errro
					if (pc < 0)
					{
						m_errorMessage = "SYSTEM files require an entry point";
						return false;
					}

					PutMediaByte(0x78);
					PutMediaWord(pc);
					break;
				case cmd:
					if (pc >= 0)
					{
						PutMediaByte(2); // execution address
						PutMediaByte(2); // record length
						PutMediaWord(pc);
					}
					break;
				case hex:
					// This replacement of PC is OK for .hex files as in the
					// larger world they don't really have entry addresses.
					if (pc < 0)
						pc = 0;

					PutRawBits(8, ':');
					PutHexByte(0);
					PutHexByte(pc >> 8);
					PutHexByte(pc & 255);
					PutHexByte(1);
					PutHexByte(-(1 + (pc >> 8) + (pc & 255)));
					PutRawBits(8, '\n');
					break;
				default:
					break;
				}
			}
		}

		// Cassette seems to require a few zeros at the end when I use "Play Cas".
		// So add them because at least they'll be ignored.  Another possible
		// control option.

		if (parts & enc_padding)
		{
			if (media == cassette250 || media == cassette500 || media == cassette1500)
			{
				for (int i = 0; i < 8; i++)
				{
					PutMediaByte(0);
				}
			}
		}
	}

	return true;
}

void NameFromFilename(TRS80Loader::Format format, const char *filename, char *name)
{
	int len;
	if (*filename == '\0')
	{
		*name = '\0';
		len = 0;
	}
	else
	{
		// Find base name.
		const char *basename = strchr(filename, '\0');
		for (; basename >= filename; basename--)
		{
			if (*basename == '/' || *basename == '\\')
			{
				break;
			}
		}
		basename++;

		// Copy up to 6 and/or stop at dot.
		for (len = 0; len < 6; len++)
		{
			int ch = basename[len];
			if (ch == '\0' || ch == '.')
			{
				break;
			}
			if (islower(ch))
			{
				ch = toupper(ch);
			}
			name[len] = ch;
		}
		name[len] = '\0';
	}

	// Padding may be necessary depending on the format.
	if (format == TRS80Loader::system)
	{
		while (len < 6)
		{
			name[len++] = ' ';
		}
		name[len] = '\0';
	}
}

void TRS80Loader::ParseSystemTape()
{
	// Assume we already have seen the 0x55 name header code.
	// BTW, Model III ROM will keep reading bytes until it sees an 0x55.

	int bpos = 1;

	TRS80Record *name = new TRS80Record(TRS80Record::name, 0, 6);
	for (int i = 0; i < 6; i++)
	{
		name->m_data[i] = GetMediaByte(bpos++);
	}
	if (m_errorMessage)
	{
		delete name;
		return;
	}
	AddRecord(name);

	// BTW, Model III ROM keeps grabbing bytes and skips over any non 0x3c or 0x78
	// bytes.  This makes Microchess work because it has a 7 character file name.
	// "CHESS  ".

	// We'll allow for it in a very specific way.
	if (GetMediaByte(bpos) == 0x20)
	{
		bpos++;
	}

	while (GetMediaByte(bpos) == 0x3c)
	{
		bpos++;
		int length = GetMediaByte(bpos++);
		if (m_errorMessage) return;
		if (length == 0)
		{
			length = 256;
		}
		int addr = GetMediaWord(bpos);
		if (m_errorMessage) return;
		int sum  = (addr & 255) + (addr >> 8);
		bpos += 2;
		TRS80Record *load = new TRS80Record(TRS80Record::loadmem, addr, length);

		for (int i = 0; i < length; i++)
		{
			load->m_data[i] = GetMediaByte(bpos++);
			sum += load->m_data[i];
			if (m_errorMessage)
			{
				delete load;
				return;
			}
		}
		int checksum = GetMediaByte(bpos++);
		if (m_errorMessage)
		{
			delete load;
			return;
		}

		if (checksum != (sum & 255))
		{
			if (m_ignoreChecksumError)
			{
				AddRecord(new TRS80Record("Checksum error."));
			}
			else
			{
				delete load;
				m_errorMessage = "Checksum error.";
				return;
			}
		}
		AddRecord(load);
	}

	int execAddr = -1;

	if (GetMediaByte(bpos) == 0x78)
	{
		execAddr = GetMediaWord(bpos + 1);
		if (m_errorMessage) return;
		bpos += 3;
	}

	SeekMediaByte(bpos);

	// However, if any data was loaded into $41E2 (the SYSTEM hook) then
	// we'll assume the start address is there.  Which is a tiny bit
	// dicey as the stack should be set to $4288 and the exec address
	// should be put into $40DF.  At least on the Model III.
	//
	// Now, some clever bunnies put an address into the 401e display address
	// to redirect SYSTEM when it prints the '*?' prompt.  So we'll check
	// for that as an alternate.
	//
	// The emulator should, based on ROM, put this stuff in and do the
	// same steps to simulate a SYSTEM command fully.
	//
	// A flag to turn off this auto-checking would be nice.  But we
	// only trigger it if it really seems intentional.  Otherwise it
	// is just ignored as, in general, loading before the start of
	// BASIC can lead to all sorts of weird crashes.
	//
	// I note $420E as cassette vector address on the Model III.

	unsigned char loaded41e1[2];
	memset(loaded41e1, 0, sizeof(loaded41e1));
	m_file->LoadRange(0, 0x41e1, 0x41e2, loaded41e1);

	unsigned char vector[2];
	unsigned char vector_loaded[2];
	memset(vector_loaded, 0, sizeof(vector_loaded));
	m_file->LoadRange(vector, 0x401e, 0x401f, vector_loaded);

	if (!loaded41e1[0] && loaded41e1[1])
	{
		execAddr = 0x41e2;
	}
	else if (vector_loaded[0] && vector_loaded[1])
	{
		execAddr = (vector[1] << 8) | vector[0];
	}

	if (execAddr >= 0)
	{
		TRS80Record *exec = new TRS80Record(TRS80Record::execute, execAddr, 0);
		AddRecord(exec);
	}
	else
	{
		m_errorMessage = "Cassette system file with bad record type.";
	}
}

void TRS80Loader::ParseLevel1Tape()
{
	// Level I begins with a big endian load address.  Must be $4200 for BASIC.
	// If you want it to execute, machine language must load over $41fe.
	// $4091 is the most reasonable low point, but lower is theoretically possible.
	//
	// But we're not going to care here; just simulate things best we can.

	int startAddress = GetMediaWordBigEndian(0);
	int endAddress = GetMediaWordBigEndian(2);
	if (m_errorMessage) return;
	int length = endAddress - startAddress;

	TRS80Record *load = new TRS80Record(TRS80Record::loadmem, startAddress, length);
	// The checksum is loaded into memory on the real system.  But we store
	// it in a special record.  Super pedantic converters should take heed.
	int sum = 0;
	for (int i = 0; i < length; i++)
	{
		load->m_data[i] = GetMediaByte(4 + i);
		sum += load->m_data[i];
		if (m_errorMessage)
		{
			delete load;
			return;
		}
	}

	SeekMediaByte(4 + length);
	int checksum = GetMediaByte(0);
	if (m_errorMessage)
	{
		delete load;
		return;
	}
	SeekMediaByte(1);

	if (((sum + checksum) & 255) != 0)
	{
		if (m_ignoreChecksumError)
		{
			AddRecord(new TRS80Record("Checksum error."));
		}
		else
		{
			delete load;
			m_errorMessage = "Checksum error.";
			return;
		}
	}

	AddRecord(load);
	TRS80Record *chk = new TRS80Record(TRS80Record::checksum1, endAddress, 1);
	chk->m_data[0] = checksum;
	AddRecord(chk);
	AddRecord(new TRS80Record(TRS80Record::loadHL, endAddress, 0));

	// Level 1 BASIC programs load at $4200 and do not automatically execute.
	// Level 1 machine langauge programs load lower than $4200 and overwrite
	// a stack address so they run automatically.  We check for that possibility
	// and set the PC if so.  We don't set the stack pointer, though nor
	// generally perform all the possible side-effects.
	// Happily, the $eec return address is same for Model 1 and Model 3 level 1 ROM.

	// Excessive loop here to get the PC, but no sense fooling around with a
	// bunch of range checks and the like especially if we wish to catch the
	// wraparound cases.

	int addr = startAddress;
	int newPC = 0xeec;
	for (int j = 0; j < length; j++)
	{
		if (addr == 0x41fe)
		{
			newPC &= ~0xff;
			newPC |= load->m_data[j];
		}
		else if (addr == 0x41ff)
		{
			newPC &= ~0xff00;
			newPC |= load->m_data[j] << 8;
		}

		addr++;
		addr &= 0xffff;
	}

	if (newPC != 0xeec)
	{
		AddRecord(new TRS80Record(TRS80Record::execute, newPC, 0));
	}
	else if (startAddress == 0x4200)
	{
		// Assume it is BASIC though a test parse of it would be sensible.
		m_detectedFormat = basic1;

		// Add the pointer to the end of the program for those wishing to load
		// directly into Level I BASIC (trs80gp does this).
		TRS80Record *ptr = new TRS80Record(TRS80Record::loadmem, 0x406c, 2, true);
		ptr->SetWord(0, addr);
		AddRecord(ptr);
	}
}

void TRS80Loader::ParseTokenizedBasicTape(int basicLoadAddress)
{
	int d0 = GetMediaByte(0);
	int d1 = GetMediaByte(1);
	int d2 = GetMediaByte(2);
	int nameByte = GetMediaByte(3);

	if (m_errorMessage) return;

	if (d0 != 0xD3 || d1 != 0xD3 || d2 != 0xD3)
	{
		m_errorMessage = "Missing BASIC tape header 0xD3 bytes.";
		return;
	}

	TRS80Record *name = new TRS80Record(TRS80Record::name, 0, 1);
	name->m_data[0] = nameByte;
	AddRecord(name);

	ParseTokenizedBasic(basicLoadAddress, 4);
}

void TRS80Loader::ParseTokenizedBasic(int basicLoadAddress, int pos)
{
	for (;;)
	{
		int nextLine = GetMediaWord(pos);
		pos += 2;
		if (m_errorMessage) return;
		AddRecord(new TRS80Record(TRS80Record::originalAddr, nextLine, 0));
		if (nextLine == 0)
		{
			break;
		}
		int lineLength = 2;
		int b = 0;
		do
		{
			b = GetMediaByte(pos + lineLength);
			if (m_errorMessage) return;
			lineLength++;
		} while (b != 0);
		TRS80Record *line = new TRS80Record(TRS80Record::loadmem, basicLoadAddress, lineLength + 2);
		int nextAddr = basicLoadAddress + 2 + lineLength;
		line->SetWord(0, nextAddr);
		for (int i = 0; i < lineLength; i++)
		{
			line->m_data[2 + i] = GetMediaByte(pos++);
		}
		AddRecord(line);

		basicLoadAddress = nextAddr;
	}

	SeekMediaByte(pos);

	AddBasicPointers(basicLoadAddress);
}

void TRS80Loader::AddBasicPointers(int basicEndAddress)
{
	TRS80Record *ptr;

	// Null pointer to terminate BASIC program.
	ptr = new TRS80Record(TRS80Record::loadmem, basicEndAddress, 2, true);
	ptr->SetWord(0, 0);
	AddRecord(ptr);
	basicEndAddress += 2;

	// Now add records to set up BASIC's program pointers.

	// end of BASIC program pointer
	ptr = new TRS80Record(TRS80Record::loadmem, 0x40f9, 2, true);
	ptr->SetWord(0, basicEndAddress);
	AddRecord(ptr);

	// start of array variables pointer
	ptr = new TRS80Record(TRS80Record::loadmem, 0x40fb, 2, true);
	ptr->SetWord(0, basicEndAddress);
	AddRecord(ptr);

	// start of free memory pointer
	ptr = new TRS80Record(TRS80Record::loadmem, 0x40fd, 2);
	ptr->SetWord(0, basicEndAddress);
	AddRecord(ptr);

	// Add entry address if loader has been asked to do so.
	if (m_basicEntryAddress >= 0)
	{
		AddRecord(new TRS80Record(TRS80Record::execute, m_basicEntryAddress, 0, true));
	}
}

static int tokenize(const char *line, unsigned char *tok);

void TRS80Loader::ParseBasic(int basicLoadAddress)
{
	// At some point we may wish to take a guess at the BASIC file format.
	// For Level I vs Level II any appearance of "[A-Z]." would indicate an abbreviation
	// and something not possible in Level II.  Not sure about Model 4 BASIC.
	// Model 4 BASIC has WHILE and such.  But it'll all be guesswork.  And the proliferation
	// of other disk BASICs is a potential problem.

	if (basicLoadAddress == 0x4200)
	{
		m_detectedFormat = basic1;
		ParseLevel1Basic();
	}
	else
	{
		m_detectedFormat = basic;
		ParseLevel2Basic(basicLoadAddress);
	}
}

void TRS80Loader::ParseLevel2Basic(int basicLoadAddress)
{
	int pos = 0;
	int lineNum = 0;

	while (m_data.AvailableBytes(pos) > 0)
	{
		lineNum++;

		char line[1024];
		// Read a line.
		char *p = line;
		for (;;)
		{
			int ch = GetMediaByte(pos++);
			if (ch == '\n' || ch == '\r' || ch < 0)
			{
				if (ch < 0)
					m_errorMessage = NULL;

				*p++ = '\n';
				*p = '\0';
				break;
			}
			*p++ = ch;
		}

		unsigned char tok[1024];
		int tokenizedLength = tokenize(line, tok);
		if (tokenizedLength <= 0)
		{
			m_errorMessage = "Missing line number or empty line at file line %d.";
			m_errorLine = lineNum;
			return;
		}

		TRS80Record *lineRec = new TRS80Record(TRS80Record::loadmem, basicLoadAddress, tokenizedLength + 2);
		int nextAddr = basicLoadAddress + 2 + tokenizedLength;
		lineRec->SetWord(0, nextAddr);
		for (int i = 0; i < tokenizedLength; i++)
		{
			lineRec->m_data[2 + i] = tok[i];
		}

		AddRecord(lineRec);
		basicLoadAddress = nextAddr;

		while (m_data.AvailableBytes(pos) > 0)
		{
			int ch = GetMediaByte(pos);
			if (ch == '\n' || ch == '\r')
			{
				pos++;
			}
			else
			{
				break;
			}
		}
	}

	SeekMediaByte(pos);

	AddBasicPointers(basicLoadAddress);
}

void TRS80Loader::ParseLevel1Basic()
{
	int addr = 0x4200;
	int pos = 0;
	int lineNum = 0;

	while (m_data.AvailableBytes(pos) > 0)
	{
		char line[1024];
		// Read a line.
		char *p = line;
		for (;;)
		{
			int ch = GetMediaByte(pos++);
			if (ch == '\n' || ch == '\r' || ch < 0)
			{
				if (ch < 0)
					m_errorMessage = NULL;

				*p = '\0';
				break;
			}
			*p++ = ch;
		}

		char *line_end = p;
		p = line;

		while (isspace(*p))
			p++;

		int line_num = atoi(p);

		if (!isdigit(*p))
		{
			m_errorMessage = "Missing line number at file line %d.";
			m_errorLine = lineNum;
			return;
		}

		while (isdigit(*p))
			p++;

		while (isspace(*p))
			p++;

		TRS80Record *lineRec = new TRS80Record(TRS80Record::loadmem, addr, 2 + (line_end - p) + 1);
		lineRec->SetWord(0, line_num);
		addr += 2;
		*line_end = 13; // end of line marker used in-memory
		for (int i = 2; p <= line_end; p++, i++)
		{
			lineRec->m_data[i] = *p;
			addr++;
		}

		AddRecord(lineRec);

		while (m_data.AvailableBytes(pos) > 0)
		{
			int ch = GetMediaByte(pos);
			if (ch == '\n' || ch == '\r')
			{
				pos++;
			}
			else
			{
				break;
			}
		}
	}

	SeekMediaByte(pos);

	// Mark end of program (necessary?)
	TRS80Record *ptr = new TRS80Record(TRS80Record::loadmem, addr, 1, true);
	ptr->m_data[0] = 0;
	AddRecord(ptr);

	// Normally a Level 1 program will load the checksum into memory, but
	// we do not bother.

	ptr = new TRS80Record(TRS80Record::loadmem, 0x406c, 2, true);
	ptr->SetWord(0, addr);
	AddRecord(ptr);
}

static int HexDigitValue(int digit)
{
	if (digit >= '0' && digit <= '9')
		return digit - '0';

	if (digit >= 'A' && digit <= 'F')
		return 10 + digit - 'A';

	if (digit >= 'a' && digit <= 'f')
		return 10 + digit - 'a';

	return -1;
}

static int HexPairValue(int highDigit, int lowDigit)
{
	int high = HexDigitValue(highDigit);
	int low = HexDigitValue(lowDigit);
	if (high >= 0 && low >= 0)
	{
		return high * 16 + low;
	}

	return -1;
}

void TRS80Loader::SkipWhiteSpace(int *pos)
{
	for (;;) {
		int ch = GetMediaByte(*pos);
		if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
			return;

		(*pos)++;
	}
}

int TRS80Loader::GetHexByte(int *pos)
{
	SkipWhiteSpace(pos);
	int hex_high = GetMediaByte(*pos);
	(*pos)++;
	SkipWhiteSpace(pos);
	int hex_low = GetMediaByte(*pos);
	(*pos)++;

	int hexByte = HexPairValue(hex_high, hex_low);
	if (hexByte < 0)
	{
		*pos -= 2;
		if (!m_errorMessage) m_errorMessage = "Bad hexadecimal number.";
	}

	return hexByte;
}

// It's possible that strict Intel Hex record format requires each record
// to be on a single line starting with ':'.  The Tandy ALDS assembler
// outputs records where they're wrapped at 80 columns.  So we'll allow
// white space.  And maybe insist on seeing the ':' at the start of each
// record.

void TRS80Loader::ParseHexFile()
{
	int pos = 0;
	for (;;)
	{
		if (GetMediaByte(pos) != ':') {
			m_errorMessage = "Missing ':' for record start.";
			return;
		}
		pos++;

		int hexRecordLength = GetHexByte(&pos);
		int address = GetHexByte(&pos) << 8;
		address += GetHexByte(&pos);

		// Skip record type byte; we don't need it.  Here 0 == data, 1 == execute
		// Matthew Reed's Z-80 assembler suggests that:
		//	0 == data
		//	1 == EOF
		//	3 == execution address
		// We remain compatible by simply stopping at the first record
		// with zero length.  Which is a bit silly but I don't want to break
		// old zmac output which puts the execution address in apparent
		// EOF records.
		int type = GetHexByte(&pos);

		if (m_errorMessage) return;

		if (hexRecordLength == 0)
		{
			// I assume this is a checksum; not bothering with it for now.
			int checksum = GetHexByte(&pos);
			(void)checksum;
			if (m_errorMessage) return;

			AddRecord(new TRS80Record(TRS80Record::execute, address, 0));

			break;
		}
		else
		{
			TRS80Record *load = new TRS80Record(TRS80Record::loadmem, address, hexRecordLength);
			for (int i = 0; i < hexRecordLength; i++)
			{
				load->m_data[i] = GetHexByte(&pos);
			}
			// I assume this is a checksum; not bothering with it for now.
			int checksum = GetHexByte(&pos);
			(void)checksum;
			if (m_errorMessage)
			{
				delete load;
				return;
			}
			AddRecord(load);
		}

		SkipWhiteSpace(&pos);
	}

	SeekMediaByte(pos);
}

void TRS80Loader::ParseCmdFile()
{
	int pos = 0;

	while (m_data.AvailableBytes(pos) > 0)
	{
		int cmdRecordType = GetMediaByte(pos++);
		if (m_errorMessage) return;
		if (cmdRecordType > 0x1f)
		{
			m_errorMessage = "Invalid /CMD record type.";
			return;
		}

		int length = GetMediaByte(pos++);
		if (m_errorMessage) return;

		TRS80Record::Action recordType;
		int addr = cmdRecordType;
		switch (cmdRecordType)
		{
		case 0:
			AddRecord(new TRS80Record(TRS80Record::eof, 0, 0));
			// TODO - could add crap past end as part of true fidelity.
			SeekMediaByte(pos);
			return;
		case 1:
		case 2:
			addr = GetMediaWord(pos);
			pos += 2;
			if (cmdRecordType == 1)
			{
				length -= 2;
				length &= 255;
				recordType = TRS80Record::loadmem;
			}
			else
			{
				recordType = TRS80Record::execute;
				length = -1;
			}
			break;
		case 5:
			recordType = TRS80Record::name;
			break;
		default:
			recordType = TRS80Record::cmdRecord;
			break;
		}

		if (length == 0)
			length = 256;

		if (length == -1)
			length = 0;

		TRS80Record *rec = new TRS80Record(recordType, addr, length);

		for (int i = 0; i < length; i++)
		{
			rec->m_data[i] = GetMediaByte(pos + i);
		}
		pos += length;

		if (m_errorMessage)
		{
			delete rec;
			return;
		}
		AddRecord(rec);

		// It would seem that crap after the execution address is OK.  I'd like to generate
		// a warning but have no warning mechanism as of yet.  Well, now I do.  But
		// not for such a low-level warning.
		if (recordType == TRS80Record::execute)
		{
			SeekMediaByte(pos);
			return;
		}
	}

	SeekMediaByte(pos);
}

int TRS80Loader::SearchForLeader()
{
	// Look for a low or high speed leader and sync byte.
	// Almost identical to DetectMedia() but will skip over as
	// much data as necessary in search of a leader.  Returns the
	// length of the leader found or zero if not found.  Media
	// type will be set if leader found.  Truncates leader to
	// reasonable length (to be verified by looking ROM output length).

	m_curMedia = justBytes;

	int run01 = 0;
	int run0 = 0;
	int prevBit = 1;
	int i;
	int found = 0;

	for (i = m_bitPos; i < m_data.GetSize() - 8; i++) {
		int nextByte = m_data.GetBits(i, 8);
		if (run0 > 32 && nextByte == 0xA5)
		{
			m_curMedia = cassette500;
			found = 1;
			break;
		}

		if (run01 > 32 && nextByte == 0x7F)
		{
			m_curMedia = cassette1500;
			found = 1;
			break;
		}

		int bit = m_data.GetBits(i, 1);
		if (bit == 0)
			run0++;
		else
			run0 = 0;

		if (bit != prevBit)
			run01++;
		else
			run01 = 0;

		prevBit = bit;
	}

	if (found) {
		i += 8;
		int leaderLength = i - m_bitPos;
		m_bitPos = i;

		if (m_curMedia == cassette500 && m_data.AvailableBits(i) >= 32)
		{
			int loadHi = m_data.GetBits(i, 8);
			int loadLo = m_data.GetBits(i + 8, 8);
			int endHi = m_data.GetBits(i + 16, 8);
			int endLo = m_data.GetBits(i + 24, 8);
			int load = loadHi * 256 + loadLo;
			int end = endHi * 256 + endLo;
			if ((loadHi == 0x40 || loadHi == 0x41 || loadHi == 0x42) 
				&& end > load)
			{
				m_curMedia = cassette250;
			}
		}

		if (leaderLength > 257 * 8)
			leaderLength = 257 * 8;

		return leaderLength;
	}

	return 0;
}

int TRS80Loader::DetectMedia()
{
	int i;
	m_curMedia = justBytes;

	// Try high speed cassette.  Header of $55 bytes followed by $7F.
	// Or {01}* 0111 1111.  Transferred tapes will have the $7F sync byte
	// byte aligned, but we'll do things at a bit level.  But won't go
	// into the machinations of skipping bad bits.
	int leaderLength = 0;
	int bitWanted = 0;
	for (i = m_bitPos; i < m_data.GetSize() - 8; i++)
	{
		if (leaderLength > 32 && m_data.GetBits(i, 8) == 0x7f)
		{
			leaderLength += 8;
			m_curMedia = cassette1500;
			m_bitPos += leaderLength;
			return leaderLength;
		}
		if (leaderLength == 0)
		{
			bitWanted = !m_data.GetBits(i, 1);
			leaderLength++;
		}
		else
		{
			int bit = m_data.GetBits(i, 1);
			if (bit != bitWanted)
			{
				break;
			}
			else
			{
				leaderLength++;
				bitWanted = !bitWanted;
			}
		}
	}

	// Now look at low-speed cassette.  Generally $00 followed by $A5.
	// Or 0* 1010 0101.  But then we'll have to figure out if it was
	// really 250 baud.

	leaderLength = 0;
	for (i = m_bitPos; i < m_data.GetSize() - 8; i++)
	{
		if (leaderLength > 32 && m_data.GetBits(i, 8) == 0xa5)
		{
			i += 8;
			leaderLength = i - m_bitPos;
			m_bitPos = i;
			m_curMedia = cassette500;
			break;
		}
		else if (m_data.GetBits(i, 1) == 0)
		{
			leaderLength++;
		}
		else
		{
			// Don't give up immediately.  Huge kludge for Micro Chess capture.
			leaderLength = 0;
			if (i - m_bitPos > 32)
			{
				break;
			}
		}
	}

	// $40, $41 or $42 will mean Level I.  There are more robust
	// checks we could do but this will suffice.
	if (m_curMedia == cassette500 && m_data.AvailableBits(m_bitPos) >= 32)
	{
		int loadHi = m_data.GetBits(m_bitPos, 8);
		int loadLo = m_data.GetBits(m_bitPos + 8, 8);
		int endHi = m_data.GetBits(m_bitPos + 16, 8);
		int endLo = m_data.GetBits(m_bitPos + 24, 8);
		int load = loadHi * 256 + loadLo;
		int end = endHi * 256 + endLo;
		if ((loadHi == 0x40 || loadHi == 0x41 || loadHi == 0x42) 
				&& end > load)
		{
			m_curMedia = cassette250;
		}

		return leaderLength;
	}

	// We don't really know; leave it to the default we set originally.

	return 0;
}

unsigned char *TRS80Loader::GetRawData()
{
	return m_data.GetPayload();
}

int TRS80Loader::GetMediaSizeInBits()
{
	return m_data.GetSize();
}

int TRS80Loader::GetMediaBit(int n)
{
	if (m_data.AvailableBits(m_bitPos + n) >= 1)
		return m_data.GetBits(m_bitPos + n, 1);

	m_errorMessage = "File too short.";
	return -1;
}

int TRS80Loader::GetMediaByte(int n)
{
	int dataByte = -1;
	switch (m_curMedia)
	{
	default:
	case justBytes:
	case cassette250:
	case cassette500:
		if (m_data.AvailableBits(m_bitPos + n * 8) >= 8)
		{
			dataByte = m_data.GetBits(m_bitPos + n * 8, 8);
		}
		break;
	case cassette1500:
		if (m_data.AvailableBits(m_bitPos + 1 + n * 9) >= 8)
		{
			// Don't care if we have 0 or 1 as a start bit
			dataByte = m_data.GetBits(m_bitPos + 1 + n * 9, 8);
		}
		break;
	}

	if (dataByte < 0)
	{
		m_errorMessage = "File too short.";
	}

	return dataByte;
}

int TRS80Loader::GetMediaWord(int byteOffset)
{
	return GetMediaByte(byteOffset) + (GetMediaByte(byteOffset + 1) << 8);
}

int TRS80Loader::GetMediaWordBigEndian(int byteOffset)
{
	return (GetMediaByte(byteOffset) << 8) + GetMediaByte(byteOffset + 1);
}

void TRS80Loader::SeekBit(int n)
{
	m_bitPos += n;
}

void TRS80Loader::SeekMediaByte(int n)
{
	int bitsPerByte = m_curMedia == cassette1500 ? 9 : 8;
	m_bitPos += n * bitsPerByte;
}

void TRS80Loader::PutRawBits(int length, int bits)
{
	if (m_data.AvailableBits(m_bitPos) >= length)
	{
		m_data.PutBits(m_bitPos, length, bits);
	}
	m_bitPos += length;
}

void TRS80Loader::PutMediaByte(int data) 
{
	if (m_curMedia == cassette1500)
	{
		PutRawBits(1, 0); // 0 start bit
	}
	PutRawBits(8, data);
}

void TRS80Loader::PutMediaWord(int data)
{
	PutMediaByte(data & 255);
	PutMediaByte(data >> 8);
}

void TRS80Loader::PutMediaString(const char *str)
{
	for (; *str; str++)
		PutMediaByte(*str);
}

void TRS80Loader::PutHexByte(int data)
{
	char buf[3];
	snprintf(buf, 3, "%02X", data & 255);
	PutMediaByte(buf[0]);
	PutMediaByte(buf[1]);
}

// Just putting this here out of everyone's hair.

static const char* token[] = {
	"END",
	"FOR", "RESET", "SET", "CLS", "CMD", "RANDOM", "NEXT", "DATA", "INPUT",
	"DIM", "READ", "LET", "GOTO", "RUN", "IF", "RESTORE", "GOSUB", "RETURN",
	"REM", "STOP", "ELSE", "TRON", "TROFF", "DEFSTR", "DEFINT", "DEFSNG",
	"DEFDBL", "LINE", "EDIT", "ERROR", "RESUME", "OUT", "ON", "OPEN",
	"FIELD", "GET", "PUT", "CLOSE", "LOAD", "MERGE", "NAME", "KILL", "LSET",
	"RSET", "SAVE", "SYSTEM", "LPRINT", "DEF", "POKE", "PRINT", "CONT",
	"LIST", "LLIST", "DELETE", "AUTO", "CLEAR", "CLOAD", "CSAVE", "NEW",
	"TAB(", "TO", "FN", "USING", "VARPTR", "USR", "ERL", "ERR", "STRING$",
	"INSTR", "POINT", "TIME$", "MEM", "INKEY$", "THEN", "NOT", "STEP",
	"+", "-", "*", "/", "[", "AND", "OR", ">", "=", "<", "SGN", "INT",
	"ABS", "FRE", "INP", "POS", "SQR", "RND", "LOG", "EXP", "COS", "SIN",
	"TAN", "ATN", "PEEK", "CVI", "CVS", "CVD", "EOF", "LOC", "LOF", "MKI$",
	"MKS$", "MKD$", "CINT", "CSNG", "CDBL", "FIX", "LEN", "STR$", "VAL",
	"ASC", "CHR$", "LEFT$", "RIGHT$", "MID$" };

struct tok {
	const char*	fullname;
	int			len;
	int			token;
	struct tok*	next;
};

static bool made_key_lookup = 0;
static struct tok* char_to_tok[256];

//
// Create a table which will give us each reserved word that starts with
// each character.  This should make the tokenizing quite quick.
//
static int make_key_lookup()
{
	int	i;
	int	n;
	struct tok* t;
	struct tok** tpp;
	char first;

	if (made_key_lookup)
		return 1;

	made_key_lookup = true;

	n = sizeof(token) / sizeof(token[0]);

	if ((t = (struct tok *)calloc(n, sizeof(struct tok))) == NULL) {
		return 0;
	}

	for (i = 0; i < 256; i++)
		char_to_tok[i] = NULL;

	for (i = 0; i < n; i++) {
		first = token[i][0];
		t[i].fullname = token[i];
		t[i].len = strlen(token[i]);
		t[i].token = i + 128;
		t[i].next = NULL;
		for (tpp = &(char_to_tok[first]); *tpp != NULL; tpp = &((*tpp)->next))
			;
		*tpp = t + i;
	}

	return 1;
}

#define REM	(147)
#define DATA (136)
#define REMQUOT (251)
#define ELSE (149)

static int tokenize(const char *line, unsigned char *tok)
{
	int	lnum;
	int ch;
	unsigned char*	tp;
	struct tok* t;

	make_key_lookup();

	tp = tok;

	while (isspace(*line))
		line++;

	lnum = atoi(line);

	if (!isdigit(*line))
		return 0;

	while (isdigit(*line))
		line++;

	while (isspace(*line))
		line++;

	if (!*line)
		return 0;

	*tp++ = lnum & 255;
	*tp++ = (lnum >> 8) & 255;

/*	printf("%d ", lnum); */

	while (*line) {
		switch (*line) {
		case '\n':
			line++;
			break;
		case '\'':
			*tp++ = ':';
			*tp++ = REM;
			*tp++ = REMQUOT;
			line++;
			while ((*tp++ = *line++))
				;
			if (tp[-2] == '\n') {
				tp[-2] = '\0';
				tp--;
			}
			return tp - tok;
		case '"':
			*tp++ = *line++;
			while (*line && *line != '\n') {
				*tp++ = *line++;
				if (line[-1] == '"') {
					break;
				}
			}
			break;
		default:
			ch = *line & 255;
			if (islower(ch))
				ch = toupper(ch);

			for (t = char_to_tok[ch]; t != NULL; t = t->next) {
				if (strnicmp(t->fullname, line, t->len) == 0) {
					ch = t->token;
					line += t->len - 1;
					break;
				}
			}
			line++;

			switch (ch) {
			int inquote;
			default:
				*tp++ = ch;
				break;
			case ELSE:
				if (tp[-1] != ':')
					*tp++ = ':';

				*tp++ = ELSE;
				break;
			case REM:
				*tp++ = REM;
				while ((*tp++ = *line++))
					;
				if (tp[-2] == '\n') {
					tp[-2] = '\0';
					tp--;
				}
				return tp - tok;
			case DATA:
				*tp++ = DATA;
				inquote = 0;
				while (*line && *line != '\n') {
					if (*line == ':' && !inquote)
						break;
					if (*line == '"')
						inquote = !inquote;
					*tp++ = *line++;
				}
				break;
			}
		}
	}
	*tp++ = '\0';
	return tp - tok;
}

void TRS80Loader::List(int len, const unsigned char *data, char eol)
{
	for (;;)
	{
		if (len < 2)
			return;

		int next = data[0] | (data[1] << 8);
		data += 2;
		len -= 2;

		if (!next)
			return;

		if (len < 2)
			return;

		char linenum[32];
		snprintf(linenum, 32, "%d ", data[0] | (data[1] << 8));
		data += 2;
		len -= 2;
		PutMediaString(linenum);

		int tstate = 0;
		while (len > 0)
		{
			int ch = *data++;
			len--;

			if (!ch)
				break;

			if (ch == ':' && tstate == 0) {
				tstate = 3;
				continue;
			}
			else if (ch == REM && tstate == 3) {
				tstate = 4;
				continue;
			}
			else if (ch == REMQUOT && tstate == 4) {
				PutMediaByte('\'');
				tstate = 2;
				continue;
			}
			else if (ch == ELSE && tstate == 3) {
				PutMediaString("ELSE");
				tstate = 0;
				continue;
			}

			if (tstate > 2) {
				PutMediaByte(':');
				if (tstate > 3)
					PutMediaString("REM");

				if (ch != ':')
					tstate = 0;
			}

			switch (tstate) {
			case 0:	/* just cruising over the line */
				if (ch > 127 && ch < 128 + (sizeof token / sizeof token[0]))
					PutMediaString(token[ch - 128]);
				else
					PutMediaByte(ch);

				if (ch == REM)
					tstate = 2;
				else if (ch == '"')
					tstate = 1;
				break;
			case 1: /* inside a quoted string */
				PutMediaByte(ch);
				if (ch == '"')
					tstate = 0;
				break;
			case 2: /* dumping from a REM statement to line's end */
				PutMediaByte(ch);
				break;
			}
		}

		if (tstate > 2) {
			PutMediaByte(':');
			if (tstate > 3)
				PutMediaString("REM");
			tstate = 0;
		}

		PutMediaByte(eol);
	}
}

void TRS80Loader::ListLevel1(int len, const unsigned char *data, char eol)
{
	// We depend on the Level I BASIC program being a single large record.
	// The load address and length (via 2 addresses) have already been
	// extracted into our load record leaving a sequence of:
	//
	//		Little endian word line number
	//		BASIC code in ASCII terminated with 0x0D.

	while (len >= 3)
	{
		char linenum[32];
		snprintf(linenum, 32, "%d", data[0] | (data[1] << 8));
		data += 2;
		len -= 2;
		PutMediaString(linenum);

		while (len > 0 && *data != 13)
		{
			PutMediaByte(*data++);
			len--;
		}

		if (len > 0 && *data == 13)
		{
			len--;
			data++;
		}

		PutMediaByte(eol);
	}
}

// Ugly, platform-specific stuff beyond this point.
// Should be seperate file, etc.

#ifdef _WIN32

#include <windows.h>

void Source::GetLastError()
{ 
    // Retrieve the system error message for the last-error code

	char *msgbuf;

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,

		::GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR) &msgbuf,
		0, NULL );

	// I'll use the C++ allocator, thank you.
	delete [] m_errorMessage;
	int bytesNeeded = strlen(msgbuf) + 1;
	m_errorMessage = new char[bytesNeeded];
	strcpy(m_errorMessage, msgbuf);

	LocalFree(msgbuf);

	m_type = error;
}
#else

#include <errno.h>

void Source::GetLastError()
{
	delete [] m_errorMessage;
	char *err = strerror(errno);
	int bytesNeeded = strlen(err) + 1;
	m_errorMessage = new char[bytesNeeded];
	strcpy(m_errorMessage, err);
	m_type = error;
}

#endif
