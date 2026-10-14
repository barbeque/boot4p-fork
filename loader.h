//
// loader.h -- load most TRS-80 file formats.
//
// Supports (or wants to support):
//
//	.3bn - my format for Model I and Model III system files
//	.hex - Intel hex records (in our case, as output by zmac)
//	.cmd - dumps of /cmd executables from TRS-80 DOSs.
//	.bas - tokenized BASIC files; slight variants between cassette and disk
//	.bba - my binary BASIC format obviously slightly different than .bas
//	.rom - dump of memory from 0 to last address
//	.cas - various TRS-80 cassette file dumps:
//		Level I tapes (basic and system the same but sorta distinguishable)
//		Level II System
//		Level II BASIC
//		Model III System
//		Model III BASIC
//
// Customized cassette loaders, basic data records and edtasm source files
// also considered.

#if !defined(_LOADER_H_)
#define _LOADER_H_

// Basic class is a block of data.
// This may have a list of sources for each range of bits in the data.
// A source is either another data block or a file.
// Records are basically lists of data blocks.
// An Interpreter takes a data block and produces records.
// A record may indicate an error in the data or a processing error.

class Source
{
public:
	Source(const char *name);
	~Source();
	void GetLastError();

	enum Type {
		file,
		block,
		error,
	};

	Type m_type;
	char *m_name;
	char *m_errorMessage;
};

class Block
{
public:
	Block();
	Block(const char *filename);
	Block(const Block *base, int firstBitAddress, int lastBitAddress);
	~Block();

	void LoadFile(const char *filename, const char *data = 0, int length = 0);

	Source *m_source;

	enum Type {
		valid,
		fileError,
		memoryError
	};

	Type m_type;

	int AvailableBits(int bitAddress) const;
	int AvailableBytes(int byteAddress) const;
	int GetBits(int bitAddress, int length) const;
	int GetByte(int byteAddress) const;

	int GetSize() const;

	void SetSize(int bitSize);
	void PutBits(int bitAddress, int length, int bits);
	void PutByte(int byteAddress, int byte);

	unsigned char *GetPayload();

private:
	unsigned char *m_payload;
	int m_bitSize;
};

class TRS80Record
{
public:
	enum Action {
		leader,
		name,
		loadmem,
		execute,
		loadHL,
		originalAddr,	// for tokenized basic programs
		checksum1,		// for Level I 250 baud format
		eof,
		cmdRecord,		// punting on /CMD stuff I don't care about
		warning
	};
	TRS80Record(Action action, int addr, int length, bool artificial = false);
	TRS80Record(const char *);
	~TRS80Record();

	void LoadAll(unsigned char *mem, int *pc, int *hl, unsigned char *loaded = 0);
	void LoadRange(unsigned char *mem, int low, int high, unsigned char *loaded = 0);
	bool LoadsMemory()
	{
		return m_action == loadmem;
	}

	void SetWord(int byteOffset, int value);

	void Dump();
	void DumpData();

	Action m_action;
	int m_size;
	int m_addr;
	bool m_artificial;
	unsigned char *m_data;
	TRS80Record *m_next;
};

class TRS80Loader
{
public:
	TRS80Loader();
	~TRS80Loader();

	enum Media {
		justBytes,
		cassette250,
		cassette500,
		cassette1500,
		mediaUnknown
	};

	enum Format {
		unknown,
		system,
		cmd,
		hex,
		rom,
		basic,
		basic1,
		level1,
		// These two are to trigger output encodings for BASIC
		text,
		list
	};

	enum Encode {
		enc_header = 1,
		enc_name = 2,
		enc_data = 4,
		enc_entry = 8,
		enc_padding = 16
	};

	enum Physical {
		physicalUnknown,
		justBits,
		baud250,
		baud500,
		baud1000,
		baud1500
	};

	void LoadFile(const char *filename, int basicLoadAddress, const char *data = 0, int length = 0);
	void LoadFileData(const char *filename, const char *data = 0, int length = 0);
	void LoadROM();
	void LoadRecords(int basicLoadAddress);

	void LoadMemory(const unsigned char *memory, const unsigned char *loaded, int pc = -1);
	void DisposeFile();
	char *GetErrorMessage();

	void SetCheckChecksum(bool check);
	void SetBasicEntryAddress(int addr);

	bool SetsPC() const;
	int GetEntry() const;
	void SetEntry(int entry);

	bool Encode(const char *name, Format format, Media media = justBytes, int parts = ~0);
	void List(int len, const unsigned char *data, char eol);
	void ListLevel1(int len, const unsigned char *data, char eol);
	void SaveFile(const char *filename, Physical physical = justBits, const char *mode = "wb");

	void Move(int offset);
	void Relocate(int newBase);

	void DumpRecords(bool dataToo = false);

	TRS80Record *m_file;

	int DetectMedia();
	int SearchForLeader();
	int GetMediaBit(int bit_offset);
	int GetMediaByte(int byte_offset);
	void SeekMediaByte(int n);					// relative seek
	void SeekBit(int n);						// relative seek
	int GetMediaBitPos() { return m_bitPos; }
	void SetMediaBitPos(int pos) { m_bitPos = pos; }
	Media GetMediaType() { return m_curMedia; }

	// Format of loaded data.  A guess and probably correct if the
	// load was successful.
	Format GetFileFormat() { return m_detectedFormat; }
	int GetFileName(char *name);

	int GetLowAddress();
	int GetHighAddress();

	unsigned char *GetRawData();
	int GetMediaSizeInBits();

private:
	void ParseSystemTape();
	void ParseLevel1Tape();
	void ParseTokenizedBasicTape(int basicLoadAddress);
	void ParseTokenizedBasic(int basicLoadAddress, int offset);
	void ParseBasic(int basicLoadAddress);
	void ParseLevel1Basic();
	void ParseLevel2Basic(int basicLoadAddress);
	void ParseHexFile();
	void ParseCmdFile();
	void AddRecord(TRS80Record *rec);
	void DeleteRecords(TRS80Record::Action type);
	void AddBasicPointers(int basicEndAddress);
	int GetMediaWord(int n);
	int GetMediaWordBigEndian(int n);
	int GetHexByte(int *pos);
	void SkipWhiteSpace(int *pos);

	void PutRawBits(int length, int bits);
	void PutMediaByte(int data);
	void PutMediaWord(int data);
	void PutMediaString(const char *str);
	void PutHexByte(int data);

	bool FilePutChar(FILE *fp, int ch);

	Block m_data;

	Media m_curMedia;

	int m_bitPos;

	Format m_detectedFormat;

	const char *m_errorMessage;
	int m_errorLine;

	bool m_ignoreChecksumError;

	int m_basicEntryAddress;
};

void NameFromFilename(TRS80Loader::Format format, const char *filename, char *name);

#endif /* !defined(_LOADER_H_) */
