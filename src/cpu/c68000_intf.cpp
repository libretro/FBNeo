// 680x0 (Sixty Eight K) Interface
// ARM32 version: Cyclone (EMU_C68K) for 68000 cpus, Musashi (EMU_M68K) for the others.

#include "burnint.h"
#include "m68000_intf.h"
#include "m68000_debug.h"

#define EMU_C68K

#ifdef EMU_M68K
INT32 nSekM68KContextSize[SEK_MAX];
INT8* SekM68KContext[SEK_MAX];
#endif

INT32 nSekCount = -1;							// Number of allocated 68000s
struct SekExt *SekExt[SEK_MAX] = { NULL, }, *pSekExt = NULL;

INT32 nSekActive = -1;								// The cpu which is currently being emulated
INT32 nSekCyclesTotal, nSekCyclesScanline, nSekCyclesSegment, nSekCyclesDone, nSekCyclesToDo;

INT32 nSekCPUType[SEK_MAX], nSekCycles[SEK_MAX], nSekIRQPending[SEK_MAX], nSekRESETLine[SEK_MAX], nSekHALT[SEK_MAX];
INT32 nSekVIRQPending[SEK_MAX][8];
INT32 nSekCyclesToDoCache[SEK_MAX], nSekm68k_ICount[SEK_MAX];
INT32 nSekCPUOffsetAddress[SEK_MAX];

static UINT32 nSekAddressMask[SEK_MAX], nSekAddressMaskActive;

// ----------------------------------------------------------------------------
// Cyclone state

#ifdef EMU_C68K
#include "Cyclone.h"

struct Cyclone c68k[SEK_MAX];
static bool bCycloneInited = false;

// Jump table with Mega Drive TAS (no write-back to memory), requires CYCLONE_FOR_GENESIS 2
static long c68k_jumptab_md[0x10000];
static bool bCycloneJumpTabMDInited = false;

static UINT8 nSekIsC68K[SEK_MAX];
#define SEK_ACTIVE_IS_C68K	((nSekActive >= 0) && nSekIsC68K[nSekActive])

// nC68KRunning: cpu in the innermost CycloneRun() (-1 = none)
// bC68KSynced: 0 = not in a handler, 1 = in a handler (m68k_ICount mirrors
// c68k[nC68KRunning].cycles), 2 = in a callback where the context cycle count is invalid
static INT32 nC68KRunning = -1;
static INT32 bC68KSynced = 0;
static INT32 nC68KInRun[SEK_MAX];
static INT32 nSekRunDepth = 0;

// Cyclone writes PC back when CycloneRun() exits, so PC changes requested while
// the cpu is running are deferred to the end of the current slice.
#define C68K_PENDING_RESET	0x01
#define C68K_PENDING_SETPC	0x02
#define C68K_PENDING_REBASE	0x04
static UINT32 nC68KPending[SEK_MAX];
static UINT32 nC68KPendingPC[SEK_MAX];

static UINT8 nC68KSleep[SEK_MAX];

static UINT32 c68k_virq_state[SEK_MAX];
static UINT8 c68k_irq_line[SEK_MAX];
static UINT8 c68k_nmi_edge[SEK_MAX];

// Cyclone takes level 7 whenever .irq is 7. Like Musashi, only take it on a rising
// edge (latched until acknowledged), or as a normal level irq when the mask is below 7.
static void C68KUpdateIrq(INT32 n)
{
	UINT32 level = c68k_irq_line[n];

	if (c68k_nmi_edge[n]) {
		level = 7;
	} else if (level == 7 && (c68k[n].srh & 7) == 7) {
		for (level = 6; level > 0; level--)
			if (c68k_virq_state[n] & (1 << level))
				break;
	}

	c68k[n].irq = level;
}

static void C68KSetIrqLine(INT32 n, UINT32 level)
{
	if (level == 7 && c68k_irq_line[n] != 7) c68k_nmi_edge[n] = 1;

	c68k_irq_line[n] = level;
	C68KUpdateIrq(n);
}

static void c68k_set_virq(UINT32 level, UINT32 active)
{
	UINT32 state = c68k_virq_state[nSekActive];
	UINT32 blevel;

	if (active)
		state |= 1 << level;
	else
		state &= ~(1 << level);
	c68k_virq_state[nSekActive] = state;

	for (blevel = 7; blevel > 0; blevel--)
		if (state & (1 << blevel))
			break;
	C68KSetIrqLine(nSekActive, blevel);
}

// Cyclone stores its cycle counter in the context before calling a handler
// (MEMHANDLERS_NEED_CYCLES) and reloads it afterwards (MEMHANDLERS_CHANGE_CYCLES).
// Mirroring it in m68k_ICount during the handler keeps SekTotalCycles() & co.
// from m68000_intf.h working unchanged.
#define C68K_HANDLER_ENTER																\
	const INT32 nC68KSync = (nC68KRunning >= 0 && bC68KSynced == 0) ? nC68KRunning : -1;	\
	if (nC68KSync >= 0) { m68k_ICount = c68k[nC68KSync].cycles; bC68KSynced = 1; }
#define C68K_HANDLER_LEAVE																\
	if (nC68KSync >= 0) { c68k[nC68KSync].cycles = m68k_ICount; bC68KSynced = 0; }

static inline bool C68KInHandler()
{
	return (nC68KRunning >= 0) && (nC68KRunning == nSekActive) && (bC68KSynced == 1);
}

// Ends the current CycloneRun() slice (not the SekRun() call). Only valid if C68KInHandler().
static void C68KForceExit()
{
	const INT32 nDone = nSekCyclesToDo - m68k_ICount;
	nSekCyclesTotal += nDone;
	nSekCyclesDone  += nDone;
	nSekCyclesToDo = m68k_ICount = -1;
}

#define C68K_CALL_READ(T, call)  { C68K_HANDLER_ENTER T r = call; C68K_HANDLER_LEAVE return r; }
#define C68K_CALL_WRITE(call)    { C68K_HANDLER_ENTER call; C68K_HANDLER_LEAVE }

#else

#define C68K_CALL_READ(T, call)  return call;
#define C68K_CALL_WRITE(call)    call;

#endif

cpu_core_config SekConfig =
{
	"68k",
	SekCPUPush, //SekOpen,
	SekCPUPop, //SekClose,
	SekCheatRead,
	SekWriteByteROM,
	SekGetActive,
	SekTotalCycles,
	SekNewFrame,
	SekIdle,
	SekSetIRQLine,
	SekRun,
	SekRunEnd,
	SekReset,
	SekScan,
	SekExit,
	0x1000000,
	1 // big endian
};

#if defined (FBNEO_DEBUG)

void (*SekDbgBreakpointHandlerRead)(UINT32, INT32);
void (*SekDbgBreakpointHandlerFetch)(UINT32, INT32);
void (*SekDbgBreakpointHandlerWrite)(UINT32, INT32);

UINT32 (*SekDbgFetchByteDisassembler)(UINT32);
UINT32 (*SekDbgFetchWordDisassembler)(UINT32);
UINT32 (*SekDbgFetchLongDisassembler)(UINT32);

static struct { UINT32 address; INT32 id; } BreakpointDataRead[9]  = { { 0, 0 }, };
static struct { UINT32 address; INT32 id; } BreakpointDataWrite[9] = { { 0, 0 }, };
static struct { UINT32 address; INT32 id; } BreakpointFetch[9] = { { 0, 0 }, };

#endif


#if defined (FBNEO_DEBUG)

inline static void CheckBreakpoint_R(UINT32 a, const UINT32 m)
{
	a &= m;

	for (INT32 i = 0; BreakpointDataRead[i].address; i++) {
		if ((BreakpointDataRead[i].address & m) == a) {


			SekDbgBreakpointHandlerRead(a, BreakpointDataRead[i].id);
			return;
		}
	}
}

inline static void CheckBreakpoint_W(UINT32 a, const UINT32 m)
{
	a &= m;

	for (INT32 i = 0; BreakpointDataWrite[i].address; i++) {
		if ((BreakpointDataWrite[i].address & m) == a) {


			SekDbgBreakpointHandlerWrite(a, BreakpointDataWrite[i].id);
			return;
		}
	}
}

inline static void CheckBreakpoint_PC(unsigned int /*pc*/)
{
	for (INT32 i = 0; BreakpointFetch[i].address; i++) {
		if (BreakpointFetch[i].address == (UINT32)SekGetPC(-1)) {


			SekDbgBreakpointHandlerFetch(SekGetPC(-1), BreakpointFetch[i].id);
			return;
		}
	}
}

inline static void SingleStep_PC(unsigned int /*pc*/)
{

	SekDbgBreakpointHandlerFetch(SekGetPC(-1), 0);
}

#endif

// ----------------------------------------------------------------------------
// Default memory access handlers

UINT8 __fastcall DefReadByte(UINT32) { return 0; }
void __fastcall DefWriteByte(UINT32, UINT8) { }

#define DEFWORDHANDLERS(i)																				\
	UINT16 __fastcall DefReadWord##i(UINT32 a) { SEK_DEF_READ_WORD(i, a) }				\
	void __fastcall DefWriteWord##i(UINT32 a, UINT16 d) { SEK_DEF_WRITE_WORD(i, a ,d) }
#define DEFLONGHANDLERS(i)																				\
	UINT32 __fastcall DefReadLong##i(UINT32 a) { SEK_DEF_READ_LONG(i, a) }					\
	void __fastcall DefWriteLong##i(UINT32 a, UINT32 d) { SEK_DEF_WRITE_LONG(i, a , d) }

DEFWORDHANDLERS(0)
DEFLONGHANDLERS(0)

#if SEK_MAXHANDLER >= 2
 DEFWORDHANDLERS(1)
 DEFLONGHANDLERS(1)
#endif

#if SEK_MAXHANDLER >= 3
 DEFWORDHANDLERS(2)
 DEFLONGHANDLERS(2)
#endif

#if SEK_MAXHANDLER >= 4
 DEFWORDHANDLERS(3)
 DEFLONGHANDLERS(3)
#endif

#if SEK_MAXHANDLER >= 5
 DEFWORDHANDLERS(4)
 DEFLONGHANDLERS(4)
