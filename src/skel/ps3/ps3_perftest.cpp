// ps3_perftest.cpp -- res-PS3 patch 46: the performance test (USRDIR/perf.txt).
//
// Put a file named perf.txt in USRDIR (any content; "secs=N" sets the seconds
// of each run, 4..30, default 6), load a game, stand outdoors (not on a
// mission) and leave the pad alone: the test starts 10 s after that and takes
// about two minutes. It stands Toni where he is and, in Portland, at three
// busy places, turns the camera around him and measures every frame with
// the optimizations off and on (A B A B, so drift averages out). "[perf]"
// lines, a summary at the end; perf.txt is renamed to perf.done.txt.
//
// What is measured, per frame:
//   frame time (average, 95% of frames under, worst, frames over 17.5 ms:
//   a missed 60 fps vsync), the CPU time of the game thread and its wait for
//   the RSX (ps3video), the time in the atomic pipelines and in flushCache
//   (the backend: what a render thread could take off the game thread), the
//   main loop's timers (game, render list, PreRender, scene, effects, 2D),
//   draws, texture binds, program switches, constants, and the busy time of
//   the other threads (audio mixer, audio streams, texture conversion).
//
// The optimizations switched between runs:
//   OPAQUE SORT     the opaque entities drawn in model order (Renderer.cpp)
//   LEEDS NO LIGHTS the Leeds program gets no directional lights (ps3device)
// -O3 on the hot files is a build option (Makefile HOT_O3): the log says
// which build this is.

#include "common.h"
#include "ps3_platform.h"

#if PS3_STAGE != 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/systime.h>

#include "Pad.h"
#include "Timer.h"
#include "Hud.h"
#include "Text.h"
#include "World.h"
#include "PlayerPed.h"
#include "Streaming.h"
#include "Camera.h"
#include "Script.h"
#include "Frontend.h"
#include "Game.h"
#include "Wanted.h"
#include "CutsceneMgr.h"

#include "src/ps3/rwps3.h"
extern bool gPS3SortOpaque;			// Renderer.cpp
extern uint64 gPS3MixerBusyUs, gPS3StreamBusyUs;	// sampman_ps3.cpp
unsigned long long PS3_ProfEverUs(const char *name);	// ps3_prof.cpp

#ifndef PS3_HOT_O3
#define PS3_HOT_O3 0
#endif

#define PERF_FILE      PS3_USRDIR "/perf.txt"
#define PERF_DONE_FILE PS3_USRDIR "/perf.done.txt"
#define MAX_SAMPLES    2048

static const char *const timerNames[] = {
	"CGame::Process", "CnstrRenderList", "PreRender", "RenderScene", "RenderEffects", "Render2dStuff"
};
#define NUM_TIMERS (int)(sizeof(timerNames) / sizeof(timerNames[0]))

struct Snapshot
{
	rw::ps3::PerfCounters pc;
	uint64 timers[NUM_TIMERS];
	uint64 mixerUs, streamUs;
};

struct Spot { const char *name; float x, y, z; };	// z: about the street (the ground is looked for under z + 2)
// Portland streets (from the logs: where peds walked)
static const Spot portlandSpots[] = {
	{ "Chinatown", 1022.6f, -483.0f, 15.0f },
	{ "Red Light", 1005.7f, -296.7f, 5.0f },
	{ "Portland Harbor", 1064.4f, -814.3f, 15.0f },
};

struct Result
{
	int spot, cfg;
	float fps, avgMs, p95Ms, worstMs, missedPct;
	float cpuMs, waitMs, renderMs, flushMs;
	float timerMs[NUM_TIMERS];
	float draws, texBinds, vpSw, fpSw, consts;
	float mixerMs, streamMs, texMs;
};

static int state;	// 0 not looked at, 1 waiting, 2 running, 3 done/off
static int secsPerRun = 6;
static uint64 waitSince, phaseStart, lastFrameUs;
static int numSpots;
static const char *spotNames[8];
static CVector spotPos[8];
static int curSpot, curRun, phase;	// phase 0 going there, 1 settling, 2 measuring
static CVector startPos;
static float startHeading;
static bool prevSort, prevNoLights;
static Snapshot snapStart;
static float samples[MAX_SAMPLES];
static int numSamples;
static Result results[8 * 4];
static int numResults;

static uint64 nowUs(void) { return sysGetSystemTime(); }

