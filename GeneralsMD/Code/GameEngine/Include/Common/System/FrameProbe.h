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
	// T11 (2026-09-17): the savegame baseline left ~105ms of t_client unexplained
	// (11% of frame time) once RENDER/POSTFX/PRESENT were subtracted. Stage ids are
	// appended at the end so the existing CSV column order stays stable; the two
	// FP_DRAW_* ids cover the part of W3DDisplay::draw() that runs *before*
	// FP_BEGIN(RENDER) and was therefore never inside any probe.
	FP_CLIENT_INPUT,		// snow / anim2d / keyboard / eva / mouse updates
	FP_CLIENT_WINDOW,		// window manager + video player
	FP_CLIENT_GHOST,		// ghost object manager orphan sweep
	FP_CLIENT_DRAWABLES,	// per-Drawable updateDrawable() loop
	FP_CLIENT_TERRAIN,		// TheTerrainVisual->UPDATE()
	FP_CLIENT_DISPUPD,		// TheDisplay->UPDATE() (outside DRAW)
	FP_CLIENT_STRMGR,		// DisplayStringManager->update()
	FP_CLIENT_SHELL,		// TheShell->UPDATE()
	FP_CLIENT_INGAMEUI,		// TheInGameUI->UPDATE()
	FP_DRAW_VIEWS,			// draw() pre-RENDER: updateViews + particle update
	FP_DRAW_RTTEX,			// draw() pre-RENDER: water/shadow render-target updates
	// T12 (2026-09-18): split FP_DRAW_RTTEX. Shadow textures already short-circuit on a
	// light-position cache (W3DProjectedShadow::update, W3DProjectedShadow.cpp:2242) and
	// the sun is static, so the water reflection is expected to dominate -- this confirms it.
	FP_DRAW_RTTEX_WATER,	// WaterRenderObjClass::updateRenderTargetTextures
	FP_DRAW_RTTEX_SHADOW,	// W3DProjectedShadowManager::updateRenderTargetTextures
	// T13 (2026-09-18): split FP_POSTFX. After the ResolveTextureDDS cache fix,
	// t_render fell 24% but t_postfx ROSE 55% with identical draw_calls -- it ate
	// most of the gain and nothing in the code path explains it. Measure the parts.
	FP_POSTFX_UI,			// TheInGameUI->DRAW()  (the HUD)
	FP_POSTFX_DEBUG,		// debug display + drawFPSStats + framerate bar
	FP_POSTFX_MISC,			// mouse + video buffer + copyright + letterbox + cinematic
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
	// T14 (2026-09-18): thread CPU microseconds consumed inside each region, to be
	// read next to the matching t_* wall-clock column. Wall time alone CANNOT tell
	// "the CPU is doing work" from "the CPU is blocked waiting on the GPU" -- which
	// is exactly the open t_postfx question: after the ResolveTextureDDS cache fix,
	// t_render fell 29% and t_postfx rose 74% with identical draw_calls. If
	// cpu_postfx_us << t_postfx*1000 the block is NOT HUD work and the time was
	// merely relocated by an async ~submit pipeline; if they match it is real CPU.
	FP_CNT_CPU_RENDER_US,
	FP_CNT_CPU_POSTFX_US,
	FP_CNT_CPU_POSTFX_UI_US,
	FP_CNT_CPU_POSTFX_MISC_US,
	FP_CNT_CPU_POSTFX_DEBUG_US,
	FP_CNT_CPU_PRESENT_US,
	FP_CNT_CPU_RTTEX_US,
	// T7 (2026-09-18): GPU-side completion probe. Uses D3DQUERYTYPE_EVENT (the one
	// query type a translation layer is guaranteed to support -- D3D9 timestamps
	// exist but are optional): issue after the scene block, then poll WITHOUT
	// D3DGETDATA_FLUSH at the end of the later blocks. 1 = the GPU had still not
	// caught up at that point. Purely observational -- no flush, no spin.
	FP_CNT_GPU_BUSY_AT_POSTFX_END,
	FP_CNT_GPU_BUSY_AT_PRESENT_END,
	FP_CNT_GPU_QUERY_UNAVAILABLE,
	// T7b (2026-09-18): poll the SAME query immediately on entry to the postfx
	// block. Together with GPU_BUSY_AT_POSTFX_END this brackets where the GPU
	// finished the scene block: busy at start + caught up at end means the GPU's
	// scene work completed *inside* the block that is currently charged for it.
	// Appended (not inserted) so existing CSV column order stays stable.
	FP_CNT_GPU_BUSY_AT_POSTFX_START,
	// T15 (2026-09-18): t_postfx is ~280ms of wall time carrying ~0 CPU, and the
	// GPU is already caught up when it starts (T7b) -- so the thread is BLOCKED on
	// something that is neither the CPU nor the scene's GPU work. Prime suspect is
	// the dynamic vertex/index buffer Lock path (dx8vertexbuffer.cpp:864 uses
	// D3DLOCK_DISCARD / D3DLOCK_NOOVERWRITE, which a translation layer may emulate
	// as a synchronous wait). VB_LOCK_US measures time spent inside those Lock
	// calls; POSTFX_PCPU_US measures total process CPU over the HUD block, so a
	// near-zero value proves no thread of this process was running either.
	FP_CNT_VB_LOCK_US,
	FP_CNT_VB_LOCK_COUNT,
	FP_CNT_POSTFX_PCPU_US,
	FP_CNT_COUNT
} FrameProbeCounter;

