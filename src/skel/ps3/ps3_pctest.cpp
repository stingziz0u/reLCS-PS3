// ps3_pctest.cpp -- res-PS3 patch 58: the ProcessControl test
// (USRDIR/processcontrol.txt).
//
// Where the game thread's time goes when the world gets busy: the peds' and
// the vehicles' ProcessControl split into AI, physics, suspension, wheels...
// ("[prof] peds:" and "[prof] vehicles:" lines, ps3_prof.cpp) and the worst
// frame of each phase with its parts ("[prof] worst frame").
//
// Put a file named processcontrol.txt in USRDIR (any content; "secs=N" sets
// the seconds of each phase, 8..40, default 15), load a game, stand outdoors
// (not on a mission) and leave the pad alone: it starts 10 s later. Toni
// can't be hurt meanwhile. The phases, each logged on its own:
//   calm        the street as it is
//   traffic     ped and car density at the top
//   pile-up     traffic, and a dozen cars dropped and thrown at each other
//               around Toni
//   chase       traffic, wanted level 4 (the cops come; they can't hurt him)
//   explosions  traffic, an explosion every 0.6 s 8-20 m around him
// The camera turns around him the whole time. At the end everything is put
// back and the file is renamed to processcontrol.done.txt.

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
#include "Explosion.h"
#include "Fire.h"
#include "General.h"
#include "Population.h"
#include "CarCtrl.h"
#include "ModelInfo.h"
#include "VehicleModelInfo.h"
#include "Automobile.h"

extern "C" void PS3_ProfFlush(const char *label);	// ps3_prof.cpp

#define PC_FILE      PS3_USRDIR "/processcontrol.txt"
#define PC_DONE_FILE PS3_USRDIR "/processcontrol.done.txt"

enum { PH_CALM, PH_TRAFFIC, PH_PILE, PH_CHASE, PH_BOOMS, NUM_PHASES };
static const char *const phaseNames[NUM_PHASES] = {
	"processcontrol test: calm", "processcontrol test: traffic", "processcontrol test: pile-up",
	"processcontrol test: chase (wanted 4)", "processcontrol test: explosions"
};

#define MAX_PILE 12
static CEntity *pile[MAX_PILE];	// references: nil if the game removes them
static int numPile;

static int state;		// 0 not looked at, 1 waiting, 2 running, 3 done/off
static int secsPerPhase = 15;
static int phase;
static uint64 waitSince, phaseStart, lastFrameUs, nextEventUs;
static float prevPedDensity, prevCarDensity;
static int eventCount;

static uint64 nowUs(void) { return sysGetSystemTime(); }

static void
showText(const char *text)
{
	wchar msg[96];
	AsciiToUnicode(text, msg);
	CHud::SetHelpMessage(msg, true);
}

static void
keepSafe(CPlayerPed *p, bool on, bool copsCome)
{
	p->bBulletProof = p->bFireProof = p->bExplosionProof = p->bCollisionProof = p->bMeleeProof = on;
	if (p->m_pWanted)
		p->m_pWanted->m_bIgnoredByCops = on && !copsCome;
	if (on && p->m_fHealth < 100.0f)
		p->m_fHealth = 100.0f;
}

// a car model that's loaded now (no streaming in the middle of the test)
static int
loadedCarModel(int k)
{
	int found = 0, first = -1;
	for (int i = 0; i < MODELINFOSIZE; i++) {
		CBaseModelInfo *mi = CModelInfo::GetModelInfo(i);
		if (mi == nil || mi->GetModelType() != MITYPE_VEHICLE)
			continue;
		if (((CVehicleModelInfo*)mi)->m_vehicleType != VEHICLE_TYPE_CAR || i == MI_RCBANDIT)
			continue;
		if (CStreaming::ms_aInfoForModel[i].m_loadState != STREAMSTATE_LOADED)
			continue;
		if (first < 0)
			first = i;
		if (found++ == k)
			return i;
	}
	return first;	// fewer than k: start over
}

