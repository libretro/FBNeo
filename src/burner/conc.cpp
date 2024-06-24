#include "burner.h"
#include <vector>
#include <string>

#define HW_NES ( ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_NES) || ((BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK) == HARDWARE_FDS) )
std::vector<char> CurrentMameCheatContent; // Global
std::vector<char> CurrentIniCheatContent; // Global
int usedCheatType = 0; //Global so we'll know if cheatload is already done or which cheat type it uses? 

static bool SkipComma(TCHAR** s)
{
	while (**s && **s != _T(',')) {
		(*s)++;
	}

	if (**s == _T(',')) {
		(*s)++;
	}

	if (**s) {
		return true;
	}

	return false;
}

static void CheatError(TCHAR* pszFilename, INT32 nLineNumber, CheatInfo* pCheat, TCHAR* pszInfo, TCHAR* pszLine)
{
#if defined (BUILD_WIN32)
	FBAPopupAddText(PUF_TEXT_NO_TRANSLATE, _T("Cheat file %s is malformed.\nPlease remove or repair the file.\n\n"), pszFilename);
	if (pCheat) {
		FBAPopupAddText(PUF_TEXT_NO_TRANSLATE, _T("Parse error at line %i, in cheat \"%s\".\n"), nLineNumber, pCheat->szCheatName);
	} else {
		FBAPopupAddText(PUF_TEXT_NO_TRANSLATE, _T("Parse error at line %i.\n"), nLineNumber);
	}

	if (pszInfo) {
		FBAPopupAddText(PUF_TEXT_NO_TRANSLATE, _T("Problem:\t%s.\n"), pszInfo);
	}
	if (pszLine) {
		FBAPopupAddText(PUF_TEXT_NO_TRANSLATE, _T("Text:\t%s\n"), pszLine);
	}

	FBAPopupDisplay(PUF_TYPE_ERROR);
#endif

#if defined(BUILD_SDL2)
	printf("Cheat file %s is malformed.\nPlease remove or repair the file.\n\n", pszFilename);
	if (pCheat) {
		printf("Parse error at line %i, in cheat \"%s\".\n", nLineNumber, pCheat->szCheatName);
	} else {
		printf("Parse error at line %i.\n", nLineNumber);
	}

	if (pszInfo) {
		printf("Problem:\t%s.\n", pszInfo);
	}
	if (pszLine) {
		printf("Text:\t%s\n", pszLine);
	}
#endif
}

