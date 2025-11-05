// Driver nvram module
#include "burner.h"

#ifdef __LIBRETRO__
#include <streams/file_stream.h>
#endif

static UINT8 *pNvramData;
static INT32 nTotalLen = 0;

static INT32 __cdecl StateLenAcb(struct BurnArea* pba)
{
	nTotalLen += pba->nLen;
	return 0;
}

static INT32 __cdecl NvramLoadAcb(struct BurnArea* pba)
{
	memcpy(pba->Data, pNvramData, pba->nLen);
	pNvramData += pba->nLen;
	return 0;
}

static INT32 __cdecl NvramSaveAcb(struct BurnArea* pba)
{
	memcpy(pNvramData, pba->Data, pba->nLen);
	pNvramData += pba->nLen;
	return 0;
}

static INT32 NvramInfo(int* pnLen)
{
	INT32 nMin = 0;
	nTotalLen = 0;
	BurnAcb = StateLenAcb;

	BurnAreaScan(ACB_NVRAM | ACB_READ, &nMin);

	*pnLen = nTotalLen;

	return 0;
}

INT32 BurnNvramLoad(TCHAR* szName)
{
	INT32 nLen = 0;
	char ReadHeader[8];

#ifdef __LIBRETRO__
	RFILE* rfp = filestream_open(szName, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
	if (rfp == NULL) {
		return 1;
	}
#else
	FILE* fp = _tfopen(szName, _T("rb"));
	if (fp == NULL) {
		return 1;
	}
#endif

	// abort the loading of older headered/compressed nvrams
	memset(ReadHeader, 0, 8);
#ifdef __LIBRETRO__
	filestream_read(rfp, ReadHeader, 8);
#else
	fread(ReadHeader, 1, 8, fp);
#endif
	if (memcmp(ReadHeader, "FB1 FS1 ", 8) == 0) {
#ifdef __LIBRETRO__
		filestream_close(rfp);
#else
		fclose(fp);
#endif
		return 1;
	}
#ifdef __LIBRETRO__
	filestream_rewind(rfp);
#else
	fseek(fp, 0L, SEEK_SET);
#endif

#ifdef __LIBRETRO__
	nLen = filestream_get_size(rfp);
#else
	fseek(fp, 0L, SEEK_END);
	nLen = ftell(fp);
	fseek(fp, 0L, SEEK_SET);
#endif

	UINT8 *data = (UINT8*)malloc(nLen);
	if (data == NULL) {
#ifdef __LIBRETRO__
		filestream_close(rfp);
#else
		fclose(fp);
#endif
		return 1;
	}
#ifdef __LIBRETRO__
	filestream_read(rfp, data, nLen);
	filestream_close(rfp);
#else
	fread(data, 1, nLen, fp);
	fclose(fp);
#endif

	BurnAcb = NvramLoadAcb;
	pNvramData = data;
	BurnAreaScan(ACB_NVRAM | ACB_WRITE, NULL);

	if (data) {
		free(data);
		data = NULL;
	}

	return 0;
}

INT32 BurnNvramSave(TCHAR* szName)
{
	INT32 nLen = 0;
	INT32 nRet = 0;

	NvramInfo(&nLen);

	if (nLen <= 0) {
		return 1;
	}

#ifdef __LIBRETRO__
	RFILE* rfp = filestream_open(szName, RETRO_VFS_FILE_ACCESS_WRITE, RETRO_VFS_FILE_ACCESS_HINT_NONE);
	if (rfp == NULL) {
		return 1;
	}
#else
	FILE* fp = _tfopen(szName, _T("wb"));
	if (fp == NULL) {
		return 1;
	}
#endif

	UINT8 *data = (UINT8*)malloc(nLen);
	if (data == NULL) {
#ifdef __LIBRETRO__
		filestream_close(rfp);
#else
		fclose(fp);
#endif
		return 1;
	}

	BurnAcb = NvramSaveAcb;
	pNvramData = data;
	BurnAreaScan(ACB_NVRAM | ACB_READ, NULL);

#ifdef __LIBRETRO__
	nRet = filestream_write(rfp, data, nLen);
#else
	nRet = fwrite(data, 1, nLen, fp);
#endif

#ifdef __LIBRETRO__
	filestream_close(rfp);
#else
	fclose(fp);
#endif

	if (data) {
		free(data);
		data = NULL;
	}

	if (nRet != nLen) {
		return 1;
	}

	return 0;
}