static void
dropCar(CPlayerPed *p)
{
	if (numPile >= MAX_PILE)
		return;
	int mi = loadedCarModel(numPile);
	if (mi < 0)
		return;
	float a = CGeneral::GetRandomNumberInRange(0.0f, TWOPI);
	float d = CGeneral::GetRandomNumberInRange(5.0f, 11.0f);
	CVector pos = p->GetPosition() + CVector(Cos(a) * d, Sin(a) * d, 0.0f);
	bool found = false;
	float gz = CWorld::FindGroundZFor3DCoord(pos.x, pos.y, pos.z + 3.0f, &found);
	if (!found)
		return;
	CAutomobile *v = new CAutomobile(mi, RANDOM_VEHICLE);
	if (v == nil)
		return;
	pos.z = gz + v->GetDistanceFromCentreOfMassToBaseOfModel() + CGeneral::GetRandomNumberInRange(1.5f, 3.5f);
	v->SetPosition(pos);
	v->SetHeading(CGeneral::GetRandomNumberInRange(0.0f, TWOPI));
	v->SetStatus(STATUS_ABANDONED);
	v->m_nDoorLock = CARLOCK_LOCKED;
	// thrown towards Toni: they land on each other
	CVector toToni = p->GetPosition() - pos;
	toToni.z = 0.0f;
	toToni.Normalise();
	v->SetMoveSpeed(toToni * CGeneral::GetRandomNumberInRange(0.15f, 0.35f));
	v->SetTurnSpeed(0.0f, 0.0f, CGeneral::GetRandomNumberInRange(-0.05f, 0.05f));
	CWorld::Add(v);
	pile[numPile] = v;
	v->RegisterReference(&pile[numPile]);
	numPile++;
	eventCount++;
}

static void
removePile(void)
{
	for (int i = 0; i < numPile; i++) {
		CEntity *e = pile[i];
		if (e == nil)
			continue;
		e->CleanUpOldReference(&pile[i]);
		pile[i] = nil;
		CVehicle *v = (CVehicle*)e;
		if (v->pDriver || v->m_nNumPassengers)	// someone got in: the game keeps it
			continue;
		CWorld::Remove(v);
		delete v;
	}
	numPile = 0;
}

static void
boom(CPlayerPed *p)
{
	float a = CGeneral::GetRandomNumberInRange(0.0f, TWOPI);
	float d = CGeneral::GetRandomNumberInRange(8.0f, 20.0f);
	CVector pos = p->GetPosition() + CVector(Cos(a) * d, Sin(a) * d, 0.0f);
	bool found = false;
	float gz = CWorld::FindGroundZFor3DCoord(pos.x, pos.y, pos.z + 3.0f, &found);
	if (found)
		pos.z = gz + 0.3f;
	static const eExplosionType types[3] = { EXPLOSION_CAR, EXPLOSION_GRENADE, EXPLOSION_MOLOTOV };
	if (CExplosion::AddExplosion(nil, nil, types[eventCount % 3], pos, 0))
		eventCount++;
}

static void
startPhase(CPlayerPed *p, int ph, uint64 now)
{
	phase = ph;
	phaseStart = now;
	nextEventUs = now;
	eventCount = 0;
	bool busy = ph != PH_CALM;
	CPopulation::PedDensityMultiplier = busy ? Max(prevPedDensity, 1.0f) * 1.5f : prevPedDensity;
	CCarCtrl::CarDensityMultiplier = busy ? Max(prevCarDensity, 1.0f) * 1.5f : prevCarDensity;
	keepSafe(p, true, ph == PH_CHASE);
	if (ph == PH_CHASE)
		p->SetWantedLevel(4);
	if (ph == PH_CALM)	// the time before the test, logged apart (later phases start as one ends)
		PS3_ProfFlush("processcontrol test: (before it, not counted)");
	PS3_Logf("[pctest] phase %d: %s, %d s (densities: peds %.2f, cars %.2f)", ph + 1, phaseNames[ph], secsPerPhase,
	         CPopulation::PedDensityMultiplier, CCarCtrl::CarDensityMultiplier);
	char msg[64];
	snprintf(msg, sizeof(msg), "PROCESSCONTROL TEST %d/%d: DON'T TOUCH THE PAD", ph + 1, NUM_PHASES);
	showText(msg);
}