static void
takeSnapshot(Snapshot *s)
{
	s->pc = rw::ps3::perfCounters;
	for (int i = 0; i < NUM_TIMERS; i++)
		s->timers[i] = PS3_ProfEverUs(timerNames[i]);
	s->mixerUs = gPS3MixerBusyUs;
	s->streamUs = gPS3StreamBusyUs;
}

static void
setConfig(int cfg)
{
	gPS3SortOpaque = cfg != 0;
	rw::ps3::optLeedsNoDirLights = cfg != 0;
}

static void
showText(const char *text)
{
	wchar msg[96];
	AsciiToUnicode(text, msg);
	CHud::SetHelpMessage(msg, true);
}

static void
keepSafe(CPlayerPed *p, bool on)
{
	p->bBulletProof = p->bFireProof = p->bExplosionProof = p->bCollisionProof = p->bMeleeProof = on;
	if (p->m_pWanted) {
		p->m_pWanted->m_bIgnoredByCops = on;
		if (on && p->m_pWanted->GetWantedLevel() > 0)
			p->SetWantedLevel(0);
	}
	if (on && p->m_fHealth < 100.0f)
		p->m_fHealth = 100.0f;
}

static bool
placePlayer(CPlayerPed *p, CVector pos)
{
	CStreaming::LoadScene(pos);
	bool found = false;
	float gz = CWorld::FindGroundZFor3DCoord(pos.x, pos.y, pos.z + 2.0f, &found);
	if (!found)
		return false;
	p->Teleport(CVector(pos.x, pos.y, gz + 1.0f));
	return true;
}

static int
cmpFloat(const void *a, const void *b)
{
	float fa = *(const float*)a, fb = *(const float*)b;
	return fa < fb ? -1 : fa > fb ? 1 : 0;
}

static void
finishRun(void)
{
	Snapshot e;
	takeSnapshot(&e);
	Result &r = results[numResults < (int)ARRAY_SIZE(results) ? numResults++ : numResults - 1];
	r.spot = curSpot;
	r.cfg = curRun & 1;
	double frames = (double)(e.pc.frames - snapStart.pc.frames);
	if (frames < 1.0)
		frames = 1.0;
	static uint64 tbFreq;
	if (tbFreq == 0)
		tbFreq = sysGetTimebaseFrequency();
	double sum = 0.0;
	int over = 0;
	for (int i = 0; i < numSamples; i++) {
		sum += samples[i];
		if (samples[i] > 17.5f)
			over++;
	}
	r.avgMs = numSamples ? (float)(sum / numSamples) : 0.0f;
	r.fps = r.avgMs > 0.0f ? 1000.0f / r.avgMs : 0.0f;
	qsort(samples, numSamples, sizeof(float), cmpFloat);
	r.p95Ms = numSamples ? samples[(numSamples * 95) / 100 < numSamples ? (numSamples * 95) / 100 : numSamples - 1] : 0.0f;
	r.worstMs = numSamples ? samples[numSamples - 1] : 0.0f;
	r.missedPct = numSamples ? 100.0f * over / numSamples : 0.0f;
	r.cpuMs = (e.pc.cpuUs - snapStart.pc.cpuUs) / 1000.0 / frames;
	r.waitMs = (e.pc.waitUs - snapStart.pc.waitUs) / 1000.0 / frames;
	r.renderMs = (e.pc.renderTb - snapStart.pc.renderTb) * 1000.0 / tbFreq / frames;
	r.flushMs = (e.pc.flushTb - snapStart.pc.flushTb) * 1000.0 / tbFreq / frames;
	for (int i = 0; i < NUM_TIMERS; i++)
		r.timerMs[i] = (e.timers[i] - snapStart.timers[i]) / 1000.0 / frames;
	r.draws = (e.pc.draws - snapStart.pc.draws) / frames;
	r.texBinds = (e.pc.texBinds - snapStart.pc.texBinds) / frames;
	r.vpSw = (e.pc.vpSwitches - snapStart.pc.vpSwitches) / frames;
	r.fpSw = (e.pc.fpSwitches - snapStart.pc.fpSwitches) / frames;
	r.consts = (e.pc.consts - snapStart.pc.consts) / frames;
	r.mixerMs = (e.mixerUs - snapStart.mixerUs) / 1000.0 / frames;
	r.streamMs = (e.streamUs - snapStart.streamUs) / 1000.0 / frames;
	r.texMs = (e.pc.texThreadUs - snapStart.pc.texThreadUs) / 1000.0 / frames;
	PS3_Logf("[perf] %s, %s (run %d): %.1f fps, frame %.2f ms (95%% under %.2f, worst %.1f, over 17.5 ms %.1f%%); "
	         "CPU %.2f ms, RSX wait %.2f; backend %.2f ms (flushCache %.2f); game %.2f, render list %.2f, PreRender %.2f, "
	         "scene %.2f, effects %.2f, 2D %.2f; %.0f draws, %.0f texture binds, VP %.1f FP %.1f, %.0f consts; "
	         "other threads per frame: mixer %.2f ms, streams %.2f ms, textures %.2f ms",
	         spotNames[curSpot], r.cfg ? "optimized" : "original", curRun + 1, r.fps, r.avgMs, r.p95Ms, r.worstMs,
	         r.missedPct, r.cpuMs, r.waitMs, r.renderMs, r.flushMs, r.timerMs[0], r.timerMs[1], r.timerMs[2],
	         r.timerMs[3], r.timerMs[4], r.timerMs[5], r.draws, r.texBinds, r.vpSw, r.fpSw, r.consts,
	         r.mixerMs, r.streamMs, r.texMs);
}

