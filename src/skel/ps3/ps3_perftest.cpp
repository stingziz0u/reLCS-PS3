// ps3_perftest.cpp -- res-PS3 patch 46: the performance test (USRDIR/perf.txt).
//
// Put a file named perf.txt in USRDIR (any content; "secs=N" sets the seconds
// of each run, 4..30, default 6), load a game, stand outdoors (not on a
// mission) and leave the pad alone: the test starts 10 s after that and takes
// about two minutes. It stands Toni where he is and, in Portland, at three
// busy places, turns the camera around him and measures every frame with
// the optimizations off and on (A B A B, so drift averages out), then
// (patch 48) heavier: in the rain, with explosions all around him, and
// both (A B each). "[perf]" lines, a summary per scenario at the end;
// perf.txt is renamed to perf.done.txt. About 6 minutes in Portland.
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
// What is switched between runs (patch 50): the particles' sprites built on
// the PPU ("original") or on an SPU ("optimized", Particle.cpp). "particles" is
// the game thread's time in CParticle::Render without its wait for the SPU,
// "SPU" that wait. The audio mixer stays on its SPU (patch 49: it took
// 1.4-8.6 ms of PPU per frame, 0.2-0.3 on the SPU); its time is split the same
// way. Patch 46's switches showed no difference: the opaque sort stays off and
// the Leeds program without lights stays on.
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
#include "Weather.h"
#include "Explosion.h"
#include "Fire.h"
#include "General.h"

#include "src/ps3/rwps3.h"
extern bool gPS3SortOpaque;			// Renderer.cpp
extern uint64 gPS3MixerBusyUs, gPS3StreamBusyUs;	// sampman_ps3.cpp
extern bool gPS3SpuAudio;			// sampman_ps3.cpp (patch 49)
extern uint64 gPS3SpuWaitUs;
extern bool gPS3SpuParticles;			// Particle.cpp (patch 50)
extern uint64 gPS3ParticleUs, gPS3SpuPartWaitUs;
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
	uint64 mixerUs, streamUs, spuUs;
	uint64 partUs, partSpuUs;
};

struct Spot { const char *name; float x, y, z; };	// z: about the street (the ground is looked for under z + 2)
// Portland streets (from the logs: where peds walked)
static const Spot portlandSpots[] = {
	{ "Chinatown", 1022.6f, -483.0f, 15.0f },
	{ "Red Light", 1005.7f, -296.7f, 5.0f },
	{ "Portland Harbor", 1064.4f, -814.3f, 15.0f },
};

// patch 48: the runs at each place: configuration (0 original, 1 optimized)
// and scenario
enum { SCEN_CLEAR, SCEN_RAIN, SCEN_EXPLOSIONS, SCEN_BOTH, NUM_SCENS };
static const char *const scenNames[NUM_SCENS] = { "clear", "rain", "explosions", "rain + explosions" };
static const int runCfg[] = { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1 };
static const int runScen[] = { SCEN_CLEAR, SCEN_CLEAR, SCEN_CLEAR, SCEN_CLEAR, SCEN_RAIN, SCEN_RAIN,
	SCEN_EXPLOSIONS, SCEN_EXPLOSIONS, SCEN_BOTH, SCEN_BOTH };
#define NUM_RUNS (int)(sizeof(runCfg) / sizeof(runCfg[0]))
static int curScen = -1;	// the scenario in force
static uint64 nextBoomUs;
static int boomCount;