// T14: slots for thread-CPU-time intervals. Separate from FrameProbeStage because
// these nest (POSTFX contains POSTFX_UI/MISC/DEBUG) and a single start-time slot
// per stage is already taken by the QPC wall clock.
typedef enum FrameProbeCpuSlot
{
	FP_CPU_RENDER = 0,
	FP_CPU_POSTFX,
	FP_CPU_POSTFX_UI,
	FP_CPU_POSTFX_MISC,
	FP_CPU_POSTFX_DEBUG,
	FP_CPU_PRESENT,
	FP_CPU_RTTEX,
	FP_CPU_SLOT_COUNT
} FrameProbeCpuSlot;

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

// T14: thread CPU time (kernel+user) consumed since the mark, in MICROSECONDS.
// This is deliberately not a wall clock -- its whole purpose is to be compared
// against the matching t_* wall column, to separate "CPU did work" from "CPU was
// blocked waiting on the GPU". Per-slot so nested regions don't clobber each
// other. Returns 0 when the probe is off or no mark was taken yet.
void FrameProbeCpuMark(unsigned int slot);
int  FrameProbeCpuSinceMark(unsigned int slot);

// T15: wall-clock microseconds blocked inside a dynamic vertex/index buffer Lock.
// Callers are in the WW3D2 library, which has no GameEngine include path, so they
// declare these two locally; ww3d2 is a static library linked into RTSI.exe, so it
// is the same module and ordinary linkage resolves. Never leaves a pending mark.
void FrameProbeLockEnter(void);
void FrameProbeLockLeave(void);

// T15: total CPU time (all threads) of this process, in milliseconds. Pair it
// around a block to learn whether any thread was running during it.
int  FrameProbeProcessCpuMs(void);

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
#define FP_CPU_MARK(slot)	FrameProbeCpuMark(FP_CPU_##slot)
#define FP_CPU_SINCE(slot)	FrameProbeCpuSinceMark(FP_CPU_##slot)
#else
#define FP_BEGIN(stage)		((void)0)
#define FP_END(stage)		((void)0)
#define FP_COUNT(cnt, add)	((void)0)
#define FP_CPU_MARK(slot)	((void)0)
#define FP_CPU_SINCE(slot)	(0)
#endif

#endif // __FRAMEPROBE_H
