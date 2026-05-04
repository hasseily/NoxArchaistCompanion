#pragma once

#include "Common.h"

// Game-specific 6502 program counters and memory addresses for Nox Archaist.
// The frontend populates this once at startup; the emulator's CPU::Fetch()
// trap-points consult it to drive Gamelink/log output.
struct noxcpuconstants
{
	UINT MEM_PARTY;             // memory area where party data starts
	UINT MEM_FOOD;
	UINT MEM_GOLD;
	UINT MEM_PICKS;
	UINT MEM_TORCHES;
	UINT PC_PRINTSTR;           // PRINT.STR routine entry (overridden before screen output, especially in combat)
	UINT PC_CARRIAGE_RETURN1;   // CARRIAGE.RETURN that breaks lines down to 16 chars (battle only)
	UINT PC_CARRIAGE_RETURN2;   // CARRIAGE.RETURN that finishes a line
	UINT PC_COUT;               // COUT (lowest level char-out, A = char)
	UINT A_PRINT_RIGHT;         // A-register value indicating output to right scroll area (conversations)
	UINT PC_INITIATE_COMBAT;    // combat routine start
	UINT PC_END_COMBAT;         // combat routine end (don't log during combat)
};
extern noxcpuconstants cpuconstants;

// Optional frontend callback for the Nox Archaist combat-log trap. The
// emulator calls this from CPU::Fetch() with each character the game's
// COUT routine emits to the right scroll panel; flush=true is a hint
// that this is a line break and the log can commit. Off until the
// frontend sets it.
typedef void (*NoxLogCallbackFn)(char ch, bool flush);
extern NoxLogCallbackFn g_noxLogCallback;
// If true, the trap also captures text emitted during combat (the
// frontend's "include combat" toggle).
extern bool g_noxLogIncludeCombat;

struct regsrec
{
  BYTE a;   // accumulator
  BYTE x;   // index X
  BYTE y;   // index Y
  BYTE ps;  // processor status
  WORD pc;  // program counter
  WORD sp;  // stack pointer
  BYTE bJammed; // CPU has crashed (NMOS 6502 only)
};

// 6502 Processor Status flags
enum {
	AF_SIGN = 0x80,
	AF_OVERFLOW = 0x40,
	AF_RESERVED = 0x20,
	AF_BREAK = 0x10,
	AF_DECIMAL = 0x08,
	AF_INTERRUPT = 0x04,
	AF_ZERO = 0x02,
	AF_CARRY = 0x01
};

extern regsrec    regs;
extern unsigned __int64 g_nCumulativeCycles;

void    CpuDestroy ();
void    CpuCalcCycles(ULONG nExecutedCycles);
uint32_t   CpuExecute(const uint32_t uCycles, const bool bVideoUpdate);
ULONG   CpuGetCyclesThisVideoFrame(ULONG nExecutedCycles);
void    CpuCreateCriticalSection(void);
void    CpuInitialize(void);
void    CpuSetupBenchmark ();
void	CpuIrqReset();
void	CpuIrqAssert(eIRQSRC Device);
void	CpuIrqDeassert(eIRQSRC Device);
void	CpuNmiReset();
void	CpuNmiAssert(eIRQSRC Device);
void	CpuNmiDeassert(eIRQSRC Device);
void    CpuReset ();
void    CpuSaveSnapshot(class YamlSaveHelper& yamlSaveHelper);
void    CpuLoadSnapshot(class YamlLoadHelper& yamlLoadHelper, UINT version);

BYTE	CpuRead(USHORT addr, ULONG uExecutedCycles);
void	CpuWrite(USHORT addr, BYTE value, ULONG uExecutedCycles);

enum eCpuType {CPU_UNKNOWN=0, CPU_6502=1, CPU_65C02, CPU_Z80};	// Don't change! Persisted to Registry

eCpuType GetMainCpu(void);
void     SetMainCpu(eCpuType cpu);
eCpuType ProbeMainCpuDefault(eApple2Type apple2Type);
void     SetMainCpuDefault(eApple2Type apple2Type);
eCpuType GetActiveCpu(void);
void     SetActiveCpu(eCpuType cpu);

bool IsIrqAsserted(void);
bool Is6502InterruptEnabled(void);
void ResetCyclesExecutedForDebugger(void);
bool IsInterruptInLastExecution(void);
void SetIrqOnLastOpcodeCycle(void);
