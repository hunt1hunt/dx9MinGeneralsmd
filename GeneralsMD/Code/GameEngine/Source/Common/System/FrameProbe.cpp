/*
** FrameProbe.cpp — SagePerfDiag low-overhead frame-stage profiler (P0/T1)
** See FrameProbe.h for the contract and Tools/PERF_DIAG_DESIGN.md for design.
**
** Hard rules (memory-palace lessons, do not violate):
**  - NEVER fopen in the per-stage hot path. File IO happens only in
**    FrameProbeFlush(), which runs on ring-full / interval / hotkey.
**  - Single-threaded: all calls come from the main game loop thread.
**  - VC6: no lambdas, explicit loop scopes, plain C arrays.
*/
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#include "Common/System/TerrainDiag.h"
#include "Common/System/FrameProbe.h"

#ifdef FRAME_PROBE

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Ring of frame records. 4096 frames * (13 doubles + 5 ints) = ~460KB, fine.
#define FP_RING_FRAMES		4096
#define FP_MAX_PATH_BUF		260

typedef struct FPFrameRecord
{
	double   stageMs[FP_STAGE_COUNT];		// accumulated stage duration, ms
	int      counter[FP_CNT_COUNT];			// per-frame magnitudes
	unsigned qpcFrame;						// engine frame ordinal
	unsigned wallClockMs;					// timeGetTime at frame end
} FPFrameRecord;

static FPFrameRecord g_ring[FP_RING_FRAMES];
static int           g_ringHead = 0;			// next slot to write
static int           g_ringCount = 0;			// valid records in ring
static int           g_enabled = 0;
static int           g_flushIntervalSec = 30;

static __int64       g_stageStart[FP_STAGE_COUNT];
static __int64       g_cpuMark[FP_CPU_SLOT_COUNT];	// T14: thread CPU time marks
static __int64       g_qpcFreq = 0;
static __int64       g_qpcFreqInvMs = 0;		// avoid per-call division: ms = dt * 1000 / freq (kept simple)

static unsigned      g_frameOrdinal = 0;
static unsigned      g_lastFlushTick = 0;
static unsigned      g_flushFileSeq = 0;
static char          g_dir[FP_MAX_PATH_BUF] = "";

// ---------------------------------------------------------------------------
static double fpDeltaMs(__int64 t0, __int64 t1)
{
	if (g_qpcFreq == 0)
		return 0.0;
	return (double)(t1 - t0) * 1000.0 / (double)g_qpcFreq;
}

void FrameProbeInit(int enable, int flushIntervalSec)
{
	g_enabled = enable;
	g_flushIntervalSec = flushIntervalSec;
	if (flushIntervalSec <= 0)
		g_flushIntervalSec = 30;

	memset(g_ring, 0, sizeof(g_ring));
	memset(g_cpuMark, 0, sizeof(g_cpuMark));
	g_ringHead = 0;
	g_ringCount = 0;
	g_frameOrdinal = 0;

	if (!QueryPerformanceFrequency((LARGE_INTEGER *)&g_qpcFreq))
	{
		// No QPC on this box: disable rather than lie.
		g_qpcFreq = 0;
		g_enabled = 0;
		return;
	}
	g_qpcFreqInvMs = g_qpcFreq; // informational only

	if (g_enabled)
		g_lastFlushTick = timeGetTime();

	// Dump next to the exe (game dir); never hardcode drive letters — the
	// terrain_diag.log E:\ habit bit us before.
	GetModuleFileNameA(NULL, g_dir, FP_MAX_PATH_BUF);
	{
		char *slash = strrchr(g_dir, '\\');
		if (slash)
			*(slash + 1) = '\0';
		else
			g_dir[0] = '\0';
	}

	DEBUG_LOG(("FrameProbe: init enable=%d interval=%ds qpc=%I64d dir='%s'\n",
		g_enabled, g_flushIntervalSec, g_qpcFreq, g_dir));
}

static const char *fpStageName(unsigned int stage)
{
	static const char *names[FP_STAGE_COUNT] = {
		"t_total", "t_radar", "t_audio", "t_client", "t_msg", "t_net",
		"t_logic", "t_net_wait", "t_render", "t_present", "t_postfx",
		"t_fps_spin",
		"t_logic_script", "t_logic_terrain", "t_logic_create",
		"t_logic_ai", "t_logic_pathfind", "t_logic_destroy",
		// T11: must stay in lockstep with the FrameProbeStage enum in FrameProbe.h
		"t_client_input", "t_client_window", "t_client_ghost",
		"t_client_drawables", "t_client_terrain", "t_client_displupd",
		"t_client_strmgr", "t_client_shell", "t_client_ingameui",
		"t_draw_views", "t_draw_rttex",
		// T12: must stay in lockstep with the FrameProbeStage enum in FrameProbe.h
		"t_draw_rttex_water", "t_draw_rttex_shadow",
		// T13: must stay in lockstep with the FrameProbeStage enum in FrameProbe.h
		"t_postfx_ui", "t_postfx_debug", "t_postfx_misc"
	};
	if (stage >= FP_STAGE_COUNT)
		return "?";
	return names[stage];
}