static void
endPhase(CPlayerPed *p)
{
	PS3_ProfFlush(phaseNames[phase]);
	PS3_Logf("[pctest] phase %d done: %d %s", phase + 1, eventCount,
	         phase == PH_PILE ? "cars dropped" : phase == PH_BOOMS ? "explosions" : "events");
	if (phase == PH_PILE)
		removePile();
	if (phase == PH_CHASE) {
		p->SetWantedLevel(0);
		CWorld::ClearExcitingStuffFromArea(p->GetPosition(), 80.0f, true);
	}
	if (phase == PH_BOOMS) {
		CExplosion::ClearAllExplosions();
		gFireManager.ExtinguishPoint(p->GetPosition(), 80.0f);
	}
}

static void
finishTest(CPlayerPed *p, const char *why)
{
	removePile();
	CPopulation::PedDensityMultiplier = prevPedDensity;
	CCarCtrl::CarDensityMultiplier = prevCarDensity;
	if (p) {
		p->SetWantedLevel(0);
		keepSafe(p, false, false);
	}
	PS3_Logf("[pctest] done (%s); densities and wanted level put back", why);
	wchar none[1] = { 0 };
	CHud::SetHelpMessage(none, true);
	rename(PC_FILE, PC_DONE_FILE);
	state = 3;
}

// CPad::UpdatePads, every frame
void
PS3_ProcessControlTestFrame(void)
{
	if (state == 3)
		return;
	uint64 now = nowUs();
	float frameMs = lastFrameUs ? (now - lastFrameUs) / 1000.0f : 0.0f;
	lastFrameUs = now;

	if (state == 0) {
		FILE *f = fopen(PC_FILE, "rb");
		if (f == nil) {
			state = 3;
			return;
		}
		char line[64];
		while (fgets(line, sizeof(line), f))
			if (strncmp(line, "secs=", 5) == 0)
				secsPerPhase = Clamp(atoi(line + 5), 8, 40);
		fclose(f);
		PS3_Logf("[pctest] processcontrol.txt found: the test starts 10 s after a game is playing, outdoors and not on "
		         "a mission (%d phases of %d s)", NUM_PHASES, secsPerPhase);
		state = 1;
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
				snprintf(a, sizeof(a), "PROCESSCONTROL TEST IN %d S: DON'T TOUCH THE PAD", left);
				showText(a);
				shown = left;
			}
			return;
		}
		prevPedDensity = CPopulation::PedDensityMultiplier;
		prevCarDensity = CCarCtrl::CarDensityMultiplier;
		numPile = 0;
		PS3_Logf("[pctest] start at %.1f %.1f %.1f", p->GetPosition().x, p->GetPosition().y, p->GetPosition().z);
		state = 2;
		startPhase(p, PH_CALM, now);
		return;
	}

	// state 2: running
	if (p == nil || p->InVehicle() || FrontEndMenuManager.m_bMenuActive || CCutsceneMgr::IsRunning()) {
		finishTest(p, p == nil ? "no player" : "interrupted");
		return;
	}
	keepSafe(p, true, phase == PH_CHASE);
	if (phase == PH_CHASE && p->m_pWanted && p->m_pWanted->GetWantedLevel() < 4)
		p->SetWantedLevel(4);

	// the camera turns around him (every direction gets drawn)
	CCam &cam = TheCamera.Cams[TheCamera.ActiveCam];
	cam.Beta += DEGTORAD(30.0f) * Min(frameMs, 100.0f) / 1000.0f;
	if (cam.Beta > PI)
		cam.Beta -= TWOPI;

	if (phase == PH_PILE && now >= nextEventUs) {
		dropCar(p);	// one every 0.8 s
		nextEventUs = now + 800000;
	}
	if (phase == PH_BOOMS && now >= nextEventUs) {
		boom(p);
		nextEventUs = now + 600000;
	}

	if (now - phaseStart >= (uint64)secsPerPhase * 1000000) {
		endPhase(p);
		if (phase + 1 < NUM_PHASES)
			startPhase(p, phase + 1, now);
		else
			finishTest(p, "all phases run");
	}
}

#else
void PS3_ProcessControlTestFrame(void) {}
#endif