#endif

#if SEK_MAXHANDLER >= 6
 DEFWORDHANDLERS(5)
 DEFLONGHANDLERS(5)
#endif

#if SEK_MAXHANDLER >= 7
 DEFWORDHANDLERS(6)
 DEFLONGHANDLERS(6)
#endif

#if SEK_MAXHANDLER >= 8
 DEFWORDHANDLERS(7)
 DEFLONGHANDLERS(7)
#endif

#if SEK_MAXHANDLER >= 9
 DEFWORDHANDLERS(8)
 DEFLONGHANDLERS(8)
#endif

#if SEK_MAXHANDLER >= 10
 DEFWORDHANDLERS(9)
 DEFLONGHANDLERS(9)
#endif

// ----------------------------------------------------------------------------
// Memory access functions

// Mapped Memory lookup (               for read)
#define FIND_R(x) pSekExt->MemMap[ x >> SEK_SHIFT]
// Mapped Memory lookup (+ SEK_WADD     for write)
#define FIND_W(x) pSekExt->MemMap[(x >> SEK_SHIFT) + SEK_WADD]
// Mapped Memory lookup (+ SEK_WADD * 2 for fetch)
#define FIND_F(x) pSekExt->MemMap[(x >> SEK_SHIFT) + SEK_WADD * 2]

// Normal memory access functions
inline static UINT8 ReadByte(UINT32 a)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;

//	bprintf(PRINT_NORMAL, _T("read8 0x%08X\n"), a);
	pr = FIND_R(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER) {
		a ^= 1;
		return pr[a & SEK_PAGEM];
	}
	C68K_CALL_READ(UINT8, pSekExt->ReadByte[(uintptr_t)pr](a))
}

inline static UINT8 FetchByte(UINT32 a)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;

//	bprintf(PRINT_NORMAL, _T("fetch8 0x%08X\n"), a);

	pr = FIND_F(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER) {
		a ^= 1;
		return pr[a & SEK_PAGEM];
	}
	C68K_CALL_READ(UINT8, pSekExt->ReadByte[(uintptr_t)pr](a))
}

inline static void WriteByte(UINT32 a, UINT8 d)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;
//	bprintf(PRINT_NORMAL, _T("write8 0x%08X\n"), a);

	pr = FIND_W(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER) {
		a ^= 1;
		pr[a & SEK_PAGEM] = (UINT8)d;
		return;
	}
	C68K_CALL_WRITE(pSekExt->WriteByte[(uintptr_t)pr](a, d))
}

inline static void WriteByteROM(UINT32 a, UINT8 d)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;
	// changed from FIND_R to allow for encrypted games (fd1094 etc) to work -dink apr. 23, 2021
	// (on non-encrypted games, Fetch is mapped to Read)
	pr = FIND_F(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER) {
		a ^= 1;
		pr[a & SEK_PAGEM] = (UINT8)d;
		return;
	}
	C68K_CALL_WRITE(pSekExt->WriteByte[(uintptr_t)pr](a, d))
}

inline static UINT16 ReadWord(UINT32 a)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;

//	bprintf(PRINT_NORMAL, _T("read16 0x%08X\n"), a);
	pr = FIND_R(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER)
	{
		if (a & 1)
		{
			return (ReadByte(a + 0) * 256) + ReadByte(a + 1);
		}
		else
		{
			return BURN_ENDIAN_SWAP_INT16(*((UINT16*)(pr + (a & SEK_PAGEM))));
		}
	}

	C68K_CALL_READ(UINT16, pSekExt->ReadWord[(uintptr_t)pr](a))
}

inline static UINT16 FetchWord(UINT32 a)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;

//	bprintf(PRINT_NORMAL, _T("fetch16 0x%08X\n"), a);

	pr = FIND_F(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER) {
		if (a & 1)
		{
			return (ReadByte(a + 0) * 256) + ReadByte(a + 1);
		}
		else
		{
			return BURN_ENDIAN_SWAP_INT16(*((UINT16*)(pr + (a & SEK_PAGEM))));
		}
	}

	C68K_CALL_READ(UINT16, pSekExt->ReadWord[(uintptr_t)pr](a))
}

inline static void WriteWord(UINT32 a, UINT16 d)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;

//	bprintf(PRINT_NORMAL, _T("write16 0x%08X\n"), a);
	pr = FIND_W(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER)
	{
		if (a & 1)
		{
		//	bprintf(PRINT_NORMAL, _T("write16 0x%08X\n"), a);

			WriteByte(a + 0, d / 0x100);
			WriteByte(a + 1, d);

			return;
		}
		else
		{
			*((UINT16*)(pr + (a & SEK_PAGEM))) = (UINT16)BURN_ENDIAN_SWAP_INT16(d);
			return;
		}
	}

	C68K_CALL_WRITE(pSekExt->WriteWord[(uintptr_t)pr](a, d))
}

inline static void WriteWordROM(UINT32 a, UINT16 d)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;
	pr = FIND_R(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER) {
		*((UINT16*)(pr + (a & SEK_PAGEM))) = (UINT16)BURN_ENDIAN_SWAP_INT16(d);
		return;
	}
	C68K_CALL_WRITE(pSekExt->WriteWord[(uintptr_t)pr](a, d))
}

// [x] byte #
// be [3210] -> (r >> 16) | (r << 16) -> [1032] -> UINT32(le) = -> [0123]
// mem [0123]

inline static UINT32 ReadLong(UINT32 a)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;

//	bprintf(PRINT_NORMAL, _T("read32 0x%08X\n"), a);
	pr = FIND_R(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER)
	{
		UINT32 r = 0;

		if (a & nSekCPUOffsetAddress[nSekActive])
		{
			r  = ReadByte((a + 0)) * 0x1000000;
			r += ReadByte((a + 1)) * 0x10000;
			r += ReadByte((a + 2)) * 0x100;
			r += ReadByte((a + 3));

			return r;
		}
		else
		{
			r = *((UINT32*)(pr + (a & SEK_PAGEM)));
			r = (r >> 16) | (r << 16);

			return BURN_ENDIAN_SWAP_INT32(r);
		}
	}

	C68K_CALL_READ(UINT32, pSekExt->ReadLong[(uintptr_t)pr](a))
}

inline static UINT32 FetchLong(UINT32 a)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;

//	bprintf(PRINT_NORMAL, _T("fetch32 0x%08X\n"), a);

	//if (a&3) bprintf(0, _T("fetchlong offset-read @ %x\n"), a);

	pr = FIND_F(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER) {
		UINT32 r = 0;

		if (a & nSekCPUOffsetAddress[nSekActive])
		{
			r  = ReadByte((a + 0)) * 0x1000000;
			r += ReadByte((a + 1)) * 0x10000;
			r += ReadByte((a + 2)) * 0x100;
			r += ReadByte((a + 3));

			return r;
		}
		else
		{
			r = *((UINT32*)(pr + (a & SEK_PAGEM)));
			r = (r >> 16) | (r << 16);

			return BURN_ENDIAN_SWAP_INT32(r);
		}
	}
	C68K_CALL_READ(UINT32, pSekExt->ReadLong[(uintptr_t)pr](a))
}

inline static void WriteLong(UINT32 a, UINT32 d)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;

//	bprintf(PRINT_NORMAL, _T("write32 0x%08X\n"), a);
	pr = FIND_W(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER)
	{
		if (a & nSekCPUOffsetAddress[nSekActive])
		{
		//	bprintf(PRINT_NORMAL, _T("write32 0x%08X 0x%8.8x\n"), a,d);

			WriteByte((a + 0), d / 0x1000000);
			WriteByte((a + 1), d / 0x10000);
			WriteByte((a + 2), d / 0x100);
			WriteByte((a + 3), d);

			return;
		}
		else
		{
			d = (d >> 16) | (d << 16);
			*((UINT32*)(pr + (a & SEK_PAGEM))) = BURN_ENDIAN_SWAP_INT32(d);

			return;
		}
	}
	C68K_CALL_WRITE(pSekExt->WriteLong[(uintptr_t)pr](a, d))
}

inline static void WriteLongROM(UINT32 a, UINT32 d)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;
	pr = FIND_R(a);
	if ((uintptr_t)pr >= SEK_MAXHANDLER) {
		d = (d >> 16) | (d << 16);
		*((UINT32*)(pr + (a & SEK_PAGEM))) = BURN_ENDIAN_SWAP_INT32(d);
		return;
	}
	C68K_CALL_WRITE(pSekExt->WriteLong[(uintptr_t)pr](a, d))
}

#if defined (FBNEO_DEBUG)

// Breakpoint checking memory access functions
UINT8 __fastcall ReadByteBP(UINT32 a)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;

	pr = FIND_R(a);

	CheckBreakpoint_R(a, ~0);

	if ((uintptr_t)pr >= SEK_MAXHANDLER) {
		a ^= 1;
		return pr[a & SEK_PAGEM];
	}
	return pSekExt->ReadByte[(uintptr_t)pr](a);
}

void __fastcall WriteByteBP(UINT32 a, UINT8 d)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;

	pr = FIND_W(a);

	CheckBreakpoint_W(a, ~0);

	if ((uintptr_t)pr >= SEK_MAXHANDLER) {
		a ^= 1;
		pr[a & SEK_PAGEM] = (UINT8)d;
		return;
	}
	pSekExt->WriteByte[(uintptr_t)pr](a, d);
}

UINT16 __fastcall ReadWordBP(UINT32 a)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;

	pr = FIND_R(a);

	CheckBreakpoint_R(a, ~1);

	if ((uintptr_t)pr >= SEK_MAXHANDLER) {
		return *((UINT16*)(pr + (a & SEK_PAGEM)));
	}
	return pSekExt->ReadWord[(uintptr_t)pr](a);
}

void __fastcall WriteWordBP(UINT32 a, UINT16 d)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;

	pr = FIND_W(a);

	CheckBreakpoint_W(a, ~1);

	if ((uintptr_t)pr >= SEK_MAXHANDLER) {
		*((UINT16*)(pr + (a & SEK_PAGEM))) = (UINT16)d;
		return;
	}
	pSekExt->WriteWord[(uintptr_t)pr](a, d);
}