static void
logSummary(void)
{
	for (int cfg = 0; cfg < 2; cfg++) {
		Result s;
		memset(&s, 0, sizeof(s));
		int n = 0;
		for (int i = 0; i < numResults; i++) {
			if (results[i].cfg != cfg)
				continue;
			const Result &r = results[i];
			s.avgMs += r.avgMs; s.p95Ms += r.p95Ms; s.missedPct += r.missedPct;
			s.cpuMs += r.cpuMs; s.waitMs += r.waitMs; s.renderMs += r.renderMs; s.flushMs += r.flushMs;
			for (int t = 0; t < NUM_TIMERS; t++)
				s.timerMs[t] += r.timerMs[t];
			s.draws += r.draws; s.texBinds += r.texBinds; s.consts += r.consts;
			s.mixerMs += r.mixerMs; s.streamMs += r.streamMs; s.texMs += r.texMs;
			if (r.worstMs > s.worstMs)
				s.worstMs = r.worstMs;
			n++;
		}
		if (n == 0)
			continue;
		float k = 1.0f / n;
		PS3_Logf("[perf] SUMMARY %s (%d runs): frame %.2f ms (95%% under %.2f, worst %.1f, over 17.5 ms %.1f%%); CPU %.2f ms "
		         "(backend %.2f, flushCache %.2f; game %.2f, render list %.2f, PreRender %.2f, scene %.2f, effects %.2f, 2D %.2f), "
		         "RSX wait %.2f; %.0f draws, %.0f texture binds, %.0f consts; mixer %.2f ms, streams %.2f ms, textures %.2f ms per frame",
		         cfg ? "optimized" : "original", n, s.avgMs * k, s.p95Ms * k, s.worstMs, s.missedPct * k, s.cpuMs * k,
		         s.renderMs * k, s.flushMs * k, s.timerMs[0] * k, s.timerMs[1] * k, s.timerMs[2] * k, s.timerMs[3] * k,
		         s.timerMs[4] * k, s.timerMs[5] * k, s.waitMs * k, s.draws * k, s.texBinds * k, s.consts * k,
		         s.mixerMs * k, s.streamMs * k, s.texMs * k);
	}
}

static void
finishTest(CPlayerPed *p, const char *why)
{
	if (p) {
		placePlayer(p, startPos);
		p->m_fRotationCur = p->m_fRotationDest = startHeading;
		keepSafe(p, false);
	}
	gPS3SortOpaque = prevSort;
	rw::ps3::optLeedsNoDirLights = prevNoLights;
	if (numResults)
		logSummary();
	PS3_Logf("[perf] done (%s); the settings are back as they were (opaque sort %s, Leeds without lights %s)",
	         why, prevSort ? "on" : "off", prevNoLights ? "on" : "off");
	wchar none[1] = { 0 };
	CHud::SetHelpMessage(none, true);
	rename(PERF_FILE, PERF_DONE_FILE);
	state = 3;
}

