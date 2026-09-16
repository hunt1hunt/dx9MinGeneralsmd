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
		"t_fps_spin"
	};
	if (stage >= FP_STAGE_COUNT)
		return "?";
	return names[stage];
}

static const char *fpCounterName(unsigned int c)
{
	static const char *names[FP_CNT_COUNT] = {
		"draw_calls", "state_changes", "objects", "drawables", "particles"
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

	g_ringCount = 0;
	g_ringHead = 0;
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
void FrameProbeEndFrame(void) {}
void FrameProbeFlush(void) {}

#endif // FRAME_PROBE
