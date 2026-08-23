#include "retro_common.h"
#include "cd_interface.h"
#include "burner.h"
#include "burnint.h"
#ifdef INCLUDE_CHD_SUPPORT
#include "cd_chd.h"
#endif

// libretro wrapper for dprint function

#define DPRINTF_BUFFER_SIZE 512
char dprintf_buf[DPRINTF_BUFFER_SIZE];
static INT32 __cdecl libretro_dprintf(TCHAR* szFormat, ...)
{
	va_list vp;
	va_start(vp, szFormat);
	int rc = vsnprintf(dprintf_buf, DPRINTF_BUFFER_SIZE, szFormat, vp);
	va_end(vp);

	if (rc >= 0)
		log_cb(RETRO_LOG_INFO, dprintf_buf);

	return rc;
}
#define dprintf libretro_dprintf

// functions from src/burner/misc.cpp

TCHAR* ExtractFilename(TCHAR* fullname)
{
	TCHAR* filename = fullname + _tcslen(fullname);

	do {
		filename--;
	} while (filename >= fullname && *filename != _T('\\') && *filename != _T('/') && *filename != _T(':'));

	return filename;
}

TCHAR* LabelCheck(TCHAR* s, TCHAR* pszLabel)
{
	INT32 nLen;
	if (s == NULL) {
		return NULL;
	}
	if (pszLabel == NULL) {
		return NULL;
	}
	nLen = _tcslen(pszLabel);

	SKIP_WS(s);													// Skip whitespace

	if (_tcsncmp(s, pszLabel, nLen)){							// Doesn't match
		return NULL;
	}
	return s + nLen;
}

INT32 QuoteRead(TCHAR** ppszQuote, TCHAR** ppszEnd, TCHAR* pszSrc)	// Read a (quoted) string from szSrc and poINT32 to the end
{
	static TCHAR szQuote[QUOTE_MAX];
	TCHAR* s = pszSrc;
	TCHAR* e;

	// Skip whitespace
	SKIP_WS(s);

	e = s;

	if (*s == _T('\"')) {										// Quoted string
		s++;
		e++;
		// Find end quote
		FIND_QT(e);
		_tcsncpy(szQuote, s, e - s);
		// Zero-terminate
		szQuote[e - s] = _T('\0');
		e++;
	} else {													// Non-quoted string
		// Find whitespace
		FIND_WS(e);
		_tcsncpy(szQuote, s, e - s);
		// Zero-terminate
		szQuote[e - s] = _T('\0');
	}

	if (ppszQuote) {
		*ppszQuote = szQuote;
	}
	if (ppszEnd)	{
		*ppszEnd = e;
	}

	return 0;
}

TCHAR *FileExt(TCHAR *str)
{
	TCHAR *dot = strrchr(str, _T('.'));

	return (dot) ? StrLower(dot) : str;
}

bool IsFileExt(TCHAR *str, TCHAR *ext)
{
	return (_tcsicmp(ext, FileExt(str)) == 0);
}

TCHAR *StrReplace(TCHAR *str, TCHAR find, TCHAR replace)
{
	INT32 length = _tcslen(str);

	for (INT32 i = 0; i < length; i++) {
		if (str[i] == find) str[i] = replace;
	}

	return str;
}

// StrLower() - leaves str untouched, returns modified string
TCHAR *StrLower(TCHAR *str)
{
	static TCHAR szBuffer[256] = _T("");
	INT32 length = _tcslen(str);

	if (length > 255) length = 255;

	for (INT32 i = 0; i < length; i++) {
		if (str[i] >= _T('A') && str[i] <= _T('Z'))
			szBuffer[i] = (str[i] + _T(' '));
		else
			szBuffer[i] = str[i];
	}
	szBuffer[length] = 0;

	return &szBuffer[0];
}

// variables from src/intf/cd/cd_interface.cpp

bool bCDEmuOkay = false;
CDEmuStatusValue CDEmuStatus;
TCHAR CDEmuImage[MAX_PATH];
UINT8 CDEmuImageTOCSHA1[MAX_PATH] = { 0, };

// variables from cd_img.h
// we don't need to share them across files so it's fine if they are not in a header

#define CDIMAGE_MAX_TRACKS       (99)
#define CDIMAGE_MAX_SOURCE_FILES (CDIMAGE_MAX_TRACKS + 2)

enum {
	CDIMAGE_TRACK_AUDIO = 0,
	CDIMAGE_TRACK_MODE1_2048,
	CDIMAGE_TRACK_MODE1_2352,
	CDIMAGE_TRACK_MODE2_2336,
	CDIMAGE_TRACK_MODE2_2352
};

struct CDImageTrack {
	INT32 nNumber;
	INT32 nType;
	INT32 nControl;
	INT32 nStartLBA;
	INT32 nIndex0LBA;
	INT32 nIndex1LBA;
	INT32 nPregap;
	INT32 nSectors;
	INT32 nSectorSize;
	INT32 nUserOffset;
	INT32 nUserSize;
	const TCHAR* szPath;
};

#define cdimgFseek fseek
#define cdimgFtell ftell

// cd_img internal functions, no deps to interface.cpp/cd_interface.cpp, shared with standalone

#include "cd_img.inc"

// our simplified version of src/intf/cd/cd_interface.cpp

INT32 CDEmuExit() {
	if (!bCDEmuOkay) {
		return 1;
	}
	bCDEmuOkay = false;
	return cdimgExit();
}
INT32 CDEmuInit() {
	INT32 nRet;
	CDEmuStatus = idle;
	if ((nRet = cdimgInit()) == 0) {
		bCDEmuOkay = true;
	}
	return nRet;
}
INT32 CDEmuStop() {
	if (!bCDEmuOkay) {
		return 1;
	}
	return cdimgStop();
}
INT32 CDEmuPlay(UINT8 M, UINT8 S, UINT8 F) {
	if (!bCDEmuOkay) {
		return 1;
	}
	return cdimgPlay(M, S, F);
}
INT32 CDEmuLoadSector(INT32 LBA, char* pBuffer) {
	if (!bCDEmuOkay) {
		return 0;
	}
	return cdimgLoadSector(LBA, pBuffer);
}
INT32 CDEmuReadDataSector(INT32 nLba, UINT8* pBuffer)
{
	if (!bCDEmuOkay) {
		return 1;
	}
	return cdimgReadDataSector(nLba, pBuffer);
}
UINT8* CDEmuReadTOC(INT32 track) {
	if (!bCDEmuOkay) {
		return NULL;
	}
	return cdimgReadTOC(track);
}
UINT8* CDEmuReadQChannel() {
	if (!bCDEmuOkay) {
		return NULL;
	}
	return cdimgReadQChannel();
}
INT32 CDEmuGetSoundBuffer(INT16* buffer, INT32 samples) {
	if (!bCDEmuOkay) {
		return 1;
	}
	return cdimgGetSoundBuffer(buffer, samples);
}
INT32 CDEmuSetVolume(double dVolume) {
	if (!bCDEmuOkay) {
		return 1;
	}
	return cdimgSetVolume(dVolume);
}
INT32 CDEmuGetCurrentLBA() {
	if (!bCDEmuOkay) {
		return 0;
	}
	return cdimgGetCurrentLBA();
}
INT32 CDEmuScan(INT32 nAction, INT32 *pnMin)
{
	if (!bCDEmuOkay) {
		return 1;
	}
	return cdimgScan(nAction, pnMin);
}