UINT32 __fastcall ReadLongBP(UINT32 a)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;

	pr = FIND_R(a);

	CheckBreakpoint_R(a, ~1);

	if ((uintptr_t)pr >= SEK_MAXHANDLER) {
		UINT32 r = *((UINT32*)(pr + (a & SEK_PAGEM)));
		r = (r >> 16) | (r << 16);
		return r;
	}
	return pSekExt->ReadLong[(uintptr_t)pr](a);
}

void __fastcall WriteLongBP(UINT32 a, UINT32 d)
{
	UINT8* pr;

	a &= nSekAddressMaskActive;

	pr = FIND_W(a);

	CheckBreakpoint_W(a, ~1);

	if ((uintptr_t)pr >= SEK_MAXHANDLER) {
		d = (d >> 16) | (d << 16);
		*((UINT32*)(pr + (a & SEK_PAGEM))) = d;
		return;
	}
	pSekExt->WriteLong[(uintptr_t)pr](a, d);
}

#endif

// ----------------------------------------------------------------------------

extern "C" {
 UINT32 mame_debug = 0, cur_mrhard = 0, m68k_illegal_opcode = 0, illegal_op = 0, illegal_pc = 0, opcode_entry = 0;
}

#ifdef EMU_M68K
extern "C" {
UINT32 __fastcall M68KReadByte(UINT32 a) { return (UINT32)ReadByte(a); }
UINT32 __fastcall M68KReadWord(UINT32 a) { return (UINT32)ReadWord(a); }
UINT32 __fastcall M68KReadLong(UINT32 a) { return               ReadLong(a); }

UINT32 __fastcall M68KFetchByte(UINT32 a) { return (UINT32)FetchByte(a); }
UINT32 __fastcall M68KFetchWord(UINT32 a) { return (UINT32)FetchWord(a); }
UINT32 __fastcall M68KFetchLong(UINT32 a) { return               FetchLong(a); }

#ifdef FBNEO_DEBUG
UINT32 __fastcall M68KReadByteBP(UINT32 a) { return (UINT32)ReadByteBP(a); }
UINT32 __fastcall M68KReadWordBP(UINT32 a) { return (UINT32)ReadWordBP(a); }
UINT32 __fastcall M68KReadLongBP(UINT32 a) { return               ReadLongBP(a); }

void __fastcall M68KWriteByteBP(UINT32 a, UINT32 d) { WriteByteBP(a, d); }
void __fastcall M68KWriteWordBP(UINT32 a, UINT32 d) { WriteWordBP(a, d); }
void __fastcall M68KWriteLongBP(UINT32 a, UINT32 d) { WriteLongBP(a, d); }

void M68KCheckBreakpoint(unsigned int pc) { CheckBreakpoint_PC(pc); }
void M68KSingleStep(unsigned int pc) { SingleStep_PC(pc); }

UINT32 (__fastcall *M68KReadByteDebug)(UINT32);
UINT32 (__fastcall *M68KReadWordDebug)(UINT32);
UINT32 (__fastcall *M68KReadLongDebug)(UINT32);

void (__fastcall *M68KWriteByteDebug)(UINT32, UINT32);
void (__fastcall *M68KWriteWordDebug)(UINT32, UINT32);
void (__fastcall *M68KWriteLongDebug)(UINT32, UINT32);
#endif

void __fastcall M68KWriteByte(UINT32 a, UINT32 d) { WriteByte(a, d); }
void __fastcall M68KWriteWord(UINT32 a, UINT32 d) { WriteWord(a, d); }
void __fastcall M68KWriteLong(UINT32 a, UINT32 d) { WriteLong(a, d); }
}
#endif


// ----------------------------------------------------------------------------
// Memory accesses (non-emu specific)

UINT32 SekReadByte(UINT32 a) { return (UINT32)ReadByte(a); }
UINT32 SekReadWord(UINT32 a) { return (UINT32)ReadWord(a); }
UINT32 SekReadLong(UINT32 a) { return ReadLong(a); }

UINT32 SekFetchByte(UINT32 a) { return (UINT32)FetchByte(a); }
UINT32 SekFetchWord(UINT32 a) { return (UINT32)FetchWord(a); }
UINT32 SekFetchLong(UINT32 a) { return FetchLong(a); }

void SekWriteByte(UINT32 a, UINT8 d) { WriteByte(a, d); }
void SekWriteWord(UINT32 a, UINT16 d) { WriteWord(a, d); }
void SekWriteLong(UINT32 a, UINT32 d) { WriteLong(a, d); }

void SekWriteByteROM(UINT32 a, UINT8 d) { WriteByteROM(a, d); }
void SekWriteWordROM(UINT32 a, UINT16 d) { WriteWordROM(a, d); }
void SekWriteLongROM(UINT32 a, UINT32 d) { WriteLongROM(a, d); }

// ----------------------------------------------------------------------------
// Callbacks for Cyclone

#ifdef EMU_C68K
extern "C" {
UINT32 __fastcall m68k_read8(UINT32 a) { return (UINT32)ReadByte(a); }
UINT32 __fastcall m68k_read16(UINT32 a) { return (UINT32)ReadWord(a); }
UINT32 __fastcall m68k_read32(UINT32 a) { return               ReadLong(a); }

UINT32 __fastcall m68k_fetch8(UINT32 a) { return (UINT32)FetchByte(a); }
UINT32 __fastcall m68k_fetch16(UINT32 a) { return (UINT32)FetchWord(a); }
UINT32 __fastcall m68k_fetch32(UINT32 a) { return               FetchLong(a); }

void __fastcall m68k_write8(UINT32 a, UINT32 d) { WriteByte(a, d); }
void __fastcall m68k_write16(UINT32 a, UINT32 d) { WriteWord(a, d); }
void __fastcall m68k_write32(UINT32 a, UINT32 d) { WriteLong(a, d); }
}

// Filled with BRA.S *, used when the pc is not in directly mapped fetch memory
static UINT16 c68k_trap_page[SEK_PAGE_SIZE / 2 + 8];
static INT32 nC68KTrapLogCount = 0;

extern "C" UINT32 m68k_checkpc(UINT32 pc)
{
	struct Cyclone *pCy = &c68k[nSekActive];

	pc -= pCy->membase;										// Get real pc
	pc &= nSekAddressMaskActive;

	UINT8 *pr = FIND_F(pc);

	if ((uintptr_t)pr < SEK_MAXHANDLER) {
		// Cyclone fetches opcodes directly from memory, it can't run code through fetch handlers
		if (nC68KTrapLogCount < 8) {
			nC68KTrapLogCount++;
			bprintf(PRINT_ERROR, _T("Cyclone: cpu #%d pc %06x is in a fetch handler area (%d), use Musashi for this game\n"), nSekActive, pc, (INT32)(uintptr_t)pr);
		}

		if (c68k_trap_page[0] != 0x60fe) {
			for (UINT32 i = 0; i < sizeof(c68k_trap_page) / sizeof(c68k_trap_page[0]); i++) {
				c68k_trap_page[i] = 0x60fe;					// BRA.S *
			}
		}

		pr = (UINT8*)c68k_trap_page;
	}

	pCy->membase = (uintptr_t)pr - (pc & ~SEK_PAGEM);

	return pCy->membase + pc;
}

// Requires Cyclone built with INT_ACK_NEEDS_STUFF and INT_ACK_CHANGES_CYCLES
extern "C" INT32 C68KIRQAcknowledge(INT32 nIRQ)
{
	C68K_HANDLER_ENTER

	INT32 nRet = CYCLONE_INT_ACK_AUTOVECTOR;

	if (nIRQ == 7) c68k_nmi_edge[nSekActive] = 0;

	if (nSekIRQPending[nSekActive] & SEK_IRQSTATUS_AUTO) {
		C68KSetIrqLine(nSekActive, 0);
		nSekIRQPending[nSekActive] = 0;
	}

	if (nSekVIRQPending[nSekActive][nIRQ] & SEK_IRQSTATUS_VAUTO) {
		c68k_set_virq(nIRQ, 0);
		nSekVIRQPending[nSekActive][nIRQ] = 0;
	}

	C68KUpdateIrq(nSekActive);

	if (pSekExt->IrqCallback) {
		nRet = pSekExt->IrqCallback(nIRQ);
	}

	C68K_HANDLER_LEAVE

	return nRet;
}

extern "C" void C68KResetCallback()
{
	C68K_HANDLER_ENTER

	if (pSekExt->ResetCallback) {
		pSekExt->ResetCallback();
	}

	C68K_HANDLER_LEAVE
}

// Cyclone has no RTE, CMPILD or TAS callbacks (TAS always writes back, like the
// default M68KTASCallback()).

static void C68KDoReset(INT32 n, bool bClearIrq)
{
	struct Cyclone *pCy = &c68k[n];

	pCy->state_flags = 0;
	pCy->srh         = 0x27;
	pCy->osp         = 0;
	if (bClearIrq) {
		c68k_irq_line[n] = 0;
		c68k_virq_state[n] = 0;
	}
	C68KUpdateIrq(n);
	pCy->a[7]        = FetchLong(0);
	pCy->membase     = 0;
	pCy->pc          = m68k_checkpc(FetchLong(4));
}

static void C68KSetPC(INT32 n, UINT32 nPC)
{
	if (nC68KInRun[n]) {
		nC68KPending[n] |= C68K_PENDING_SETPC;
		nC68KPendingPC[n] = nPC;
		if (C68KInHandler()) C68KForceExit();
		return;
	}

	c68k[n].pc = m68k_checkpc(c68k[n].membase + nPC);
}

static void C68KProcessPending(INT32 n)
{
	if (nC68KPending[n] == 0) return;

	const INT32 nPrevSynced = bC68KSynced;
	bC68KSynced = 2;

	if (nC68KPending[n] & C68K_PENDING_RESET) {
		C68KDoReset(n, false);
	} else if (nC68KPending[n] & C68K_PENDING_SETPC) {
		c68k[n].pc = m68k_checkpc(c68k[n].membase + nC68KPendingPC[n]);
	} else if (nC68KPending[n] & C68K_PENDING_REBASE) {
		c68k[n].pc = m68k_checkpc(c68k[n].pc);
	}

	nC68KPending[n] = 0;
	bC68KSynced = nPrevSynced;
}