// CPad::UpdatePads, every frame
void
PS3_PerfTestFrame(void)
{
	if (state == 3)
		return;
	uint64 now = nowUs();
	float frameMs = lastFrameUs ? (now - lastFrameUs) / 1000.0f : 0.0f;
	lastFrameUs = now;

	if (state == 0) {
		FILE *f = fopen(PERF_FILE, "rb");
		if (f == nil) {
			state = 3;
			return;
		}
		char line[64];
		while (fgets(line, sizeof(line), f))
			if (strncmp(line, "secs=", 5) == 0)
				secsPerRun = Clamp(atoi(line + 5), 4, 30);
		fclose(f);
		PS3_Logf("[perf] perf.txt found: the test starts 10 s after a game is playing, outdoors and not on a mission "
		         "(%d s per run); build: -O3 on the hot files %s", secsPerRun, PS3_HOT_O3 ? "ON" : "OFF");
		state = 1;
		waitSince = 0;
		return;
	}

	CPlayerPed *p = FindPlayerPed();
	bool ready = p && !FrontEndMenuManager.m_bMenuActive && !CCutsceneMgr::IsRunning() && !p->InVehicle() &&
	             !CTheScripts::IsPlayerOnAMission() && !CGame::IsInInterior();

	if (state == 1) {
		if (!ready) {
			waitSince = 0;
			return;
		}
		if (waitSince == 0)
			waitSince = now;
		int left = 10 - (int)((now - waitSince) / 1000000);
		if (left > 0) {
			static int shown = -1;
			if (left != shown) {
				char a[64];
				snprintf(a, sizeof(a), "PERF TEST IN %d S: DON'T TOUCH THE PAD", left);
				showText(a);
				shown = left;
			}
			return;
		}
		startPos = p->GetPosition();
		startHeading = p->m_fRotationCur;
		prevSort = gPS3SortOpaque;
		prevNoLights = rw::ps3::optLeedsNoDirLights;
		numSpots = 0;
		spotNames[numSpots] = "where Toni stood";
		spotPos[numSpots++] = startPos;
		if (CGame::currLevel == LEVEL_INDUSTRIAL)
			for (int i = 0; i < (int)ARRAY_SIZE(portlandSpots); i++) {
				spotNames[numSpots] = portlandSpots[i].name;
				spotPos[numSpots++] = CVector(portlandSpots[i].x, portlandSpots[i].y, portlandSpots[i].z);
			}
		PS3_Logf("[perf] start at %.1f %.1f %.1f: %d places x 4 runs (original, optimized, original, optimized) of %d s",
		         startPos.x, startPos.y, startPos.z, numSpots, secsPerRun);
		curSpot = 0;
		curRun = 0;
		phase = 0;
		numResults = 0;
		phaseStart = now;
		state = 2;
		return;
	}

	// state 2: running
	if (p == nil) {
		finishTest(nil, "no player");
		return;
	}
	if (FrontEndMenuManager.m_bMenuActive)
		return;	// paused: the runs go on after
	if (p->InVehicle() || CTheScripts::IsPlayerOnAMission() || CCutsceneMgr::IsRunning()) {
		finishTest(p, "stopped: a vehicle, a mission or a cutscene");
		return;
	}
	keepSafe(p, true);
	float secs = (now - phaseStart) / 1000000.0f;

	if (phase == 0) {
		// go to the place (the first one is where he stands)
		if (curSpot > 0 && !placePlayer(p, spotPos[curSpot])) {
			PS3_Logf("[perf] %s: no ground found, skipped", spotNames[curSpot]);
			if (++curSpot >= numSpots) {
				finishTest(p, "all places run");
				return;
			}
			return;
		}
		phase = 1;
		phaseStart = now;
		return;
	}
	if (phase == 1) {
		// streaming settles, and the first run's configuration
		if (secs < 4.0f)
			return;
		setConfig(curRun & 1);
		char a[96];
		snprintf(a, sizeof(a), "PERF %s: %s (%d/4)", spotNames[curSpot], (curRun & 1) ? "OPTIMIZED" : "ORIGINAL", curRun + 1);
		showText(a);
		numSamples = 0;
		takeSnapshot(&snapStart);
		phase = 2;
		phaseStart = now;
		return;
	}

	// phase 2: measuring, the camera turning around him (45 degrees a second)
	CCam &cam = TheCamera.Cams[TheCamera.ActiveCam];
	cam.Beta += DEGTORAD(45.0f) * Min(frameMs, 100.0f) / 1000.0f;
	if (cam.Beta > PI)
		cam.Beta -= TWOPI;
	if (numSamples < MAX_SAMPLES && frameMs > 0.0f)
		samples[numSamples++] = frameMs;
	if (secs < secsPerRun)
		return;
	finishRun();
	if (++curRun < 4) {
		// next configuration at the same place: settle 1 s first
		phase = 1;
		phaseStart = now - 3000000;
		return;
	}
	curRun = 0;
	if (++curSpot >= numSpots) {
		finishTest(p, "all places run");
		return;
	}
	phase = 0;
	phaseStart = now;
}

#else
void PS3_PerfTestFrame(void) {}
#endif
