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