static const char *fpCounterName(unsigned int c)
{
	static const char *names[FP_CNT_COUNT] = {
		"draw_calls", "state_changes", "objects", "drawables", "particles",
		// T14: must stay in lockstep with FrameProbeCounter in FrameProbe.h
		"cpu_render_us", "cpu_postfx_us", "cpu_postfx_ui_us",
		"cpu_postfx_misc_us", "cpu_postfx_debug_us", "cpu_present_us",
		"cpu_rttex_us",
		// T7: 1 = GPU had still not caught up at that point
		"gpu_busy_postfx_end", "gpu_busy_present_end", "gpu_query_unavailable",
		// T7b (2026-09-18): must stay in lockstep with FrameProbeCounter in FrameProbe.h
		"gpu_busy_postfx_start"
	};
	if (c >= FP_CNT_COUNT)
		return "?";
	return names[c];
}

void FrameProbeFlush(void)
{
	char path[FP_MAX_PATH_BUF + 64];
	FILE *f;
	int i, s, c;
	int n = g_ringCount;

	if (n <= 0)
		return;

	sprintf(path, "%sframeprobe_%u_%u.spd", g_dir, (unsigned)timeGetTime(), g_flushFileSeq);
	g_flushFileSeq++;

	f = fopen(path, "w");
	if (f == NULL)
	{
		// Drop the batch, keep running. One retry next flush.
		g_ringCount = 0;
		g_ringHead = 0;
		return;
	}

	fprintf(f, "frame,wall_ms");
	for (s = 0; s < FP_STAGE_COUNT; s++)
		fprintf(f, ",%s", fpStageName((unsigned)s));
	for (c = 0; c < FP_CNT_COUNT; c++)
		fprintf(f, ",%s", fpCounterName((unsigned)c));
	fprintf(f, "\n");

	for (i = 0; i < n; i++)
	{
		FPFrameRecord *r = &g_ring[i];
		fprintf(f, "%u,%u", r->qpcFrame, r->wallClockMs);
		for (s = 0; s < FP_STAGE_COUNT; s++)
			fprintf(f, ",%.3f", r->stageMs[s]);
		for (c = 0; c < FP_CNT_COUNT; c++)
			fprintf(f, ",%d", r->counter[c]);
		fprintf(f, "\n");
	}
	fclose(f);

	DEBUG_LOG(("FrameProbe: flushed %d frames to '%s'\n", n, path));

	// 2026-09-15: memory watermark on the 30s flush cadence (never the hot
	// path). After the 11:09 real-OOM crash (OOM_SYSALLOC 1398140 = 32-bit
	// address space exhausted ~25min into a long game) this shows whether we
	// leak (steady dwAvailVirtual decline) or just peak.
	{
		MEMORYSTATUS ms;
		GlobalMemoryStatus(&ms);
		f = fopen(GetTerrainDiagLogPath(), "a");
		if (f)
		{
			fprintf(f, "[%u] #MEM load=%u availPhys=%u availVirtual=%u totalVirtual=%u\n",
					(unsigned)timeGetTime(), ms.dwMemoryLoad, ms.dwAvailPhys, ms.dwAvailVirtual, ms.dwTotalVirtual);
			fclose(f);
		}
	}

	// 2026-09-17: rewinding g_ringHead to 0 WITHOUT clearing slot 0 made the first
	// frame of every flush window accumulate on top of the previous window's first
	// frame: FrameProbeCount/FrameProbeEnd only ever do "+=", and the per-frame
	// zeroing in FrameProbeEndFrame targets the *next* slot (head+1), never slot 0.
	// Observed symptom: the first row of each .spd carried the running sum of all
	// earlier windows' first rows (objects 651 -> 1302 -> 1953 -> ...; identical
	// growth in draw_calls and in every stage timer), which is what produced the
	// bogus "450k draw_calls peak" headline. Clear the slot we are about to reuse.
	g_ringCount = 0;
	g_ringHead = 0;
	memset(&g_ring[0], 0, sizeof(FPFrameRecord));
	g_lastFlushTick = timeGetTime();
}

