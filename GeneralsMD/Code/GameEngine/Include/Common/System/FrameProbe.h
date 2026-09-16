/*
** FrameProbe.h — SagePerfDiag low-overhead frame-stage profiler (P0/T1)
**
** Design doc: Tools/PERF_DIAG_DESIGN.md. All instrumentation compiles away
** unless FRAME_PROBE is defined. Runtime switch: GlobalData INI
** EnableFrameProbe / FrameProbeIntervalSec (see FrameProbeInit).
**
** VC6 constraints honored: no lambdas, no multiline string literals,
** explicit for-loop scopes, no runtime type info.
**
** Usage pattern in hot code (2-3 lines only, keep diffs tiny):
**   FP_BEGIN(CLIENT); ...work...; FP_END(CLIENT);
**   FP_COUNT(DRAW_CALLS, 1);              // counter add
**   FrameProbeEndFrame();                 // exactly once per engine update
*/
#ifndef __FRAMEPROBE_H
#define __FRAMEPROBE_H

// Compile-time enable: Internal and Debug builds always carry the probe code
// (runtime-gated by INI EnableFrameProbe). Release builds compile it away.
#if !defined(FRAME_PROBE) && (defined(_INTERNAL) || defined(_DEBUG) || defined(DEBUG))
#define FRAME_PROBE
#endif

// ---------------------------------------------------------------------------
// Stage ids. Add new stages ONLY at the end (CSV column order is stable).
typedef enum FrameProbeStage
{
	FP_FRAME_TOTAL = 0,		// whole GameEngine::update
	FP_RADAR,				// TheRadar->UPDATE()
	FP_AUDIO,				// TheAudio->UPDATE()
	FP_CLIENT,				// TheGameClient->UPDATE() (contains render draw)
	FP_MSG,					// TheMessageStream->propagateMessages()
	FP_NET,					// TheNetwork->UPDATE()
	FP_LOGIC,				// TheGameLogic->UPDATE()
	FP_NET_WAIT,			// lock-step wait: isFrameDataReady() polling span
	FP_RENDER,				// W3DDisplay::draw scene portion (P1)
	FP_PRESENT,				// Present()/swap wait (P1) - keep separate from RENDER
	FP_POSTFX,				// post-processing (P1)
	FP_FPS_LIMIT_SPIN,		// FPS limiter Sleep(0) spin span
	FP_LOGIC_SCRIPT,		// T5: TheScriptEngine->UPDATE()
	FP_LOGIC_TERRAIN,		// T5: TheTerrainLogic->UPDATE()
	FP_LOGIC_CREATE,		// T5: processCommandList (create/destroy commands)
	FP_LOGIC_AI,			// T5: TheAI->UPDATE()
	FP_LOGIC_PATHFIND,		// T5: ThePartitionManager->UPDATE() (spatial/pathfind)
	FP_LOGIC_DESTROY,		// T5: processDestroyList()
	FP_STAGE_COUNT			// must be last
} FrameProbeStage;

// Counters (per-frame integer magnitudes, not timings)
typedef enum FrameProbeCounter
{
	FP_CNT_DRAW_CALLS = 0,	// DX8Wrapper DrawIndexedPrimitive count (P1/T6)
	FP_CNT_STATE_CHANGES,	// SetTexture/SetRenderState/etc count (P1/T6)
	FP_CNT_OBJECTS,			// GameLogic object count
	FP_CNT_DRAWABLES,		// client drawable count
	FP_CNT_PARTICLES,		// active particles
	FP_CNT_COUNT
} FrameProbeCounter;

#ifdef __cplusplus
extern "C++" {
#endif

// One-time init/shutdown. enable=0 makes all probes no-ops at runtime too.
void FrameProbeInit(int enable, int flushIntervalSec);
void FrameProbeShutdown(void);

// Per-stage begin/end (QPC snapshot into a start-time slot). Cheap: two
// function calls + one counter read each. Do NOT call Begin on a stage
// twice without End in between.
void FrameProbeBegin(unsigned int stage);
void FrameProbeEnd(unsigned int stage);

// Add `add` to a per-frame counter (magnitude, not time).
void FrameProbeCount(unsigned int counter, int add);

// Close the current frame record: commit ring slot, handle flush triggers
// (ring full / interval timer). Call EXACTLY once per GameEngine::update.
void FrameProbeEndFrame(void);

// Manual flush (hotkey / shutdown path).
void FrameProbeFlush(void);

#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------------------------
// Compile-time kill switch. When FRAME_PROBE is not defined every macro
// expands to ((void)0) and zero code is generated.
#ifdef FRAME_PROBE
#define FP_BEGIN(stage)		FrameProbeBegin(FP_##stage)
#define FP_END(stage)		FrameProbeEnd(FP_##stage)
#define FP_COUNT(cnt, add)	FrameProbeCount(FP_CNT_##cnt, (add))
#else
#define FP_BEGIN(stage)		((void)0)
#define FP_END(stage)		((void)0)
#define FP_COUNT(cnt, add)	((void)0)
#endif

#endif // __FRAMEPROBE_H