// pszFilename only uses for cheaterror as string while iniContent,not as file
// while no iniContent,process ini File
static INT32 ConfigParseFile(TCHAR* pszFilename, const std::vector<char>* iniContent = NULL)
{
#define INSIDE_NOTHING (0xFFFF & (1 << ((sizeof(TCHAR) * 8) - 1)))

	TCHAR szLine[8192];
	TCHAR* s;
	TCHAR* t;
	INT32 nLen;

	INT32 nLine = 0;
	TCHAR nInside = INSIDE_NOTHING;

	CheatInfo* pCurrentCheat = NULL;

	FILE* h = NULL;
	const char* iniPtr = NULL;

	if (iniContent) {
		iniPtr = iniContent->data();
	} else {
		h = _tfopen(pszFilename, _T("rt"));
		if (h == NULL) {
			return 1;
		}
	}

	while (1) {
		if (iniContent) {
			if (*iniPtr == '\0') {
				break;
			}
			char* s = szLine;
			while (*iniPtr && *iniPtr != '\n') {
				*s++ = *iniPtr++;
			}
			if (*iniPtr == '\n') {
				*s++ = *iniPtr++;
			}
			*s = '\0';
		} else {
			if (_fgetts(szLine, 8192, h) == NULL) {
				break;
			}
		}

		nLine++;

		nLen = _tcslen(szLine);

		// Get rid of the linefeed at the end
		while ((nLen > 0) && (szLine[nLen - 1] == 0x0A || szLine[nLen - 1] == 0x0D)) {
			szLine[nLen - 1] = 0;
			nLen--;
		}

		s = szLine;													// Start parsing

		if (s[0] == _T('/') && s[1] == _T('/')) {					// Comment
			continue;
		}

		if (!iniContent) {
			if ((t = LabelCheck(s, _T("include"))) != 0) {				// Include a file
				s = t;

				TCHAR szFilename[MAX_PATH] = _T("");

				// Read name of the cheat file
				TCHAR* szQuote = NULL;
				QuoteRead(&szQuote, NULL, s);

				_stprintf(szFilename, _T("%s%s.dat"), szAppCheatsPath, szQuote);	// Is it a fault?Why do we read a NebulaDatCheat here?
																					// Never mind,we already checked included ini before read to inicontent.
				if (ConfigParseFile(szFilename)) {
					_stprintf(szFilename, _T("%s%s.ini"), szAppCheatsPath, szQuote);
					if (ConfigParseFile(szFilename)) {
						CheatError(pszFilename, nLine, NULL, _T("included file doesn't exist"), szLine);
					}
				}

				continue;
			}
		}

		if ((t = LabelCheck(s, _T("cheat"))) != 0) {				// Add new cheat
			s = t;

			// Read cheat name
			TCHAR* szQuote = NULL;
			TCHAR* szEnd = NULL;

			QuoteRead(&szQuote, &szEnd, s);

			s = szEnd;

			if ((t = LabelCheck(s, _T("advanced"))) != 0) {			// Advanced cheat
				s = t;
			}

			SKIP_WS(s);

			if (nInside == _T('{')) {
				CheatError(pszFilename, nLine, pCurrentCheat, _T("missing closing bracket"), NULL);
				break;
			}
#if 0
			if (*s != _T('\0') && *s != _T('{')) {
				CheatError(pszFilename, nLine, NULL, _T("malformed cheat declaration"), szLine);
				break;
			}
#endif
			nInside = *s;

			// Link new node into the list
			CheatInfo* pPreviousCheat = pCurrentCheat;
			pCurrentCheat = (CheatInfo*)malloc(sizeof(CheatInfo));
			if (pCheatInfo == NULL) {
				pCheatInfo = pCurrentCheat;
			}

			memset(pCurrentCheat, 0, sizeof(CheatInfo));
			pCurrentCheat->pPrevious = pPreviousCheat;
			if (pPreviousCheat) {
				pPreviousCheat->pNext = pCurrentCheat;
			}

			// Fill in defaults
			pCurrentCheat->nType = 0;								// Default to cheat type 0 (apply each frame)
			pCurrentCheat->nStatus = -1;							// Disable cheat

			memcpy(pCurrentCheat->szCheatName, szQuote, QUOTE_MAX);

			continue;
		}

		if ((t = LabelCheck(s, _T("type"))) != 0) {					// Cheat type
			if (nInside == INSIDE_NOTHING || pCurrentCheat == NULL) {
				CheatError(pszFilename, nLine, pCurrentCheat, _T("rogue cheat type"), szLine);
				break;
			}
			s = t;

			// Set type
			pCurrentCheat->nType = _tcstol(s, NULL, 0);

			continue;
		}

		if ((t = LabelCheck(s, _T("default"))) != 0) {				// Default option
			if (nInside == INSIDE_NOTHING || pCurrentCheat == NULL) {
				CheatError(pszFilename, nLine, pCurrentCheat, _T("rogue default"), szLine);
				break;
			}
			s = t;

			// Set default option
			pCurrentCheat->nDefault = _tcstol(s, NULL, 0);

			continue;
		}

		INT32 n = _tcstol(s, &t, 0);
		if (t != s) {				   								// New option

			if (nInside == INSIDE_NOTHING || pCurrentCheat == NULL) {
				CheatError(pszFilename, nLine, pCurrentCheat, _T("rogue option"), szLine);
				break;
			}

			// Link a new Option structure to the cheat
			if (n < CHEAT_MAX_OPTIONS) {
				s = t;

				// Read option name
				TCHAR* szQuote = NULL;
				TCHAR* szEnd = NULL;
				if (QuoteRead(&szQuote, &szEnd, s)) {
					CheatError(pszFilename, nLine, pCurrentCheat, _T("option name omitted"), szLine);
					break;
				}
				s = szEnd;

				if (pCurrentCheat->pOption[n] == NULL) {
					pCurrentCheat->pOption[n] = (CheatOption*)malloc(sizeof(CheatOption));
				}
				memset(pCurrentCheat->pOption[n], 0, sizeof(CheatOption));

				memcpy(pCurrentCheat->pOption[n]->szOptionName, szQuote, QUOTE_MAX * sizeof(TCHAR));

				INT32 nCurrentAddress = 0;
				bool bOK = true;
				while (nCurrentAddress < CHEAT_MAX_ADDRESS) {
					INT32 nCPU = 0, nAddress = 0, nValue = 0;

					if (SkipComma(&s)) {
						if (HW_NES) {
							t = s;
							INT32 newlen = 0;
#if defined(BUILD_WIN32)
							for (INT32 z = 0; z < lstrlen(t); z++) {
#else
							for (INT32 z = 0; z < strlen(t); z++) {
#endif
								char c = toupper((char)*s);
								if (c >= 'A' && c <= 'Z' && newlen < 10)
									pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].szGenieCode[newlen++] = c;
								s++;
								if (*s == _T(',')) break;
							}
							nAddress = 0xffff; // nAddress not used, but needs to be nonzero (NES/Game Genie)
						} else {
							nCPU = _tcstol(s, &t, 0);		// CPU number
							if (t == s) {
								CheatError(pszFilename, nLine, pCurrentCheat, _T("CPU number omitted"), szLine);
								bOK = false;
								break;
							}
							s = t;

							SkipComma(&s);
							nAddress = _tcstol(s, &t, 0);	// Address
							if (t == s) {
								bOK = false;
								CheatError(pszFilename, nLine, pCurrentCheat, _T("address omitted"), szLine);
								break;
							}
							s = t;

							SkipComma(&s);
							nValue = _tcstol(s, &t, 0);		// Value
							if (t == s) {
								bOK = false;
								CheatError(pszFilename, nLine, pCurrentCheat, _T("value omitted"), szLine);
								break;
							}
						}
					} else {
						if (nCurrentAddress) {			// Only the first option is allowed no address
							break;
						}
						if (n) {
							bOK = false;
							CheatError(pszFilename, nLine, pCurrentCheat, _T("CPU / address / value omitted"), szLine);
							break;
						}
					}

					pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].nCPU = nCPU;
					pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].nAddress = nAddress;
					pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].nValue = nValue;
					nCurrentAddress++;
				}

				if (!bOK) {
					break;
				}

			}

			continue;
		}

		SKIP_WS(s);
		if (*s == _T('}')) {
			if (nInside != _T('{')) {
				CheatError(pszFilename, nLine, pCurrentCheat, _T("missing opening bracket"), NULL);
				break;
			}

			nInside = INSIDE_NOTHING;
		}

		// Line isn't (part of) a valid cheat