// Without this, Cyclone keeps running from the old buffer until the next jump
static void C68KFetchMapChanged()
{
	const INT32 n = nSekActive;

	if (n < 0 || !nSekIsC68K[n] || c68k[n].pc == 0) return;

	if (nC68KInRun[n]) {
		nC68KPending[n] |= C68K_PENDING_REBASE;
		if (C68KInHandler()) C68KForceExit();
		return;
	}

	const INT32 nPrevSynced = bC68KSynced;
	bC68KSynced = 2;
	c68k[n].pc = m68k_checkpc(c68k[n].pc);
	bC68KSynced = nPrevSynced;
}

static INT32 SekInitCPUC68K(INT32 nCount, INT32 nCPUType)
{
#if defined (FBNEO_DEBUG)
	bprintf(PRINT_NORMAL, _T("EMU_C68K: SekInitCPUC68K(%i, %x)\n"), nCount, nCPUType);
#endif

	if (nCPUType != 0x68000) {
		return 1;
	}

	nSekCPUType[nCount] = nCPUType;

	nSekCPUOffsetAddress[nCount] = 1;

	if (!bCycloneInited) {
		CycloneInit();
		CycloneSetRealTAS(1);
		bCycloneInited = true;
	}

	memset(&c68k[nCount], 0, sizeof(struct Cyclone));
	c68k[nCount].jumptab       = (uintptr_t)CycloneJumpTab;
	c68k[nCount].checkpc       = m68k_checkpc;
	c68k[nCount].IrqCallback   = C68KIRQAcknowledge;
	c68k[nCount].ResetCallback = C68KResetCallback;

	c68k_virq_state[nCount] = 0;
	c68k_irq_line[nCount]   = 0;
	c68k_nmi_edge[nCount]   = 0;
	nC68KInRun[nCount]      = 0;
	nC68KPending[nCount]    = 0;
	nC68KPendingPC[nCount]  = 0;
	nC68KSleep[nCount]      = 0;

	return 0;
}

static INT32 SekRunC68K(const INT32 nCycles)
{
	const INT32 n = nSekActive;
	struct Cyclone *pCy = &c68k[n];

	// SekRun() can be re-entered from a handler, preserve the caller's loop state
	const INT32 nPrevRunning = nC68KRunning, nPrevSynced = bC68KSynced;
	const INT32 nPrevDone = nSekCyclesDone, nPrevSegment = nSekCyclesSegment;
	const bool bNested = (nSekRunDepth > 1);

	nSekCyclesDone = 0;
	nSekCyclesSegment = nCycles;

	C68KProcessPending(n);

	if (nSekRESETLine[n] || nSekHALT[n] || nC68KSleep[n])
	{
		// idle when RESET high, halted, or sleeping until an interrupt (SekBurnUntilInt)
		nSekCyclesTotal += nCycles;
	}
	else
	{
		nC68KRunning = n;
		bC68KSynced = 0;
		nC68KInRun[n]++;

		while (nSekCyclesDone < nSekCyclesSegment) {
			pCy->cycles = m68k_ICount = nSekCyclesToDo = nSekCyclesSegment - nSekCyclesDone;

			C68KUpdateIrq(n);
			CycloneRun(pCy);

			const INT32 nDone = nSekCyclesToDo - pCy->cycles;
			nSekCyclesDone  += nDone;
			nSekCyclesTotal += nDone;

			C68KProcessPending(n);
		}

		nSekCyclesSegment = nSekCyclesDone;

		nC68KInRun[n]--;
		nC68KRunning = nPrevRunning;
		bC68KSynced = nPrevSynced;
	}

	const INT32 nRet = nSekCyclesSegment;

	nSekCyclesToDo = m68k_ICount = pCy->cycles = 0;

	if (bNested) {
		nSekCyclesDone = nPrevDone;
		nSekCyclesSegment = nPrevSegment;
	} else {
		nSekCyclesDone = 0;
	}

	return nRet;
}
#endif


// ----------------------------------------------------------------------------
// Callbacks for Musashi

#ifdef EMU_M68K
extern "C" INT32 M68KIRQAcknowledge(INT32 nIRQ)
{
	if (nSekIRQPending[nSekActive] & SEK_IRQSTATUS_AUTO) {
		m68k_set_irq(0);
		nSekIRQPending[nSekActive] = 0;
	}

	if (nSekVIRQPending[nSekActive][nIRQ] & SEK_IRQSTATUS_VAUTO) {
		m68k_set_virq(nIRQ, 0);
		nSekVIRQPending[nSekActive][nIRQ] = 0;
	}
	
	if (pSekExt->IrqCallback) {
		return pSekExt->IrqCallback(nIRQ);
	}

	return M68K_INT_ACK_AUTOVECTOR;
}

extern "C" void M68KResetCallback()
{
	if (pSekExt->ResetCallback) {
		pSekExt->ResetCallback();
	}
}

extern "C" void M68KRTECallback()
{
	if (pSekExt->RTECallback) {
		pSekExt->RTECallback();
	}
}

extern "C" void M68KcmpildCallback(UINT32 val, INT32 reg)
{
	if (pSekExt->CmpCallback) {
		pSekExt->CmpCallback(val, reg);
	}
}

extern "C" INT32 M68KTASCallback()
{
	if (pSekExt->TASCallback) {
		return pSekExt->TASCallback();
	}
	
	return 1; // enable by default
}
#endif

// ## SekCPUPush() / SekCPUPop() ## internal helpers for sending signals to other 68k's
struct m68kpstack {
	INT32 nHostCPU;
	INT32 nPushedCPU;
};
#define MAX_PSTACK 20

static m68kpstack pstack[MAX_PSTACK];
static INT32 pstacknum = 0;

INT32 SekCPUGetStackNum()
{
	return pstacknum;
}

void SekCPUPush(INT32 nCPU)
{
	m68kpstack *p = &pstack[pstacknum++];

	if (pstacknum + 1 >= MAX_PSTACK) {
		bprintf(0, _T("SekCPUPush(): out of stack!  Possible infinite recursion?  Crash pending..\n"));
	}

	p->nPushedCPU = nCPU;

	p->nHostCPU = SekGetActive();

	if (p->nHostCPU != p->nPushedCPU) {
		if (p->nHostCPU != -1) SekClose();
		SekOpen(p->nPushedCPU);
	}
}

void SekCPUPop()
{
	m68kpstack *p = &pstack[--pstacknum];

	if (p->nHostCPU != p->nPushedCPU) {
		SekClose();
		if (p->nHostCPU != -1) SekOpen(p->nHostCPU);
	}
}

// ----------------------------------------------------------------------------
// Initialisation/exit/reset


#ifdef EMU_M68K
static INT32 SekInitCPUM68K(INT32 nCount, INT32 nCPUType)
{
	nSekCPUType[nCount] = nCPUType;

	nSekCPUOffsetAddress[nCount] = 1; // 3 for 020!

	switch (nCPUType) {
		case 0x68000:
			m68k_set_cpu_type(M68K_CPU_TYPE_68000);
			break;
		case 0x68010:
			m68k_set_cpu_type(M68K_CPU_TYPE_68010);
			break;
		case 0x68EC020:
			m68k_set_cpu_type(M68K_CPU_TYPE_68EC020);
			nSekCPUOffsetAddress[nCount] = 3;
			break;
		default:
			return 1;
	}

	nSekM68KContextSize[nCount] = m68k_context_size();
	SekM68KContext[nCount] = (INT8*)malloc(nSekM68KContextSize[nCount]);
	if (SekM68KContext[nCount] == NULL) {
		return 1;
	}
	memset(SekM68KContext[nCount], 0, nSekM68KContextSize[nCount]);
	m68k_get_context(SekM68KContext[nCount]);

	return 0;
}
#endif

void SekNewFrame()
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekNewFrame called without init\n"));
#endif

	for (INT32 i = 0; i <= nSekCount; i++) {
		nSekCycles[i] = 0;
		nSekCyclesToDoCache[i] = 0;
		nSekm68k_ICount[i] = 0;
#ifdef EMU_C68K
		c68k[i].cycles = 0;
#endif
	}

	nSekCyclesToDo = m68k_ICount = 0;
	nSekCyclesTotal = 0;
}

void SekCyclesBurnRun(INT32 nCycles)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetCyclesBurnRun called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetCyclesBurnRun called when no CPU open\n"));
#endif
	m68k_ICount -= nCycles;
}

void SekSetCyclesScanline(INT32 nCycles)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetCyclesScanline called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetCyclesScanline called when no CPU open\n"));
#endif

	nSekCyclesScanline = nCycles;
}

UINT8 SekCheatRead(UINT32 a)
{
	return SekReadByte(a);
}