void FrameProbeBegin(unsigned int stage)
{
	if (g_enabled == 0 || stage >= FP_STAGE_COUNT)
		return;
	QueryPerformanceCounter((LARGE_INTEGER *)&g_stageStart[stage]);
}

void FrameProbeEnd(unsigned int stage)
{
	__int64 now;
	if (g_enabled == 0 || stage >= FP_STAGE_COUNT)
		return;
	QueryPerformanceCounter((LARGE_INTEGER *)&now);
	// accumulate into the in-flight slot (slot 0 while ring not committed)
	{
		FPFrameRecord *r = &g_ring[g_ringHead];
		r->stageMs[stage] += fpDeltaMs(g_stageStart[stage], now);
	}
}

void FrameProbeCount(unsigned int counter, int add)
{
	if (g_enabled == 0 || counter >= FP_CNT_COUNT)
		return;
	g_ring[g_ringHead].counter[counter] += add;
}

// ---------------------------------------------------------------------------
// T14: thread CPU time (kernel + user). GetThreadTimes reports 100ns FILETIME
// units; the kernel updates them on the scheduler tick (1ms when a high timer
// resolution is active, 15.6ms otherwise). That quantisation is an order of
// magnitude finer than the ~270ms region we are trying to classify, so it is fit
// for the purpose -- and unlike a D3D query it cannot fail on dgVoodoo.
static __int64 fpThreadCpu100ns(void)
{
	FILETIME creation, exitTime, kernel, user;
	__int64 k, u;
	if (!GetThreadTimes(GetCurrentThread(), &creation, &exitTime, &kernel, &user))
		return 0;
	k = ((__int64)kernel.dwHighDateTime << 32) | (__int64)kernel.dwLowDateTime;
	u = ((__int64)user.dwHighDateTime << 32) | (__int64)user.dwLowDateTime;
	return k + u;
}

void FrameProbeCpuMark(unsigned int slot)
{
	if (g_enabled == 0 || slot >= FP_CPU_SLOT_COUNT)
		return;
	g_cpuMark[slot] = fpThreadCpu100ns();
}

int FrameProbeCpuSinceMark(unsigned int slot)
{
	__int64 now, delta;
	if (g_enabled == 0 || slot >= FP_CPU_SLOT_COUNT)
		return 0;
	if (g_cpuMark[slot] == 0)
		return 0;
	now = fpThreadCpu100ns();
	if (now == 0)
		return 0;
	delta = (now - g_cpuMark[slot]) / 10;	// 100ns -> microseconds
	if (delta < 0)
		return 0;
	if (delta > 2147483647)
		delta = 2147483647;					// never wrap an int column
	return (int)delta;
}

void FrameProbeEndFrame(void)
{
	unsigned nowTick;

	if (g_enabled == 0)
		return;

	nowTick = timeGetTime();

	{
		FPFrameRecord *r = &g_ring[g_ringHead];
		r->qpcFrame = g_frameOrdinal;
		r->wallClockMs = nowTick;
	}

	g_frameOrdinal++;
	g_ringHead++;
	if (g_ringCount < FP_RING_FRAMES)
		g_ringCount++;
	if (g_ringHead >= FP_RING_FRAMES)
		g_ringHead = 0;

	// In-flight slot reset: zero the NEXT slot before we start writing it.
	memset(&g_ring[g_ringHead], 0, sizeof(FPFrameRecord));

	// Flush triggers. Ring full OR interval elapsed. Both are bounded-rate.
	if (g_ringCount >= FP_RING_FRAMES)
		FrameProbeFlush();
	else if ((int)(nowTick - g_lastFlushTick) >= g_flushIntervalSec * 1000)
		FrameProbeFlush();
}

void FrameProbeShutdown(void)
{
	if (g_enabled)
		FrameProbeFlush();
	g_enabled = 0;
}

#else // !FRAME_PROBE

// Runtime no-op stubs so call sites link with or without FRAME_PROBE.
void FrameProbeInit(int enable, int flushIntervalSec) { (void)enable; (void)flushIntervalSec; }
void FrameProbeShutdown(void) {}
void FrameProbeBegin(unsigned int stage) { (void)stage; }
void FrameProbeEnd(unsigned int stage) { (void)stage; }
void FrameProbeCount(unsigned int counter, int add) { (void)counter; (void)add; }
void FrameProbeCpuMark(unsigned int slot) { (void)slot; }
int  FrameProbeCpuSinceMark(unsigned int slot) { (void)slot; return 0; }
void FrameProbeEndFrame(void) {}
void FrameProbeFlush(void) {}

#endif // FRAME_PROBE