#if 0
		if (*s) {
			CheatError(pszFilename, nLine, NULL, _T("rogue line"), szLine);
			break;
		}
#endif

	}

	if (h) {
		fclose(h);
		usedCheatType = 4; // see usedCheatType define
	} else {
		usedCheatType = 3; // see usedCheatType define
	}

	return 0;
}

//TODO: make cross platform
static INT32 ConfigParseNebulaFile(TCHAR* pszFilename)
{
	FILE *fp = _tfopen(pszFilename, _T("rt"));
	if (fp == NULL) {
		return 1;
	}

	INT32 nLen;
	INT32 i, j, n = 0;
	TCHAR tmp[32];
	TCHAR szLine[1024];

	CheatInfo* pCurrentCheat = NULL;

	while (1)
	{
		if (_fgetts(szLine, 1024, fp) == NULL)
			break;

		nLen = _tcslen(szLine);

		if (nLen < 3 || szLine[0] == '[') continue;

		if (!_tcsncmp (_T("Name="), szLine, 5))
		{
			n = 0;

			// Link new node into the list
			CheatInfo* pPreviousCheat = pCurrentCheat;
			pCurrentCheat = (CheatInfo*)malloc(sizeof(CheatInfo));
			if (pCheatInfo == NULL) {
				pCheatInfo = pCurrentCheat;
			}

			memset(pCurrentCheat, 0, sizeof(CheatInfo));
			pCurrentCheat->pPrevious = pPreviousCheat;
			if (pPreviousCheat) {
				pPreviousCheat->pNext = pCurrentCheat;
			}

			// Fill in defaults
			pCurrentCheat->nType = 0;							// Default to cheat type 0 (apply each frame)
			pCurrentCheat->nStatus = -1;							// Disable cheat
			pCurrentCheat->nDefault = 0;							// Set default option

			_tcsncpy (pCurrentCheat->szCheatName, szLine + 5, QUOTE_MAX);
			pCurrentCheat->szCheatName[nLen-6] = '\0';

			continue;
		}

		if (!_tcsncmp (_T("Default="), szLine, 8) && n >= 0)
		{
			_tcsncpy (tmp, szLine + 8, nLen-9);
			tmp[nLen-9] = '\0';
#if defined(BUILD_WIN32)
			_stscanf (tmp, _T("%d"), &(pCurrentCheat->nDefault));
#else
			sscanf (tmp, _T("%d"), &(pCurrentCheat->nDefault));
#endif
			continue;
		}


		i = 0, j = 0;
		while (i < nLen)
		{
			if (szLine[i] == '=' && i < 4) j = i+1;
			if (szLine[i] == ',' || szLine[i] == '\r' || szLine[i] == '\n')
			{
				if (pCurrentCheat->pOption[n] == NULL) {
					pCurrentCheat->pOption[n] = (CheatOption*)malloc(sizeof(CheatOption));
				}
				memset(pCurrentCheat->pOption[n], 0, sizeof(CheatOption));

				_tcsncpy (pCurrentCheat->pOption[n]->szOptionName, szLine + j, QUOTE_MAX * sizeof(TCHAR));
				pCurrentCheat->pOption[n]->szOptionName[i-j] = '\0';

				i++; j = i;
				break;
			}
			i++;
		}

		INT32 nAddress = -1, nValue = 0, nCurrentAddress = 0;
		while (nCurrentAddress < CHEAT_MAX_ADDRESS)
		{
			if (i == nLen) break;

			if (szLine[i] == ',' || szLine[i] == '\r' || szLine[i] == '\n')
			{
				_tcsncpy (tmp, szLine + j, i-j);
				tmp[i-j] = '\0';

				if (nAddress == -1) {
#if defined(BUILD_WIN32)
					_stscanf (tmp, _T("%x"), &nAddress);
#else
					sscanf (tmp, _T("%x"), &nAddress);
#endif
				} else {
#if defined(BUILD_WIN32)
					_stscanf (tmp, _T("%x"), &nValue);
#else
					sscanf (tmp, _T("%x"), &nValue);
#endif

					pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].nCPU = 0; 	// Always
					pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].nAddress = nAddress ^ 1;
					pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].nValue = nValue;
					nCurrentAddress++;

					nAddress = -1;
					nValue = 0;
				}
				j = i+1;
			}
			i++;
		}
		n++;
	}

	fclose (fp);
	usedCheatType = 5;// see usedCheatType define
	return 0;
}