struct Result
{
	int spot, cfg, scen;
	float fps, avgMs, p95Ms, worstMs, missedPct;
	float cpuMs, waitMs, renderMs, flushMs;
	float timerMs[NUM_TIMERS];
	float draws, texBinds, vpSw, fpSw, consts;
	float mixerMs, streamMs, texMs, spuMs;
	float partMs, partSpuMs;
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
static bool prevSort, prevNoLights, prevSpu, prevSpuPart;
static Snapshot snapStart;
static float samples[MAX_SAMPLES];
static int numSamples;
static Result results[8 * NUM_RUNS];
static int numResults;

static uint64 nowUs(void) { return sysGetSystemTime(); }

static void
takeSnapshot(Snapshot *s)
{
	s->pc = rw::ps3::perfCounters;
	for (int i = 0; i < NUM_TIMERS; i++)
		s->timers[i] = PS3_ProfEverUs(timerNames[i]);
	s->spuUs = gPS3SpuWaitUs;
	s->mixerUs = gPS3MixerBusyUs - s->spuUs;	// the PPU's part
	s->streamUs = gPS3StreamBusyUs;
	s->partSpuUs = gPS3SpuPartWaitUs;
	s->partUs = gPS3ParticleUs - s->partSpuUs;
}

static void
setConfig(int cfg)
{
	gPS3SortOpaque = false;
	rw::ps3::optLeedsNoDirLights = true;
	gPS3SpuAudio = true;
	gPS3SpuParticles = cfg != 0;
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
	r.cfg = runCfg[curRun];
	r.scen = runScen[curRun];
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
	r.spuMs = (e.spuUs - snapStart.spuUs) / 1000.0 / frames;
	r.partMs = (e.partUs - snapStart.partUs) / 1000.0 / frames;
	r.partSpuMs = (e.partSpuUs - snapStart.partSpuUs) / 1000.0 / frames;
	r.streamMs = (e.streamUs - snapStart.streamUs) / 1000.0 / frames;
	r.texMs = (e.pc.texThreadUs - snapStart.pc.texThreadUs) / 1000.0 / frames;
	PS3_Logf("[perf] %s, %s, %s (run %d%s): %.1f fps, frame %.2f ms (95%% under %.2f, worst %.1f, over 17.5 ms %.1f%%); "
	         "CPU %.2f ms, RSX wait %.2f; backend %.2f ms (flushCache %.2f); game %.2f, render list %.2f, PreRender %.2f, "
	         "scene %.2f, effects %.2f (particles %.2f, SPU %.2f), 2D %.2f; %.0f draws, %.0f texture binds, VP %.1f FP %.1f, %.0f consts; "
	         "other threads per frame: mixer %.2f ms (SPU %.2f), streams %.2f ms, textures %.2f ms",
	         spotNames[curSpot], scenNames[r.scen], r.cfg ? "SPU particles" : "PPU particles", curRun + 1,
	         r.scen >= SCEN_EXPLOSIONS ? (boomCount ? "" : ", no explosion took") : "", r.fps, r.avgMs, r.p95Ms, r.worstMs,
	         r.missedPct, r.cpuMs, r.waitMs, r.renderMs, r.flushMs, r.timerMs[0], r.timerMs[1], r.timerMs[2],
	         r.timerMs[3], r.timerMs[4], r.partMs, r.partSpuMs, r.timerMs[5], r.draws, r.texBinds, r.vpSw, r.fpSw, r.consts,
	         r.mixerMs, r.spuMs, r.streamMs, r.texMs);
}

static void
logSummary(void)
{
	for (int sc = 0; sc < NUM_SCENS * 2; sc++) {
		int cfg = sc & 1, scen = sc >> 1;
		Result s;
		memset(&s, 0, sizeof(s));
		int n = 0;
		for (int i = 0; i < numResults; i++) {
			if (results[i].cfg != cfg || results[i].scen != scen)
				continue;
			const Result &r = results[i];
			s.avgMs += r.avgMs; s.p95Ms += r.p95Ms; s.missedPct += r.missedPct;
			s.cpuMs += r.cpuMs; s.waitMs += r.waitMs; s.renderMs += r.renderMs; s.flushMs += r.flushMs;
			for (int t = 0; t < NUM_TIMERS; t++)
				s.timerMs[t] += r.timerMs[t];
			s.draws += r.draws; s.texBinds += r.texBinds; s.consts += r.consts;
			s.partMs += r.partMs; s.partSpuMs += r.partSpuMs;
			s.mixerMs += r.mixerMs; s.spuMs += r.spuMs; s.streamMs += r.streamMs; s.texMs += r.texMs;
			if (r.worstMs > s.worstMs)
				s.worstMs = r.worstMs;
			n++;
		}
		if (n == 0)
			continue;
		float k = 1.0f / n;
		PS3_Logf("[perf] SUMMARY %s, %s (%d runs): frame %.2f ms (95%% under %.2f, worst %.1f, over 17.5 ms %.1f%%); CPU %.2f ms "
		         "(backend %.2f, flushCache %.2f; game %.2f, render list %.2f, PreRender %.2f, scene %.2f, effects %.2f (particles %.2f, SPU %.2f), 2D %.2f), "
		         "RSX wait %.2f; %.0f draws, %.0f texture binds, %.0f consts; mixer %.2f ms (SPU %.2f), streams %.2f ms, textures %.2f ms per frame",
		         scenNames[scen], cfg ? "SPU particles" : "PPU particles", n, s.avgMs * k, s.p95Ms * k, s.worstMs, s.missedPct * k, s.cpuMs * k,
		         s.renderMs * k, s.flushMs * k, s.timerMs[0] * k, s.timerMs[1] * k, s.timerMs[2] * k, s.timerMs[3] * k,
		         s.timerMs[4] * k, s.partMs * k, s.partSpuMs * k, s.timerMs[5] * k, s.waitMs * k, s.draws * k, s.texBinds * k, s.consts * k,
		         s.mixerMs * k, s.spuMs * k, s.streamMs * k, s.texMs * k);
	}
}

// the scenario's weather (rain at full strength at once) and, when it ends,
// the explosions' fires put out
static void
setScenario(CPlayerPed *p, int scen)
{
	if (scen == curScen)
		return;
	if (curScen == SCEN_EXPLOSIONS || curScen == SCEN_BOTH) {
		CExplosion::ClearAllExplosions();
		if (p)
			gFireManager.ExtinguishPoint(p->GetPosition(), 80.0f);
	}
	if (scen == SCEN_RAIN || scen == SCEN_BOTH)
		CWeather::ForceWeatherNow(WEATHER_RAINY);
	else
		CWeather::ReleaseWeather();
	curScen = scen;
	boomCount = 0;
	nextBoomUs = 0;
}

static void
keepScenario(CPlayerPed *p, uint64 now)
{
	if (curScen == SCEN_RAIN || curScen == SCEN_BOTH) {
		CWeather::Rain = 1.0f;
		CWeather::WetRoads = 1.0f;
	}
	if ((curScen == SCEN_EXPLOSIONS || curScen == SCEN_BOTH) && now >= nextBoomUs) {
		// one every 0.6 s, 8 to 20 m around him, on the ground: cars,
		// grenades and molotovs (fire) in turn
		nextBoomUs = now + 600000;
		float a = CGeneral::GetRandomNumberInRange(0.0f, TWOPI);
		float d = CGeneral::GetRandomNumberInRange(8.0f, 20.0f);
		CVector pos = p->GetPosition() + CVector(Cos(a) * d, Sin(a) * d, 0.0f);
		bool found = false;
		float gz = CWorld::FindGroundZFor3DCoord(pos.x, pos.y, pos.z + 3.0f, &found);
		if (found)
			pos.z = gz + 0.3f;
		static const eExplosionType types[3] = { EXPLOSION_CAR, EXPLOSION_GRENADE, EXPLOSION_MOLOTOV };
		if (CExplosion::AddExplosion(nil, nil, types[boomCount % 3], pos, 0))
			boomCount++;
	}
}

static void
finishTest(CPlayerPed *p, const char *why)
{
	setScenario(p, SCEN_CLEAR);
	if (p) {
		placePlayer(p, startPos);
		p->m_fRotationCur = p->m_fRotationDest = startHeading;
		keepSafe(p, false);
	}
	gPS3SortOpaque = prevSort;
	rw::ps3::optLeedsNoDirLights = prevNoLights;
	gPS3SpuAudio = prevSpu;
	gPS3SpuParticles = prevSpuPart;
	if (numResults)
		logSummary();
	PS3_Logf("[perf] done (%s); the settings are back as they were (opaque sort %s, Leeds without lights %s, SPU mixer %s, "
	         "SPU particles %s)", why, prevSort ? "on" : "off", prevNoLights ? "on" : "off", prevSpu ? "on" : "off",
	         prevSpuPart ? "on" : "off");
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
		prevSpu = gPS3SpuAudio;
		prevSpuPart = gPS3SpuParticles;
		numSpots = 0;
		spotNames[numSpots] = "where Toni stood";
		spotPos[numSpots++] = startPos;
		if (CGame::currLevel == LEVEL_INDUSTRIAL)
			for (int i = 0; i < (int)ARRAY_SIZE(portlandSpots); i++) {
				spotNames[numSpots] = portlandSpots[i].name;
				spotPos[numSpots++] = CVector(portlandSpots[i].x, portlandSpots[i].y, portlandSpots[i].z);
			}
		PS3_Logf("[perf] start at %.1f %.1f %.1f: %d places x %d runs of %d s: clear (PPU particles, SPU particles, "
		         "PPU, SPU), rain, explosions, rain + explosions (PPU particles, SPU particles each)",
		         startPos.x, startPos.y, startPos.z, numSpots, NUM_RUNS, secsPerRun);
		curScen = -1;
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
	if (p->m_pFire)
		p->m_pFire->Extinguish();	// the molotovs
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
		// streaming settles (a new scenario: the rain comes, the first
		// explosions go off), and the run's configuration
		if (runScen[curRun] != curScen) {
			setScenario(p, runScen[curRun]);
			phaseStart = now;
			return;
		}
		keepScenario(p, now);
		if (secs < 4.0f)
			return;
		setConfig(runCfg[curRun]);
		char a[96];
		snprintf(a, sizeof(a), "PERF %s, %s: %s (%d/%d)", spotNames[curSpot], scenNames[curScen],
		         runCfg[curRun] ? "OPTIMIZED" : "ORIGINAL", curRun + 1, NUM_RUNS);
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
	keepScenario(p, now);
	if (numSamples < MAX_SAMPLES && frameMs > 0.0f)
		samples[numSamples++] = frameMs;
	if (secs < secsPerRun)
		return;
	finishRun();
	if (++curRun < NUM_RUNS) {
		// next configuration at the same place: settle 1 s first
		phase = 1;
		phaseStart = now - 3000000;
		return;
	}
	curRun = 0;
	setScenario(p, SCEN_CLEAR);	// fires out before he goes
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