INT32 SekInit(INT32 nCount, INT32 nCPUType)
{
	DebugCPU_SekInitted = 1;
	
	struct SekExt* ps = NULL;


	if (nSekActive >= 0) {
		SekClose();
		nSekActive = -1;
	}

	if (nCount > nSekCount) {
		nSekCount = nCount;
	}

	// Allocate cpu extenal data (memory map etc)
	SekExt[nCount] = (struct SekExt*)malloc(sizeof(struct SekExt));
	if (SekExt[nCount] == NULL) {
		SekExit();
		return 1;
	}
	memset(SekExt[nCount], 0, sizeof(struct SekExt));

	// Put in default memory handlers
	ps = SekExt[nCount];

	for (INT32 j = 0; j < SEK_MAXHANDLER; j++) {
		ps->ReadByte[j]  = DefReadByte;
		ps->WriteByte[j] = DefWriteByte;
	}

	ps->ReadWord[0]  = DefReadWord0;
	ps->WriteWord[0] = DefWriteWord0;
	ps->ReadLong[0]  = DefReadLong0;
	ps->WriteLong[0] = DefWriteLong0;

#if SEK_MAXHANDLER >= 2
	ps->ReadWord[1]  = DefReadWord1;
	ps->WriteWord[1] = DefWriteWord1;
	ps->ReadLong[1]  = DefReadLong1;
	ps->WriteLong[1] = DefWriteLong1;
#endif

#if SEK_MAXHANDLER >= 3
	ps->ReadWord[2]  = DefReadWord2;
	ps->WriteWord[2] = DefWriteWord2;
	ps->ReadLong[2]  = DefReadLong2;
	ps->WriteLong[2] = DefWriteLong2;
#endif

#if SEK_MAXHANDLER >= 4
	ps->ReadWord[3]  = DefReadWord3;
	ps->WriteWord[3] = DefWriteWord3;
	ps->ReadLong[3]  = DefReadLong3;
	ps->WriteLong[3] = DefWriteLong3;
#endif

#if SEK_MAXHANDLER >= 5
	ps->ReadWord[4]  = DefReadWord4;
	ps->WriteWord[4] = DefWriteWord4;
	ps->ReadLong[4]  = DefReadLong4;
	ps->WriteLong[4] = DefWriteLong4;
#endif

#if SEK_MAXHANDLER >= 6
	ps->ReadWord[5]  = DefReadWord5;
	ps->WriteWord[5] = DefWriteWord5;
	ps->ReadLong[5]  = DefReadLong5;
	ps->WriteLong[5] = DefWriteLong5;
#endif

#if SEK_MAXHANDLER >= 7
	ps->ReadWord[6]  = DefReadWord6;
	ps->WriteWord[6] = DefWriteWord6;
	ps->ReadLong[6]  = DefReadLong6;
	ps->WriteLong[6] = DefWriteLong6;
#endif

#if SEK_MAXHANDLER >= 8
	ps->ReadWord[7]  = DefReadWord7;
	ps->WriteWord[7] = DefWriteWord7;
	ps->ReadLong[7]  = DefReadLong7;
	ps->WriteLong[7] = DefWriteLong7;
#endif

#if SEK_MAXHANDLER >= 9
	ps->ReadWord[8]  = DefReadWord8;
	ps->WriteWord[8] = DefWriteWord8;
	ps->ReadLong[8]  = DefReadLong8;
	ps->WriteLong[8] = DefWriteLong8;
#endif

#if SEK_MAXHANDLER >= 10
	ps->ReadWord[9]  = DefReadWord9;
	ps->WriteWord[9] = DefWriteWord9;
	ps->ReadLong[9]  = DefReadLong9;
	ps->WriteLong[9] = DefWriteLong9;
#endif

#if SEK_MAXHANDLER >= 11
	for (int j = 10; j < SEK_MAXHANDLER; j++) {
		ps->ReadWord[j]  = DefReadWord0;
		ps->WriteWord[j] = DefWriteWord0;
		ps->ReadLong[j]  = DefReadLong0;
		ps->WriteLong[j] = DefWriteLong0;
	}
#endif

	// Map the normal memory handlers
	SekDbgDisableBreakpoints();

#ifdef EMU_C68K
	nSekIsC68K[nCount] = ((nSekCpuCore == SEK_CORE_C68K) && (nCPUType == 0x68000)) ? 1 : 0;

	if (nSekIsC68K[nCount]) {
		if (SekInitCPUC68K(nCount, nCPUType)) {
			SekExit();
			return 1;
		}
	} else
#endif
	{
#ifdef EMU_M68K
		m68k_init();
		if (SekInitCPUM68K(nCount, nCPUType)) {
			SekExit();
			return 1;
		}
#else
		return 1;
#endif
	}


	nSekAddressMask[nCount] = 0xffffff;

	nSekCycles[nCount] = 0;
	nSekCyclesToDoCache[nCount] = 0;
	nSekm68k_ICount[nCount] = 0;

	nSekIRQPending[nCount] = 0;
	for (INT32 i = 0; i < 8; i++) {
		nSekVIRQPending[nCount][i] = 0;
	}
	nSekRESETLine[nCount] = 0;
	nSekHALT[nCount] = 0;

	nSekCyclesTotal = 0;
	nSekCyclesScanline = 0;

	CpuCheatRegister(nCount, &SekConfig);

	pstacknum = 0;

	return 0;
}


#ifdef EMU_M68K
static void SekCPUExitM68K(INT32 i)
{
		if(SekM68KContext[i]) {
			free(SekM68KContext[i]);
			SekM68KContext[i] = NULL;
		}
}
#endif


void SekExit()
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekExit called without init\n"));
#endif

	if (!DebugCPU_SekInitted) return;

	// Deallocate cpu extenal data (memory map etc)
	for (INT32 i = 0; i <= nSekCount; i++) {

#ifdef EMU_C68K
		if (nSekIsC68K[i]) {
			nSekIsC68K[i] = 0;
			c68k_virq_state[i] = 0;
		} else
#endif
		{
#ifdef EMU_M68K
			SekCPUExitM68K(i);
#endif
		}

		// Deallocate other context data
		if (SekExt[i]) {
			free(SekExt[i]);
			SekExt[i] = NULL;
		}

		nSekCPUOffsetAddress[i] = 0;
	}

	pSekExt = NULL;

	nSekActive = -1;
	nSekCount = -1;

#ifdef EMU_C68K
	nC68KRunning = -1;
	bC68KSynced = 0;
	nSekRunDepth = 0;
#endif

	DebugCPU_SekInitted = 0;
}

void SekReset()
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekReset called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekReset called when no CPU open\n"));
#endif

#ifdef EMU_C68K
	if (SEK_ACTIVE_IS_C68K) {
		const INT32 n = nSekActive;

		c68k_irq_line[n] = 0;
		c68k_virq_state[n] = 0;
		C68KUpdateIrq(n);

		if (nC68KInRun[n]) {
			// deferred, and ends the slice like SET_CYCLES(0) in m68k_pulse_reset()
			nC68KPending[n] |= C68K_PENDING_RESET;
			if (C68KInHandler()) m68k_ICount = 0;
		} else {
			C68KDoReset(n, true);
		}
	} else
#endif
	{
#ifdef EMU_M68K
		m68k_pulse_reset();
#endif
	}

	for (INT32 i = 0; i < 8; i++) {
		nSekVIRQPending[nSekActive][i] = 0;
	}
}

void SekReset(INT32 nCPU)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekReset called without init\n"));
#endif

	SekCPUPush(nCPU);

	SekReset();

	SekCPUPop();
}
// ----------------------------------------------------------------------------
// Control the active CPU

// Open a CPU
void SekOpen(const INT32 i)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekOpen called without init\n"));
	if (i > nSekCount) bprintf(PRINT_ERROR, _T("SekOpen called with invalid index %x\n"), i);
	if (nSekActive != -1) bprintf(PRINT_ERROR, _T("SekOpen called when CPU already open (%x) with index %x\n"), nSekActive, i);
#endif

	if (i != nSekActive) {
		nSekActive = i;

		pSekExt = SekExt[nSekActive];						// Point to cpu context

		nSekAddressMaskActive = nSekAddressMask[nSekActive];

#ifdef EMU_C68K
		if (!nSekIsC68K[nSekActive])
#endif
		{
#ifdef EMU_M68K
			m68k_set_context(SekM68KContext[nSekActive]);
#endif
		}

		nSekCyclesTotal = nSekCycles[nSekActive];

		// Allow for SekRun() reentrance:
		nSekCyclesToDo = nSekCyclesToDoCache[nSekActive];
		m68k_ICount = nSekm68k_ICount[nSekActive];
	}
}

// Close the active cpu
void SekClose()
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekClose called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekClose called when no CPU open\n"));
#endif

#ifdef EMU_C68K
	if (!nSekIsC68K[nSekActive])
#endif
	{
#ifdef EMU_M68K
		m68k_get_context(SekM68KContext[nSekActive]);
#endif
	}

	nSekCycles[nSekActive] = nSekCyclesTotal;

	// Allow for SekRun() reentrance:
	nSekCyclesToDoCache[nSekActive] = nSekCyclesToDo;
	nSekm68k_ICount[nSekActive] = m68k_ICount;

	nSekActive = -1;
}

// Get the current CPU
INT32 SekGetActive()
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekGetActive called without init\n"));
#endif

	return nSekActive;
}

// For Megadrive - check if the vdp controlport should set IRQ
INT32 SekShouldInterrupt()
{
#ifdef EMU_C68K
	if (SEK_ACTIVE_IS_C68K) {
		return (c68k_irq_line[nSekActive] > (c68k[nSekActive].srh & 7)) ? 1 : 0;
	}
#endif

	return m68k_check_shouldinterrupt();
}

INT32 SekGetIRQLevel()
{
#ifdef EMU_C68K
	if (SEK_ACTIVE_IS_C68K) {
		return (INT32)c68k_irq_line[nSekActive];
	}
#endif

	return m68k_get_irq();
}

void SekBurnUntilInt()
{
#ifdef EMU_C68K
	if (SEK_ACTIVE_IS_C68K) {
		nC68KSleep[nSekActive] = 1;
		return;
	}
#endif

	m68k_burn_until_irq(1);
}

INT32 SekGetRESETLine()
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekGetRESETLine called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekGetRESETLine called when no CPU open\n"));
#endif


	if (nSekActive != -1)
	{
		return nSekRESETLine[nSekActive];
	}

	return 0;
}

INT32 SekGetRESETLine(INT32 nCPU)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekGetRESETLine called without init\n"));
#endif

	SekCPUPush(nCPU);

	INT32 rc = SekGetRESETLine();

	SekCPUPop();

	return rc;
}

void SekSetRESETLine(INT32 nStatus)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetRESETLine called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetRESETLine called when no CPU open\n"));
#endif

	if (nSekActive != -1)
	{
		if (nSekRESETLine[nSekActive] && nStatus == 0)
		{
			SekReset();
			//bprintf(0, _T("SEK: cleared resetline.\n"));
		}

		nSekRESETLine[nSekActive] = nStatus;
	}
}