#define IS_MIDWAY ((BurnDrvGetHardwareCode() & HARDWARE_PREFIX_MIDWAY) == HARDWARE_PREFIX_MIDWAY)

static INT32 ConfigParseMAMEFile_internal(const TCHAR *name)
{
#define AddressInfo()	\
	INT32 k = (flags >> 20) & 3;	\
	INT32 cpu = (flags >> 24) & 0x1f; \
	if (cpu > 3) cpu = 0; \
	for (INT32 i = 0; i < k+1; i++) {	\
		pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].nCPU = cpu;	\
		if ((flags & 0xf0000000) == 0x80000000) { \
			pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].bRelAddress = 1; \
			pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].nRelAddressOffset = nAttrib; \
			pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].nRelAddressBits = (flags & 0x3000000) >> 24; \
		} \
		pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].nAddress = (pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].bRelAddress) ? nAddress : nAddress + i;	\
		pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].nExtended = nAttrib; \
		pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].nValue = (nValue >> ((k*8)-(i*8))) & 0xff;	\
		pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].nMask = (nAttrib >> ((k*8)-(i*8))) & 0xff;	\
		pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].nMultiByte = i;	\
		pCurrentCheat->pOption[n]->AddressInfo[nCurrentAddress].nTotalByte = k+1;	\
		nCurrentAddress++;	\
	}	\

#define OptionName(a)	\
	if (pCurrentCheat->pOption[n] == NULL) {						\
		pCurrentCheat->pOption[n] = (CheatOption*)malloc(sizeof(CheatOption));		\
	}											\
	memset(pCurrentCheat->pOption[n], 0, sizeof(CheatOption));				\
	_tcsncpy (pCurrentCheat->pOption[n]->szOptionName, a, QUOTE_MAX * sizeof(TCHAR));	\

#define tmpcpy(a)	\
	_tcsncpy (tmp, szLine + c0[a] + 1, c0[a+1] - (c0[a]+1));	\
	tmp[c0[a+1] - (c0[a]+1)] = '\0';				\

	TCHAR tmp[256];
	TCHAR tmp2[256];
	TCHAR gName[64];
	TCHAR szLine[1024];

	INT32 nLen;
	INT32 n = 0;
	INT32 menu = 0;
	INT32 nFound = 0;
	INT32 nCurrentAddress = 0;
	UINT32 flags = 0;
	UINT32 nAddress = 0;
	UINT32 nValue = 0;
	UINT32 nAttrib = 0;

	CheatInfo* pCurrentCheat = NULL;
	_stprintf(gName, _T(":%s:"), name);

	const char* iniPtr = CurrentMameCheatContent.data();
	while (*iniPtr)
	{
		char* s = szLine;
		while (*iniPtr && *iniPtr != '\n') {
			*s++ = *iniPtr++;
		}
		// szLine should include '\n'
		if (*iniPtr == '\n') {
			*s++ = *iniPtr++;
		}
		*s = '\0';

		nLen = _tcslen (szLine);

		if (szLine[0] == ';') continue;

		/*
		 // find the cheat flags & 0x80000000 cheats (for debugging) -dink
		 int derpy = 0;
		 for (INT32 i = 0; i < nLen; i++) {
		 	if (szLine[i] == ':') {
		 		derpy++;
		 		if (derpy == 2 && szLine[i+1] == '8') {
					bprintf(0, _T("%s\n"), szLine);
				}
			}
		}
		*/

#if defined(BUILD_WIN32)
		if (_tcsncmp (szLine, gName, lstrlen(gName))) {
#else
		if (_tcsncmp (szLine, gName, strlen(gName))) {
#endif
			if (nFound) break;
			else continue;
		}

		if (_tcsstr(szLine, _T("----:REASON"))) {
			// reason to leave!
			break;
		}

		nFound = 1;

		INT32 c0[16], c1 = 0;					// find colons / break
		for (INT32 i = 0; i < nLen; i++)
			if (szLine[i] == ':' || szLine[i] == '\r' || szLine[i] == '\n')
				c0[c1++] = i;

		tmpcpy(1);						// control flags
#if defined(BUILD_WIN32)
		_stscanf (tmp, _T("%x"), &flags);
#else
		sscanf (tmp, _T("%x"), &flags);
#endif

		tmpcpy(2);						// cheat address
#if defined(BUILD_WIN32)
		_stscanf (tmp, _T("%x"), &nAddress);
#else
		sscanf (tmp, _T("%x"), &nAddress);
#endif

		tmpcpy(3);						// cheat value
#if defined(BUILD_WIN32)
		_stscanf (tmp, _T("%x"), &nValue);
#else
		sscanf (tmp, _T("%x"), &nValue);
#endif

		tmpcpy(4);						// cheat attribute
#if defined(BUILD_WIN32)
		_stscanf (tmp, _T("%x"), &nAttrib);
#else
		sscanf (tmp, _T("%x"), &nAttrib);
#endif

		tmpcpy(5);						// cheat name

		// & 0x4000 = don't add to list
		// & 0x0800 = BCD
		if (flags & 0x00004800) continue;			// skip various cheats (unhandled methods at this time)

		if ((flags & 0xff000000) == 0x39000000 && IS_MIDWAY) {
			nAddress |= 0xff800000 >> 3; // 0x39 = address is relative to system's ROM block, only midway uses this kinda cheats
		}

		if ( flags & 0x00008000 || (flags & 0x00010000 && !menu)) { // Linked cheat "(2/2) etc.."
			if (nCurrentAddress < CHEAT_MAX_ADDRESS) {
				AddressInfo();
			}

			continue;
		}

		if (~flags & 0x00010000) {
			n = 0;
			menu = 0;
			nCurrentAddress = 0;

			// Link new node into the list
			CheatInfo* pPreviousCheat = pCurrentCheat;
			pCurrentCheat = (CheatInfo*)malloc(sizeof(CheatInfo));
			if (pCheatInfo == NULL) {
				pCheatInfo = pCurrentCheat;
			}

			memset(pCurrentCheat, 0, sizeof(CheatInfo));
			pCurrentCheat->pPrevious = pPreviousCheat;
			if (pPreviousCheat) {
				pPreviousCheat->pNext = pCurrentCheat;
			}

			// Fill in defaults
			pCurrentCheat->nType = 0;								// Default to cheat type 0 (apply each frame)
			pCurrentCheat->nStatus = -1;							// Disable cheat
			pCurrentCheat->nDefault = 0;							// Set default option
			pCurrentCheat->bOneShot = 0;							// Set default option (off)
			pCurrentCheat->bWatchMode = 0;							// Set default option (off)

			_tcsncpy (pCurrentCheat->szCheatName, tmp, QUOTE_MAX);

#if defined(BUILD_WIN32)
			if (lstrlen(tmp) <= 0 || flags == 0x60000000) {
#else
			if (strlen(tmp) <= 0 || flags == 0x60000000) {
#endif
				n++;
				continue;
			}

			OptionName(_T("Disabled"));

			if (nAddress) {
				if ((flags & 0x80018) == 0 && nAttrib != 0xffffffff) {
					pCurrentCheat->bWriteWithMask = 1; // nAttrib field is the mask
				}
				if (flags & 0x1) {
					pCurrentCheat->bOneShot = 1; // apply once and stop
				}
				if (flags & 0x2) {
					pCurrentCheat->bWaitForModification = 1; // wait for modification before changing
				}
				if (flags & 0x80000) {
					pCurrentCheat->bWaitForModification = 2; // check address against extended field before changing
				}
				if (flags & 0x800000) {
					pCurrentCheat->bRestoreOnDisable = 1; // restore previous value on disable
				}
				if (flags & 0x3000) {
					pCurrentCheat->nPrefillMode = (flags & 0x3000) >> 12;
				}
				if ((flags & 0x6) == 0x6) {
					pCurrentCheat->bWatchMode = 1; // display value @ address
				}
				if (flags & 0x100) { // add options
					INT32 nTotal = nValue + 1;
					INT32 nPlus1 = (flags & 0x200) ? 1 : 0; // displayed value +1?
					INT32 nStartValue = (flags & 0x400) ? 1 : 0; // starting value

					//bprintf(0, _T("adding .. %X. options\n"), nTotal);
					if (nTotal > 0xff) continue; // bad entry (roughrac has this)
					for (nValue = nStartValue; nValue < nTotal; nValue++) {
#if defined(UNICODE)
						swprintf(tmp2, L"# %d.", nValue + nPlus1);
#else
						sprintf(tmp2, _T("# %d."), nValue + nPlus1);
#endif
						n++;
						nCurrentAddress = 0;
						OptionName(tmp2);
						AddressInfo();
					}
				} else {
					n++;
					OptionName(tmp);
					AddressInfo();
				}
			} else {
				menu = 1;
			}

			continue;
		}

		if ( flags & 0x00010000 && menu) {
			n++;
			nCurrentAddress = 0;

			if ((flags & 0x80018) == 0 && nAttrib != 0xffffffff) {
				pCurrentCheat->bWriteWithMask = 1; // nAttrib field is the mask
			}
			if (flags & 0x1) {
				pCurrentCheat->bOneShot = 1; // apply once and stop
			}
			if (flags & 0x2) {
				pCurrentCheat->bWaitForModification = 1; // wait for modification before changing
			}
			if (flags & 0x80000) {
				pCurrentCheat->bWaitForModification = 2; // check address against extended field before changing
			}
			if (flags & 0x800000) {
				pCurrentCheat->bRestoreOnDisable = 1; // restore previous value on disable
			}
			if (flags & 0x3000) {
				pCurrentCheat->nPrefillMode = (flags & 0x3000) >> 12;
			}
			if ((flags & 0x6) == 0x6) {
				pCurrentCheat->bWatchMode = 1; // display value @ address
			}

			OptionName(tmp);
			AddressInfo();

			continue;
		}
	}

	// if no cheat was found, don't return success code
	if (pCurrentCheat == NULL) return 1;
	return 0;
}

static INT32 ExtractMameCheatFromDat(FILE* MameDatCheat, const TCHAR* matchDrvName) {

	CurrentMameCheatContent.clear();
	TCHAR szLine[1024];
	TCHAR gName[64];
	_stprintf(gName, _T(":%s:"), matchDrvName);

	bool foundData = false;

	while (_fgetts(szLine, 1024, MameDatCheat) != NULL) {
		// Check if the current line contains matchDrvName
#if defined(BUILD_WIN32)
		if (_tcsncmp(szLine, gName, lstrlen(gName)) == 0) {
#else
		if (_tcsncmp(szLine, gName, strlen(gName)) == 0) {
#endif
			if (!foundData) {
				foundData = true;
			}
			// Add the current line to CurrentMameCheatContent
			for (TCHAR* p = szLine; *p; ++p) {
				CurrentMameCheatContent.push_back(*p);
			}
		}
	}

	if (!foundData) {
		return 1;
	}

	return 0;
}

static INT32 ConfigParseMAMEFile()
{
	TCHAR szFileName[MAX_PATH] = _T("");
	_stprintf(szFileName, _T("%scheat.dat"), szAppCheatsPath);

	FILE *fz = _tfopen(szFileName, _T("rt"));
	INT32 ret = 1;

	const TCHAR* DrvName = BurnDrvGetText(DRV_NAME);

	if (fz) {
		ret = ExtractMameCheatFromDat(fz, DrvName);
		if (ret == 0) {
			ret = ConfigParseMAMEFile_internal(DrvName);
			usedCheatType = (ret == 0) ? 1 : usedCheatType;	// see usedCheatType define
		}
		// let's try using parent entry as a fallback if no cheat was found for this romset
		if (ret > 0 && (BurnDrvGetFlags() & BDF_CLONE) && BurnDrvGetText(DRV_PARENT)) {
			fseek(fz, 0, SEEK_SET);
			DrvName = BurnDrvGetText(DRV_PARENT);
			ret = ExtractMameCheatFromDat(fz, DrvName);
			if (ret == 0) {
				ret = ConfigParseMAMEFile_internal(DrvName);
				usedCheatType = (ret == 0) ? 2 : usedCheatType; // see usedCheatType define
			}
		}

		fclose(fz);
	}

	if (ret) {
		CurrentMameCheatContent.clear();
	}

	return ret;
}

static INT32 LoadIniContentFromZip(const char* DrvName, const char* zipFileName, std::vector<char>& iniContent) {
	TCHAR iniFileName[MAX_PATH] = "";
	sprintf(iniFileName, "%s.ini", DrvName);

	TCHAR zipCheatPath[MAX_PATH];
	sprintf(zipCheatPath, "%s%s", szAppCheatsPath, zipFileName);

	if (ZipOpen((char*)zipCheatPath) != 0) {
		ZipClose();
		return 1;
	}

	struct ZipEntry* pList = NULL;
	INT32 pnListCount = 0;

	if (ZipGetList(&pList, &pnListCount) != 0) {
		ZipClose();
		return 1;
	}

	INT32 ret = 1;

	for (int i = 0; i < pnListCount; i++) {
		if (strcmp(pList[i].szName, iniFileName) == 0) {
			void* dest = malloc(pList[i].nLen);
			if (dest == NULL) {
				break;
			}

			INT32 pnWrote = 0;
			if (ZipLoadFile((UINT8*)dest, pList[i].nLen, &pnWrote, i) == 0) {
				char* content = (char*)dest;
				content[pnWrote / sizeof(char)] = 0;

				iniContent.insert(iniContent.end(), content, content + pnWrote);

				free(dest);
				ret = 0;
			}
			break;
		}
	}

	for (int i = 0; i < pnListCount; i++) {
		free(pList[i].szName);
	}

	free(pList);

	ZipClose();

	return ret;
}

 //Extract matched INI in cheat.zip or 7z
static INT32 ExtractIniFromZip(const char* DrvName, const char* zipFileName, std::vector<char>& CurrentIniCheat) {

	if (LoadIniContentFromZip(DrvName, zipFileName, CurrentIniCheatContent) != 0) {
		return 1;
	}

	int depth = 0;
	bool processInclude = true;
	//max searching included files 5 depth
	while (processInclude && depth < 5) {
		processInclude = false;
		std::vector<char> newContent;
		const char* iniPtr = CurrentIniCheatContent.data();
		char szLine[1024];

		// Let's check each line of CurrentIniCheatContent
		// Looking for include file and hooking them to CurrentIniCheatContent
		while (*iniPtr) {
			char* s = szLine;
			while (*iniPtr && *iniPtr != '\n') {
				*s++ = *iniPtr++;
			}
			if (*iniPtr == '\n') {
				*s++ = *iniPtr++;
			}
			*s = '\0';

			char* t;
			if ((t = LabelCheck(szLine, "include")) != 0) {
				processInclude = true;
				char* szQuote = NULL;
				QuoteRead(&szQuote, NULL, t);

				if (szQuote) {
					std::vector<char> includedContent;

					if (LoadIniContentFromZip(szQuote, zipFileName, includedContent) == 0) {
						newContent.insert(newContent.end(), includedContent.begin(), includedContent.end());
						newContent.push_back('\n');
					}
				}
			} else {
#if defined(BUILD_WIN32)
				newContent.insert(newContent.end(), szLine, szLine + lstrlen(szLine));
#else
				newContent.insert(newContent.end(), szLine, szLine + strlen(szLine));
#endif
			}
		}

		CurrentIniCheatContent = newContent;
		depth++;
	}

	return 0;
}

INT32 ConfigCheatLoad() {
	TCHAR szFilename[MAX_PATH] = "";
	INT32 ret = 1;

	// During running game,while ConfigCheatLoad is called the second time or more,
	// Try to load cheat directly,skip unnecessary steps.
	// usedCheatType define:
	// 0:first ConfigCheatLoad() while launching game
	// 1:first ConfigCheatLoad() used MameDatCheat,we directly reload existing cache(DRV_NAME) from cheat.dat
	// 2:first ConfigCheatLoad() used MameDatCheat,we directly reload existing cache(DRV_PARENT) from cheat.dat
	// 3:first ConfigCheatLoad() used ini cheat in Zip/7Z,we directly reload existing cache from cheat.zip/7z
	// 4:first ConfigCheatLoad() used ini cheat in folder,we directly reload from <drvname>.ini
	// 5:first ConfigCheatLoad() used NebulaDatCheat in folder,we directly reload from <drvname>.dat
	// 6:first ConfigCheatLoad() no cheats found,we do nothing,never check again.
	switch (usedCheatType) {
		case 0:
			if (ConfigParseMAMEFile()) {
				ret = ExtractIniFromZip(BurnDrvGetText(DRV_NAME), "cheat", CurrentIniCheatContent);
				if (ret == 0) {
					// pszFilename only uses for cheaterror as string,not a file
					sprintf(szFilename, "%s%s.ini(cheat.zip/7z)", szAppCheatsPath, BurnDrvGetText(DRV_NAME));
					ret = ConfigParseFile(szFilename, &CurrentIniCheatContent);
				}
				if (ret > 0) {
					sprintf(szFilename, "%s%s.ini", szAppCheatsPath, BurnDrvGetText(DRV_NAME));
					ret = ConfigParseFile(szFilename,NULL);
					if (ret != 0) {
						sprintf(szFilename, "%s%s.dat", szAppCheatsPath, BurnDrvGetText(DRV_NAME));
						ret = ConfigParseNebulaFile(szFilename);
						if (ret != 0) {
							usedCheatType = 6;
						}
					}
				}
			}
			break;
		case 1:
			ret = ConfigParseMAMEFile_internal(BurnDrvGetText(DRV_NAME));
			break;
		case 2:
			ret = ConfigParseMAMEFile_internal(BurnDrvGetText(DRV_PARENT));
			break;
		case 3:
			// pszFilename only uses for cheaterror as string, not a file in this step
			sprintf(szFilename, "%s%s.ini(cheat.zip/7z)", szAppCheatsPath, BurnDrvGetText(DRV_NAME));
			ret = ConfigParseFile(szFilename, &CurrentIniCheatContent);
			break;
		case 4:
			sprintf(szFilename, "%s%s.ini", szAppCheatsPath, BurnDrvGetText(DRV_NAME));
			ret = ConfigParseFile(szFilename, NULL);
			break;
		case 5:
			sprintf(szFilename, "%s%s.dat", szAppCheatsPath, BurnDrvGetText(DRV_NAME));
			ret = ConfigParseNebulaFile(szFilename);
			break;
		default: //case 6 aswell
			ret = 1;
			break;
	}

	if (pCheatInfo) {
		INT32 nCurrentCheat = 0;
		while (CheatEnable(nCurrentCheat, -1) == 0) {
			nCurrentCheat++;
		}

		CheatUpdate();
	}

	return ret;
}