void SekSetRESETLine(INT32 nCPU, INT32 nStatus)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetRESETLine called without init\n"));
#endif

	SekCPUPush(nCPU);

	SekSetRESETLine(nStatus);

	SekCPUPop();
}

INT32 SekGetHALT()
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekGetHALT called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekGetHALT called when no CPU open\n"));
#endif


	if (nSekActive != -1)
	{
		return nSekHALT[nSekActive];
	}

	return 0;
}

INT32 SekGetHALT(INT32 nCPU)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekGetHALT called without init\n"));
#endif

	SekCPUPush(nCPU);

	INT32 rc = SekGetHALT();

	SekCPUPop();

	return rc;
}

INT32 SekTotalCycles(INT32 nCPU)
{
	SekCPUPush(nCPU);

	INT32 rc = SekTotalCycles();

	SekCPUPop();

	return rc;
}

void SekSetHALT(INT32 nStatus)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetHALT called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetHALT called when no CPU open\n"));
#endif

	if (nSekActive != -1)
	{
		if (nSekHALT[nSekActive] == 1 && nStatus == 0)
		{
			//bprintf(0, _T("SEK: cleared HALT.\n"));
		}

		if (nSekHALT[nSekActive] == 0 && nStatus == 1)
		{
			//bprintf(0, _T("SEK: entered HALT.\n"));
			// we must halt in the cpu core too
			SekRunEnd();
		}

		nSekHALT[nSekActive] = nStatus;
	}
}

void SekSetHALT(INT32 nCPU, INT32 nStatus)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetHALT called without init\n"));
#endif

	SekCPUPush(nCPU);

	SekSetHALT(nStatus);

	SekCPUPop();
}


// Set the status of an IRQ line on the active CPU
void SekSetIRQLine(const INT32 line, INT32 nstatus)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetIRQLine called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetIRQLine called when no CPU open\n"));
#endif

	if (nstatus == CPU_IRQSTATUS_HOLD)
		nstatus = CPU_IRQSTATUS_AUTO; // on sek, AUTO is HOLD.

	INT32 status = nstatus << 12; // needed for compatibility

#ifdef EMU_C68K
	if (SEK_ACTIVE_IS_C68K) {
		nC68KSleep[nSekActive] = 0;

		if (status) {
			nSekIRQPending[nSekActive] = line | status;
			C68KSetIrqLine(nSekActive, line);

			// No forced exit: like Musashi, irqs are only checked when entering the
			// cpu core and on SR changes.
		} else {
			nSekIRQPending[nSekActive] = 0;
			C68KSetIrqLine(nSekActive, 0);
		}

		return;
	}
#endif

	if (status) {
		nSekIRQPending[nSekActive] = line | status;

#ifdef EMU_M68K
		m68k_set_irq(line);
#endif

		return;
	}

	nSekIRQPending[nSekActive] = 0;

#ifdef EMU_M68K
	m68k_set_irq(0);
#endif
}

void SekSetIRQLine(INT32 nCPU, const INT32 line, INT32 status)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetIRQLine called without init\n"));
#endif

	SekCPUPush(nCPU);

	SekSetIRQLine(line, status);

	SekCPUPop();
}

void SekSetVIRQLine(const INT32 line, INT32 nstatus)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetIRQLine called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetIRQLine called when no CPU open\n"));
#endif

	if (nstatus == CPU_IRQSTATUS_AUTO) {
		nstatus = 4; // special handling for virq
	}

	INT32 status = nstatus << 12; // needed for compatibility

#ifdef EMU_C68K
	if (SEK_ACTIVE_IS_C68K) {
		nC68KSleep[nSekActive] = 0;

		nSekVIRQPending[nSekActive][line] = status;
		c68k_set_virq(line, status ? 1 : 0);

		return;
	}
#endif

	if (status) {
		nSekVIRQPending[nSekActive][line] = status;

#ifdef EMU_M68K
		m68k_set_virq(line, 1);
#endif

		return;
	}

	nSekVIRQPending[nSekActive][line] = 0;

#ifdef EMU_M68K
	m68k_set_virq(line, 0);
#endif
}

void SekSetVIRQLine(INT32 nCPU, const INT32 line, INT32 status)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetVIRQLine called without init\n"));
#endif

	SekCPUPush(nCPU);

	SekSetVIRQLine(line, status);

	SekCPUPop();
}


// Adjust the active CPU's timeslice
void SekRunAdjust(const INT32 nCycles)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekRunAdjust called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekRunAdjust called when no CPU open\n"));
#endif

	if (nCycles < 0 && m68k_ICount < -nCycles) {
		SekRunEnd();
		return;
	}

#ifdef EMU_C68K
	if (SEK_ACTIVE_IS_C68K) {
		if (C68KInHandler()) {
			m68k_ICount += nCycles;
			nSekCyclesToDo += nCycles;
			nSekCyclesSegment += nCycles;
		}
		return;
	}
#endif

#ifdef EMU_M68K
	nSekCyclesToDo += nCycles;
	m68k_modify_timeslice(nCycles);
#endif
}

// End the active CPU's timeslice
void SekRunEnd()
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekRunEnd called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekRunEnd called when no CPU open\n"));
#endif

#ifdef EMU_C68K
	if (SEK_ACTIVE_IS_C68K) {
		if (C68KInHandler()) {
			C68KForceExit();
			nSekCyclesSegment = nSekCyclesDone;
		}
		return;
	}
#endif

#ifdef EMU_M68K
	m68k_end_timeslice();
#endif
}

// Run the active CPU
INT32 SekRun(const INT32 nCycles)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekRun called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekRun called when no CPU open\n"));
#endif

#ifdef EMU_C68K
	nSekRunDepth++;

	if (SEK_ACTIVE_IS_C68K) {
		const INT32 nRet = SekRunC68K(nCycles);
		nSekRunDepth--;
		return nRet;
	}

	// no Cyclone cycle sync while Musashi is running
	const INT32 nPrevRunning = nC68KRunning, nPrevSynced = bC68KSynced;
	nC68KRunning = -1;
	bC68KSynced = 0;
#endif

#ifdef EMU_M68K
	nSekCyclesToDo = nCycles;

	if (nSekRESETLine[nSekActive] || nSekHALT[nSekActive])
	{
		nSekCyclesSegment = nCycles; // idle when RESET high or halted
	}
	else
	{
		nSekCyclesSegment = m68k_execute(nCycles);
	}

	nSekCyclesTotal += nSekCyclesSegment;
	nSekCyclesToDo = m68k_ICount = 0; // was -1; changed june26, 2019 -dink
#else
	nSekCyclesSegment = 0;
#endif

#ifdef EMU_C68K
	nC68KRunning = nPrevRunning;
	bC68KSynced = nPrevSynced;
	nSekRunDepth--;
#endif

	return nSekCyclesSegment;
}

INT32 SekRun(INT32 nCPU, INT32 nCycles)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekRun called without init\n"));
#endif

	SekCPUPush(nCPU);

	INT32 nRet = SekRun(nCycles);

	SekCPUPop();

	return nRet;
}

INT32 SekIdle(INT32 nCPU, INT32 nCycles)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekIdle called without init\n"));
#endif

	SekCPUPush(nCPU);

	INT32 nRet = SekIdle(nCycles);

	SekCPUPop();

	return nRet;
}


// ----------------------------------------------------------------------------
// Breakpoint support

void SekDbgDisableBreakpoints()
{
#if defined FBNEO_DEBUG && defined EMU_M68K
		m68k_set_instr_hook_callback(NULL);

		M68KReadByteDebug = M68KReadByte;
		M68KReadWordDebug = M68KReadWord;
		M68KReadLongDebug = M68KReadLong;

		M68KWriteByteDebug = M68KWriteByte;
		M68KWriteWordDebug = M68KWriteWord;
		M68KWriteLongDebug = M68KWriteLong;
#endif


	mame_debug = 0;
}

#if defined (FBNEO_DEBUG)

void SekDbgEnableBreakpoints()
{
	if (BreakpointDataRead[0].address || BreakpointDataWrite[0].address || BreakpointFetch[0].address) {
#if defined FBNEO_DEBUG && defined EMU_M68K
		SekDbgDisableBreakpoints();

		if (BreakpointFetch[0].address) {
			m68k_set_instr_hook_callback(M68KCheckBreakpoint);
		}

		if (BreakpointDataRead[0].address) {
			M68KReadByteDebug = M68KReadByteBP;
			M68KReadWordDebug = M68KReadWordBP;
			M68KReadLongDebug = M68KReadLongBP;
		}

		if (BreakpointDataWrite[0].address) {
			M68KWriteByteDebug = M68KWriteByteBP;
			M68KWriteWordDebug = M68KWriteWordBP;
			M68KWriteLongDebug = M68KWriteLongBP;
		}
#endif

	} else {
		SekDbgDisableBreakpoints();
	}
}

void SekDbgEnableSingleStep()
{
#if defined FBNEO_DEBUG && defined EMU_M68K
	m68k_set_instr_hook_callback(M68KSingleStep);
#endif

}

INT32 SekDbgSetBreakpointDataRead(UINT32 nAddress, INT32 nIdentifier)
{
	for (INT32 i = 0; i < 8; i++) {
		if (BreakpointDataRead[i].id == nIdentifier) {

			if	(nAddress) {							// Change breakpoint
				BreakpointDataRead[i].address = nAddress;
			} else {									// Delete breakpoint
				for ( ; i < 8; i++) {
					BreakpointDataRead[i] = BreakpointDataRead[i + 1];
				}
			}

			SekDbgEnableBreakpoints();
			return 0;
		}
	}

	// No breakpoints present, add it to the 1st slot
	BreakpointDataRead[0].address = nAddress;
	BreakpointDataRead[0].id = nIdentifier;

	SekDbgEnableBreakpoints();
	return 0;
}

INT32 SekDbgSetBreakpointDataWrite(UINT32 nAddress, INT32 nIdentifier)
{
	for (INT32 i = 0; i < 8; i++) {
		if (BreakpointDataWrite[i].id == nIdentifier) {

			if (nAddress) {								// Change breakpoint
				BreakpointDataWrite[i].address = nAddress;
			} else {									// Delete breakpoint
				for ( ; i < 8; i++) {
					BreakpointDataWrite[i] = BreakpointDataWrite[i + 1];
				}
			}

			SekDbgEnableBreakpoints();
			return 0;
		}
	}

	// No breakpoints present, add it to the 1st slot
	BreakpointDataWrite[0].address = nAddress;
	BreakpointDataWrite[0].id = nIdentifier;

	SekDbgEnableBreakpoints();
	return 0;
}

INT32 SekDbgSetBreakpointFetch(UINT32 nAddress, INT32 nIdentifier)
{
	for (INT32 i = 0; i < 8; i++) {
		if (BreakpointFetch[i].id == nIdentifier) {

			if (nAddress) {								// Change breakpoint
				BreakpointFetch[i].address = nAddress;
			} else {									// Delete breakpoint
				for ( ; i < 8; i++) {
					BreakpointFetch[i] = BreakpointFetch[i + 1];
				}
			}

			SekDbgEnableBreakpoints();
			return 0;
		}
	}

	// No breakpoints present, add it to the 1st slot
	BreakpointFetch[0].address = nAddress;
	BreakpointFetch[0].id = nIdentifier;

	SekDbgEnableBreakpoints();
	return 0;
}

#endif

// ----------------------------------------------------------------------------
// Memory map setup

void SekSetAddressMask(UINT32 nAddressMask)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetAddressMask called without init\n"));
	if (nSekActive == -1) { bprintf(PRINT_ERROR, _T("SekSetAddressMask called when no CPU open\n")); return; }
	if ((nAddressMask & 1) == 0) bprintf(PRINT_ERROR, _T("SekSetAddressMask called with invalid mask! (%x)\n"), nAddressMask);
#endif

	nSekAddressMask[nSekActive] = nSekAddressMaskActive = nAddressMask;
}

// Note - each page is 1 << SEK_BITS.
INT32 SekMapMemory(UINT8* pMemory, UINT32 nStart, UINT32 nEnd, INT32 nType)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekMapMemory called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekMapMemory called when no CPU open\n"));
	if (pMemory == NULL) bprintf(0, _T("SekMapMemory() mapped NULL block!  start, end, type:  %x - %x  0x%x\n"), nStart, nEnd, nType);
#endif

	UINT8* Ptr = pMemory - nStart;
	UINT8** pMemMap = pSekExt->MemMap + (nStart >> SEK_SHIFT);

	// Special case for ROM banks
	if (nType == MAP_ROM) {
		for (UINT32 i = (nStart & ~SEK_PAGEM); i <= nEnd; i += SEK_PAGE_SIZE, pMemMap++) {
			pMemMap[0]			  = Ptr + i;
			pMemMap[SEK_WADD * 2] = Ptr + i;
		}

#ifdef EMU_C68K
		C68KFetchMapChanged();
#endif

		return 0;
	}

	for (UINT32 i = (nStart & ~SEK_PAGEM); i <= nEnd; i += SEK_PAGE_SIZE, pMemMap++) {

		if (nType & MAP_READ) {					// Read
			pMemMap[0]			  = Ptr + i;
		}
		if (nType & MAP_WRITE) {					// Write
			pMemMap[SEK_WADD]	  = Ptr + i;
		}
		if (nType & MAP_FETCH) {					// Fetch
			pMemMap[SEK_WADD * 2] = Ptr + i;
		}
	}

#ifdef EMU_C68K
	if (nType & MAP_FETCH) C68KFetchMapChanged();
#endif

	return 0;
}

INT32 SekMapHandler(uintptr_t nHandler, UINT32 nStart, UINT32 nEnd, INT32 nType)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekMapHander called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekMapHandler called when no CPU open\n"));
#endif

	UINT8** pMemMap = pSekExt->MemMap + (nStart >> SEK_SHIFT);

	// Add to memory map
	for (UINT32 i = (nStart & ~SEK_PAGEM); i <= nEnd; i += SEK_PAGE_SIZE, pMemMap++) {

		if (nType & MAP_READ) {					// Read
			pMemMap[0]			  = (UINT8*)nHandler;
		}
		if (nType & MAP_WRITE) {					// Write
			pMemMap[SEK_WADD]	  = (UINT8*)nHandler;
		}
		if (nType & MAP_FETCH) {					// Fetch
			pMemMap[SEK_WADD * 2] = (UINT8*)nHandler;
		}
	}

#ifdef EMU_C68K
	if (nType & MAP_FETCH) C68KFetchMapChanged();
#endif

	return 0;
}

// Set callbacks
INT32 SekSetResetCallback(pSekResetCallback pCallback)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetResetCallback called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetResetCallback called when no CPU open\n"));
#endif

	pSekExt->ResetCallback = pCallback;

	return 0;
}

INT32 SekSetRTECallback(pSekRTECallback pCallback)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetRTECallback called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetRTECallback called when no CPU open\n"));
#endif

	pSekExt->RTECallback = pCallback;

#ifdef EMU_C68K
	if (SEK_ACTIVE_IS_C68K && pCallback) {
		bprintf(PRINT_IMPORTANT, _T("Cyclone: RTECallback not supported (cpu #%d), use Musashi for this game\n"), nSekActive);
	}
#endif

	return 0;
}

INT32 SekSetIrqCallback(pSekIrqCallback pCallback)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetIrqCallback called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetIrqCallback called when no CPU open\n"));
#endif

	pSekExt->IrqCallback = pCallback;

	return 0;
}

INT32 SekSetCmpCallback(pSekCmpCallback pCallback)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetCmpCallback called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetCmpCallback called when no CPU open\n"));
#endif

	pSekExt->CmpCallback = pCallback;

#ifdef EMU_C68K
	if (SEK_ACTIVE_IS_C68K && pCallback) {
		bprintf(PRINT_IMPORTANT, _T("Cyclone: CmpCallback not supported (cpu #%d), use Musashi for this game\n"), nSekActive);
	}
#endif

	return 0;
}

INT32 SekSetTASCallback(pSekTASCallback pCallback)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetTASCallback called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetTASCallback called when no CPU open\n"));
#endif

	pSekExt->TASCallback = pCallback;

#ifdef EMU_C68K
	if (SEK_ACTIVE_IS_C68K) {
		// Cyclone can't call it on every TAS: it is evaluated once here, its result must be constant
		if (pCallback && pCallback() == 0) {
			if (!bCycloneJumpTabMDInited) {
				memcpy(c68k_jumptab_md, CycloneJumpTab, sizeof(c68k_jumptab_md));
				CycloneSetRealTAS_JT(0, c68k_jumptab_md);
				bCycloneJumpTabMDInited = true;
			}
			c68k[nSekActive].jumptab = (uintptr_t)c68k_jumptab_md;
		} else {
			c68k[nSekActive].jumptab = (uintptr_t)CycloneJumpTab;
		}
	}
#endif

	return 0;
}

// Set handlers
INT32 SekSetReadByteHandler(INT32 i, pSekReadByteHandler pHandler)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetReadByteHandler called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetReadByteHandler called when no CPU open\n"));
#endif

	if (i >= SEK_MAXHANDLER) {
		return 1;
	}

	pSekExt->ReadByte[i] = pHandler;

	return 0;
}

INT32 SekSetWriteByteHandler(INT32 i, pSekWriteByteHandler pHandler)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetWriteByteHandler called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetWriteByteHandler called when no CPU open\n"));
#endif

	if (i >= SEK_MAXHANDLER) {
		return 1;
	}

	pSekExt->WriteByte[i] = pHandler;

	return 0;
}

INT32 SekSetReadWordHandler(INT32 i, pSekReadWordHandler pHandler)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetReadWordHandler called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetReadWordHandler called when no CPU open\n"));
#endif

	if (i >= SEK_MAXHANDLER) {
		return 1;
	}

	pSekExt->ReadWord[i] = pHandler;

	return 0;
}

INT32 SekSetWriteWordHandler(INT32 i, pSekWriteWordHandler pHandler)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetWriteWordHandler called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetWriteWordHandler called when no CPU open\n"));
#endif

	if (i >= SEK_MAXHANDLER) {
		return 1;
	}

	pSekExt->WriteWord[i] = pHandler;

	return 0;
}

INT32 SekSetReadLongHandler(INT32 i, pSekReadLongHandler pHandler)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetReadLongHandler called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetReadLongHandler called when no CPU open\n"));
#endif

	if (i >= SEK_MAXHANDLER) {
		return 1;
	}

	pSekExt->ReadLong[i] = pHandler;

	return 0;
}

INT32 SekSetWriteLongHandler(INT32 i, pSekWriteLongHandler pHandler)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekSetWriteLongHandler called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekSetWriteLongHandler called when no CPU open\n"));
#endif

	if (i >= SEK_MAXHANDLER) {
		return 1;
	}

	pSekExt->WriteLong[i] = pHandler;

	return 0;
}

// ----------------------------------------------------------------------------
// Query register values

UINT32 SekGetPC(INT32 n)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekGetPC called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekGetPC called when no CPU open\n"));
#endif

#ifdef EMU_C68K
	const INT32 nCPU = (n < 0) ? nSekActive : n;
	if (nCPU >= 0 && nSekIsC68K[nCPU]) {
		return (c68k[nCPU].pc - c68k[nCPU].membase) & 0xffffff;
	}
#else
	(void)n;
#endif

#ifdef EMU_M68K
	return m68k_get_reg(NULL, M68K_REG_PC);
#else
	return 0;
#endif
}

UINT32 SekGetPPC(INT32)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekGetPC called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekGetPC called when no CPU open\n"));
#endif

#ifdef EMU_C68K
	if (SEK_ACTIVE_IS_C68K) {
		// prev_pc is a host pointer to the current opcode + 2
		return (c68k[nSekActive].prev_pc - c68k[nSekActive].membase - 2) & 0xffffff;
	}
#endif

#ifdef EMU_M68K
	return m68k_get_reg(NULL, M68K_REG_PPC);
#else
	return 0;
#endif
}

UINT32 SekGetDAR(INT32 n)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekGetDAR called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekGetDAR called when no CPU open\n"));
#endif

#ifdef EMU_C68K
	if (SEK_ACTIVE_IS_C68K) {
		n &= 0xf;
		return (n < 8) ? c68k[nSekActive].d[n] : c68k[nSekActive].a[n - 8];
	}
#endif

#ifdef EMU_M68K
	return m68k_get_dar(n);
#else
	return 0;
#endif
}

INT32 SekDbgGetCPUType()
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekDbgGetCPUType called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekDbgGetCPUType called when no CPU open\n"));
#endif

	switch (nSekCPUType[nSekActive]) {
		case 0:
		case 0x68000:
			return M68K_CPU_TYPE_68000;
		case 0x68010:
			return M68K_CPU_TYPE_68010;
		case 0x68EC020:
			return M68K_CPU_TYPE_68EC020;
	}

	return 0;
}

INT32 SekDbgGetPendingIRQ()
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekDbgGetPendingIRQ called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekDbgGetPendingIRQ called when no CPU open\n"));
#endif

	return nSekIRQPending[nSekActive] & 7;
}

UINT32 SekDbgGetRegister(SekRegister nRegister)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekDbgGetRegister called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekDbgGetRegister called when no CPU open\n"));
#endif

#ifdef EMU_C68K
	if (SEK_ACTIVE_IS_C68K) {
		// SR is not up to date inside handlers (MEMHANDLERS_NEED_FLAGS=0)
		struct Cyclone *pCy = &c68k[nSekActive];
		const bool bSupervisor = (pCy->srh & 0x20) != 0;

		switch (nRegister) {
			case SEK_REG_D0: case SEK_REG_D1: case SEK_REG_D2: case SEK_REG_D3:
			case SEK_REG_D4: case SEK_REG_D5: case SEK_REG_D6: case SEK_REG_D7:
				return pCy->d[nRegister - SEK_REG_D0];

			case SEK_REG_A0: case SEK_REG_A1: case SEK_REG_A2: case SEK_REG_A3:
			case SEK_REG_A4: case SEK_REG_A5: case SEK_REG_A6: case SEK_REG_A7:
				return pCy->a[nRegister - SEK_REG_A0];

			case SEK_REG_PC:
				return SekGetPC(-1);
			case SEK_REG_PPC:
				return SekGetPPC(-1);

			case SEK_REG_SR:
				return CycloneGetSr(pCy);

			case SEK_REG_SP:
				return pCy->a[7];
			case SEK_REG_USP:
				return bSupervisor ? pCy->osp : pCy->a[7];
			case SEK_REG_ISP:
				return bSupervisor ? pCy->a[7] : pCy->osp;

			default:
				return 0;
		}
	}
#endif


	switch (nRegister) {
		case SEK_REG_D0:
			return m68k_get_reg(NULL, M68K_REG_D0);
		case SEK_REG_D1:
			return m68k_get_reg(NULL, M68K_REG_D1);
		case SEK_REG_D2:
			return m68k_get_reg(NULL, M68K_REG_D2);
		case SEK_REG_D3:
			return m68k_get_reg(NULL, M68K_REG_D3);
		case SEK_REG_D4:
			return m68k_get_reg(NULL, M68K_REG_D4);
		case SEK_REG_D5:
			return m68k_get_reg(NULL, M68K_REG_D5);
		case SEK_REG_D6:
			return m68k_get_reg(NULL, M68K_REG_D6);
		case SEK_REG_D7:
			return m68k_get_reg(NULL, M68K_REG_D7);

		case SEK_REG_A0:
			return m68k_get_reg(NULL, M68K_REG_A0);
		case SEK_REG_A1:
			return m68k_get_reg(NULL, M68K_REG_A1);
		case SEK_REG_A2:
			return m68k_get_reg(NULL, M68K_REG_A2);
		case SEK_REG_A3:
			return m68k_get_reg(NULL, M68K_REG_A3);
		case SEK_REG_A4:
			return m68k_get_reg(NULL, M68K_REG_A4);
		case SEK_REG_A5:
			return m68k_get_reg(NULL, M68K_REG_A5);
		case SEK_REG_A6:
			return m68k_get_reg(NULL, M68K_REG_A6);
		case SEK_REG_A7:
			return m68k_get_reg(NULL, M68K_REG_A7);

		case SEK_REG_PC:
			return m68k_get_reg(NULL, M68K_REG_PC);
		case SEK_REG_PPC:
			return m68k_get_reg(NULL, M68K_REG_PPC);

		case SEK_REG_SR:
			return m68k_get_reg(NULL, M68K_REG_SR);

		case SEK_REG_SP:
			return m68k_get_reg(NULL, M68K_REG_SP);
		case SEK_REG_USP:
			return m68k_get_reg(NULL, M68K_REG_USP);
		case SEK_REG_ISP:
			return m68k_get_reg(NULL, M68K_REG_ISP);
		case SEK_REG_MSP:
			return m68k_get_reg(NULL, M68K_REG_MSP);

		case SEK_REG_VBR:
			return m68k_get_reg(NULL, M68K_REG_VBR);

		case SEK_REG_SFC:
			return m68k_get_reg(NULL, M68K_REG_SFC);
		case SEK_REG_DFC:
			return m68k_get_reg(NULL, M68K_REG_DFC);

		case SEK_REG_CACR:
			return m68k_get_reg(NULL, M68K_REG_CACR);
		case SEK_REG_CAAR:
			return m68k_get_reg(NULL, M68K_REG_CAAR);

		default:
			return 0;
	}
}

bool SekDbgSetRegister(SekRegister nRegister, UINT32 nValue)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekDbgSetRegister called without init\n"));
	if (nSekActive == -1) bprintf(PRINT_ERROR, _T("SekDbgSetRegister called when no CPU open\n"));
#endif

	switch (nRegister) {
		case SEK_REG_D0:
		case SEK_REG_D1:
		case SEK_REG_D2:
		case SEK_REG_D3:
		case SEK_REG_D4:
		case SEK_REG_D5:
		case SEK_REG_D6:
		case SEK_REG_D7:
			break;

		case SEK_REG_A0:
		case SEK_REG_A1:
		case SEK_REG_A2:
		case SEK_REG_A3:
		case SEK_REG_A4:
		case SEK_REG_A5:
		case SEK_REG_A6:
		case SEK_REG_A7:
			break;

		case SEK_REG_PC:
#ifdef EMU_C68K
			if (SEK_ACTIVE_IS_C68K) {
				C68KSetPC(nSekActive, nValue);
			} else
#endif
			if (nSekCPUType[nSekActive] == 0) {
			} else {
				m68k_set_reg(M68K_REG_PC, nValue);
			}
			SekClose();
			return true;

		case SEK_REG_SR:
			break;

		case SEK_REG_SP:
		case SEK_REG_USP:
		case SEK_REG_ISP:
		case SEK_REG_MSP:
			break;

		case SEK_REG_VBR:
			break;

		case SEK_REG_SFC:
		case SEK_REG_DFC:
			break;

		case SEK_REG_CACR:
		case SEK_REG_CAAR:
			break;

		default:
			break;
	}

	return false;
}

// ----------------------------------------------------------------------------
// Savestate support

INT32 SekScan(INT32 nAction)
{
#if defined FBNEO_DEBUG
	if (!DebugCPU_SekInitted) bprintf(PRINT_ERROR, _T("SekScan called without init\n"));
#endif

	// Scan the 68000 states
	struct BurnArea ba;

	if ((nAction & ACB_DRIVER_DATA) == 0) {
		return 1;
	}

	memset(&ba, 0, sizeof(ba));

	nSekActive = -1;

	for (INT32 i = 0; i <= nSekCount; i++) {
		char szName[] = "MC68000 #n";

		szName[9] = '0' + i;

		SCAN_VAR(nSekCPUType[i]);
		SCAN_VAR(nSekIRQPending[i]);
		SCAN_VAR(nSekVIRQPending[i]);
		SCAN_VAR(nSekCycles[i]);
		SCAN_VAR(nSekRESETLine[i]);
		SCAN_VAR(nSekHALT[i]);

#ifdef EMU_C68K
		if (nSekIsC68K[i]) {
			static UINT8 cyclone_buffer[0x80];

			ba.Data = cyclone_buffer;
			ba.nLen = sizeof(cyclone_buffer);
			ba.szName = szName;

			if (nAction & ACB_READ) {
				memset(cyclone_buffer, 0, sizeof(cyclone_buffer));
				CyclonePack(&c68k[i], cyclone_buffer);
				BurnAcb(&ba);
			} else if (nAction & ACB_WRITE) {
				memset(cyclone_buffer, 0, sizeof(cyclone_buffer));
				BurnAcb(&ba);

				// CycloneUnpack() calls m68k_checkpc(), which uses the active cpu's memory map
				const INT32 nPrevActive = nSekActive;
				struct SekExt *pPrevExt = pSekExt;
				const UINT32 nPrevMask = nSekAddressMaskActive;
				const INT32 nPrevSynced = bC68KSynced;

				nSekActive = i;
				pSekExt = SekExt[i];
				nSekAddressMaskActive = nSekAddressMask[i];
				bC68KSynced = 2;

				CycloneUnpack(&c68k[i], cyclone_buffer);
				nC68KPending[i] = 0;

				bC68KSynced = nPrevSynced;
				nSekAddressMaskActive = nPrevMask;
				pSekExt = pPrevExt;
				nSekActive = nPrevActive;
			}

			SCAN_VAR(c68k_virq_state[i]);
			SCAN_VAR(c68k_irq_line[i]);
			SCAN_VAR(c68k_nmi_edge[i]);
		} else
#endif
		{
#ifdef EMU_M68K
			if (nSekCPUType[i] != 0) {
				ba.Data = SekM68KContext[i];
				// for savestate portability: preserve our cpu's pointers, they are set up in DrvInit() and can be specific to different systems.
				// Therefore we scan the cpu context structure up until right before the pointers
				ba.nLen = m68k_context_size_no_pointers();
				ba.szName = szName;
				BurnAcb(&ba);
			}
#endif
		}

	}

	return 0;
}
