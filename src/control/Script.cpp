#include "common.h"
#ifdef __PS3__
#include "ps3_platform.h"
#include <sys/systime.h>
#endif

#include "Script.h"
#include "ScriptCommands.h"

#include "AnimBlendAssociation.h"
#include "AudioManager.h"
#include "Boat.h"
#include "Camera.h"
#include "CarCtrl.h"
#include "CivilianPed.h"
#include "Clock.h"
#include "CopPed.h"
#include "Coronas.h"
#include "Debug.h"
#include "DMAudio.h"
#include "EmergencyPed.h"
#include "FileMgr.h"
#include "FinalMissionMusic.h"
#include "Frontend.h"
#include "General.h"
#include "GenericGameStorage.h"
#include "HandlingMgr.h"
#include "Heli.h"
#include "Hud.h"
#include "Lines.h"
#include "Messages.h"
#include "CutsceneMgr.h"
#include "Pad.h"
#include "Pickups.h"
#include "Pools.h"
#include "Population.h"
#include "Remote.h"
#include "Replay.h"
#include "Stats.h"
#include "Streaming.h"
#include "User.h"
#include "Wanted.h"
#include "Weather.h"
#include "Zones.h"
#include "main.h"
#include "Ropes.h"
#include "ColStore.h"
#include "Fluff.h"
#include "GameLogic.h"
#include "MBlur.h"
#include "PedRoutes.h"
#include "RoadBlocks.h"
#include "SpecialFX.h"
#include "Timecycle.h"
#include "TxdStore.h"
#include "Bike.h"
#include "smallHeap.h"
#ifdef USE_ADVANCED_SCRIPT_DEBUG_OUTPUT
#include <stdarg.h>
#endif

//--LCS: file done except TODOs and command table
uint8* CTheScripts::ScriptSpace;
CRunningScript CTheScripts::ScriptsArray[MAX_NUM_SCRIPTS];
intro_text_line CTheScripts::IntroTextLines[MAX_NUM_INTRO_TEXT_LINES];
intro_script_rectangle CTheScripts::IntroRectangles[MAX_NUM_INTRO_RECTANGLES];
CSprite2d CTheScripts::ScriptSprites[MAX_NUM_SCRIPT_SRPITES];
script_sphere_struct CTheScripts::ScriptSphereArray[MAX_NUM_SCRIPT_SPHERES];
tCollectiveData CTheScripts::CollectiveArray[MAX_NUM_COLLECTIVES];
tUsedObject CTheScripts::UsedObjectArray[MAX_NUM_USED_OBJECTS];
int32 CTheScripts::MultiScriptArray[MAX_NUM_MISSION_SCRIPTS];
tBuildingSwap CTheScripts::BuildingSwapArray[MAX_NUM_BUILDING_SWAPS];
CEntity* CTheScripts::InvisibilitySettingArray[MAX_NUM_INVISIBILITY_SETTINGS];
uint32 CTheScripts::InvisibilitySettingPoolType[MAX_NUM_INVISIBILITY_SETTINGS];
uint32 CTheScripts::InvisibilitySettingPoolHandle[MAX_NUM_INVISIBILITY_SETTINGS];
CStoredLine CTheScripts::aStoredLines[MAX_NUM_STORED_LINES];
bool CTheScripts::DbgFlag;
uint32 CTheScripts::OnAMissionFlag;
int32 CTheScripts::StoreVehicleIndex;
bool CTheScripts::StoreVehicleWasRandom;
CRunningScript *CTheScripts::pIdleScripts;
CRunningScript *CTheScripts::pActiveScripts;
int32 CTheScripts::NextFreeCollectiveIndex;
int32 CTheScripts::LastRandomPedId;
uint16 CTheScripts::NumberOfUsedObjects;
bool CTheScripts::bAlreadyRunningAMissionScript;
bool CTheScripts::bUsingAMultiScriptFile;
uint16 CTheScripts::NumberOfMissionScripts;
uint32 CTheScripts::LargestMissionScriptSize;
uint32 CTheScripts::MainScriptSize;
uint8 CTheScripts::FailCurrentMission;
uint16 CTheScripts::NumScriptDebugLines;
uint16 CTheScripts::NumberOfIntroRectanglesThisFrame;
uint16 CTheScripts::NumberOfIntroTextLinesThisFrame;
uint8 CTheScripts::UseTextCommands;
CMissionCleanup CTheScripts::MissionCleanUp;
CUpsideDownCarCheck CTheScripts::UpsideDownCars;
CStuckCarCheck CTheScripts::StuckCars;
uint16 CTheScripts::CommandsExecuted;
uint16 CTheScripts::ScriptsUpdated;
int32 ScriptParams[32];
uint8 CTheScripts::RiotIntensity;
uint32 CTheScripts::LastMissionPassedTime;
uint16 CTheScripts::NumberOfExclusiveMissionScripts;
bool CTheScripts::bPlayerHasMetDebbieHarry;
bool CTheScripts::bPlayerIsInTheStatium;
int CTheScripts::AllowedCollision[MAX_ALLOWED_COLLISIONS];
int CTheScripts::FSDestroyedFlag;
uint16* CTheScripts::SavedVarIndices;
int CTheScripts::NumSaveVars;
int gScriptsFile = -1;
int CTheScripts::NextProcessId = 1;
bool CTheScripts::InTheScripts;
CRunningScript* pCurrent;
uint16 CTheScripts::NumTrueGlobals;
uint16 CTheScripts::MostGlobals;
CVector gVectorSetInLua;
int CTheScripts::NextScriptCoronaID;
base::cSList<script_corona> CTheScripts::mCoronas;

void
RecoverLCSWorldBuildingSwapState(void)
{
	/* A loaded game resumes the saved script threads after their one-shot
	 * declarations, so DECLARE_FORT_STAUNTON_DESTROYED_FLAG is not executed
	 * again.  In the supported LCS main.scm that declaration lives in a
	 * separately streamed mission-script section and binds global 4512.  Only
	 * the main section is resident here, so restore the declared binding rather
	 * than scanning unrelated resident bytes for an opcode-shaped pattern. */
	CTheScripts::FSDestroyedFlag = 0;
	if(CTheScripts::ScriptSpace == nil)
		return;

	const int32 fortDestroyedGlobal = 4512;
	if(fortDestroyedGlobal < CTheScripts::GetSizeOfVariableSpace()) {
		CTheScripts::FSDestroyedFlag = fortDestroyedGlobal;
		debug("Recovered Fort Staunton destroyed flag at global offset %d\n",
			fortDestroyedGlobal);
		return;
	}

	debug("Could not recover Fort Staunton destroyed flag declaration\n");
}

#ifdef MISSION_REPLAY

static const char* MissionScripts[] = {
	"VIC2",
	"VIC3",
	"VIC4",
	"VIC5",
	"VIC6",
	"VIC7",
	"SAL1",
	"SAL2",
	"SAL3",
	"SAL4",
	"SAL5",
	"SAL6",
	"SAL7",
	"SAL8",
	"JDT1",
	"JDT2",
	"JDT3",
	"JDT4",
	"JDT5",
	"JDT6",
	"JDT7",
	"JDT8",
	"MAC1",
	"MAC2",
	"MAC3",
	"MAC4",
	"MAC5",
	"SALS1",
	"SALS2",
	"SALS3",
	"SALS4",
	"SALS5",
	"SALS6",
	"RAYS1",
	"RAYS2",
	"RAYS3",
	"RAYS4",
	"RAYS5",
	"DONS1",
	"DONS2",
	"DONS3",
	"DONS4",
	"DONS5",
	"DONS6",
	"DONS7",
	"NEDS1",
	"NEDS2",
	"NEDS3",
	"NEDS4",
	"SALH1",
	"SALH2",
	"SALH3",
	"SALH4",
	"SALH5",
	"DONH1",
	"DONH2",
	"DONH3",
	"DONH4",
	"DONH5",
	"DONH6",
	"TOSH1",
	"TOSH2",
	"TOSH3",
	"TOSH4"
};

int AllowMissionReplay;
uint32 NextMissionDelay;
uint32 MissionStartTime;
uint32 WaitForMissionActivate;
uint32 WaitForSave;
float oldTargetX;
float oldTargetY;
int missionRetryScriptIndex;
bool doingMissionRetry;
bool gbTryingPorn4Again;
int IsInAmmunation;
int MissionSkipLevel;

#ifdef USE_MISSION_REPLAY_OVERRIDE_FOR_NON_MOBILE_SCRIPT
bool AlreadySavedGame;
#endif

#endif

const uint32 CRunningScript::nSaveStructSize =
#ifdef COMPATIBLE_SAVES
	536;
#else
	sizeof(CRunningScript);
#endif

CMissionCleanup::CMissionCleanup()
{
	Init();
}

void CMissionCleanup::Init()
{
	m_nCount = 0;
	for (int i = 0; i < MAX_CLEANUP; i++){
		m_sEntities[i].type = CLEANUP_UNUSED;
		m_sEntities[i].id = 0;
	}
}

cleanup_entity_struct* CMissionCleanup::FindFree()
{
	for (int i = 0; i < MAX_CLEANUP; i++){
		if (m_sEntities[i].type == CLEANUP_UNUSED)
			return &m_sEntities[i];
	}
	script_assert(0);
	return nil;
}

void SleepThisPed(cleanup_entity_struct* pCleanup, CPed* pPed)
{
	printf("*** SLEEPING PED %i %i\n", pCleanup->id, pPed->GetModelIndex());
	if (!pPed->GetIsStatic())
		pPed->RemoveFromMovingList();
	pPed->bIsStaticWaitingForCollision = true;
}

void WakeThisPed(cleanup_entity_struct* pCleanup, CPed* pPed)
{
	printf("*** WAKING UP PED %i %i\n", pCleanup->id, pPed->GetModelIndex());
	pPed->bIsStaticWaitingForCollision = false;
	if (!pPed->bIsStatic)
		pPed->AddToMovingList();

}

void CMissionCleanup::AddEntityToList(int32 id, uint8 type)
{
	cleanup_entity_struct* pNew = FindFree();
	if (!pNew)
		return;
	pNew->id = id;
	pNew->type = type;
	m_nCount++;
}

void CMissionCleanup::RemoveEntityFromList(int32 id, uint8 type)
{
	for (int i = 0; i < MAX_CLEANUP; i++){
		if (m_sEntities[i].type == type && m_sEntities[i].id == id){
			switch (m_sEntities[i].type) {
			case CLEANUP_CAR:
			{
				CVehicle* pVehicle = CPools::GetVehiclePool()->GetAt(m_sEntities[i].id);
				if (pVehicle) {
					if (pVehicle->bIsStaticWaitingForCollision) {
						pVehicle->bIsStaticWaitingForCollision = false;
						if (!pVehicle->GetIsStatic())
							pVehicle->AddToMovingList();
					}
				}
				break;
			}
			case CLEANUP_CHAR:
			{
				CPed* pPed = CPools::GetPedPool()->GetAt(m_sEntities[i].id);
				if (pPed) {
					if (pPed->bIsStaticWaitingForCollision) {
						pPed->bIsStaticWaitingForCollision = false;
						if (!pPed->GetIsStatic())
							pPed->AddToMovingList();
					}
				}
				break;
			}
			case CLEANUP_OBJECT:
			{
				CObject* pObject = CPools::GetObjectPool()->GetAt(m_sEntities[i].id);
				if (pObject) {
					if (pObject->bIsStaticWaitingForCollision) {
						pObject->bIsStaticWaitingForCollision = false;
						if (!pObject->GetIsStatic())
							pObject->AddToMovingList();
					}
				}
				break;
			}
			default:
				break;
			}
			m_sEntities[i].id = 0;
			m_sEntities[i].type = CLEANUP_UNUSED;
			m_nCount--;
		}
	}
}

void CMissionCleanup::CheckIfCollisionHasLoadedForMissionObjects()
{
	for (int i = 0; i < MAX_CLEANUP; i++) {
		switch (m_sEntities[i].type) {
		case CLEANUP_CAR:
		{
			CVehicle* pVehicle = CPools::GetVehiclePool()->GetAt(m_sEntities[i].id);
			if (pVehicle) {
				eLevelName level = CTheZones::GetLevelFromPosition(&pVehicle->GetPosition());
				if (level == LEVEL_GENERIC)
					level = CGame::currLevel;
				const eVehicleAppearance appearance = pVehicle->GetVehicleAppearance();
				const bool needsGroundCollision =
					!pVehicle->IsHeli() && !pVehicle->IsPlane() &&
					appearance != VEHICLE_APPEARANCE_HELI &&
					appearance != VEHICLE_APPEARANCE_BOAT &&
					appearance != VEHICLE_APPEARANCE_PLANE;
				if (needsGroundCollision && !CColStore::HasCollisionLoaded(level)) {
					if (!pVehicle->bIsStaticWaitingForCollision) {
						printf("*** SLEEPING VEHICLE %i %i\n", m_sEntities[i].id, pVehicle->GetModelIndex());
						if (!pVehicle->GetIsStatic())
							pVehicle->RemoveFromMovingList();
						pVehicle->bIsStaticWaitingForCollision = true;
					}
				}
				else {
					if (pVehicle->bIsStaticWaitingForCollision) {
						printf("*** WAKING UP VEHICLE %i %i\n", m_sEntities[i].id, pVehicle->GetModelIndex());
						pVehicle->bIsStaticWaitingForCollision = false;
						if (!pVehicle->bIsStatic)
							pVehicle->AddToMovingList();
					}
				}
			}
		}
		break;
		case CLEANUP_OBJECT:
			CObject* pObject = CPools::GetObjectPool()->GetAt(m_sEntities[i].id);
			if (pObject) {
				eLevelName level = CTheZones::GetLevelFromPosition(&pObject->GetPosition());
				if (level == LEVEL_GENERIC)
					level = CGame::currLevel;
				if (!CColStore::HasCollisionLoaded(level)) {
					if (!pObject->bIsStaticWaitingForCollision) {
						if (!pObject->GetIsStatic())
							pObject->RemoveFromMovingList();
						pObject->bIsStaticWaitingForCollision = true;
					}
				}
				else {
					if (pObject->bIsStaticWaitingForCollision) {
						pObject->bIsStaticWaitingForCollision = false;
						if (!pObject->bIsStatic)
							pObject->AddToMovingList();
					}
				}
			}
		}
	}
	for (int i = 0; i < MAX_CLEANUP; i++) {
		switch (m_sEntities[i].type) {
		case CLEANUP_CHAR:
		{
			CPed* pPed = CPools::GetPedPool()->GetAt(m_sEntities[i].id);
			if (pPed) {
				eLevelName level = CTheZones::GetLevelFromPosition(&pPed->GetPosition());
				if (level == LEVEL_GENERIC)
					level = CGame::currLevel;
				if (!pPed->bIsStaticWaitingForCollision) {
					if (pPed->bInVehicle) {
						if (pPed->m_pMyVehicle->GetIsStatic()) {
							SleepThisPed(&m_sEntities[i], pPed);
							continue;
						}
					}
					if (!CColStore::HasCollisionLoaded(level)) {
						if (pPed->bInVehicle && pPed->m_pMyVehicle->GetIsStatic() ||
							pPed->m_attachedTo && pPed->m_attachedTo->GetIsStatic())
							SleepThisPed(&m_sEntities[i], pPed);
					}
				}
				else {
					if (!pPed->bInVehicle) {
						if (CColStore::HasCollisionLoaded(level)) {
							if (!(pPed->bInVehicle && pPed->m_pMyVehicle->GetIsStatic() ||
								pPed->m_attachedTo && pPed->m_attachedTo->GetIsStatic()))
								WakeThisPed(&m_sEntities[i], pPed);
						}
					}
					else {
						if (!pPed->m_pMyVehicle->GetIsStatic()) {
							WakeThisPed(&m_sEntities[i], pPed);
							continue;
						}
						if (CColStore::HasCollisionLoaded(level)) {
							if (!(pPed->bInVehicle && pPed->m_pMyVehicle->GetIsStatic() ||
								pPed->m_attachedTo && pPed->m_attachedTo->GetIsStatic()))
								WakeThisPed(&m_sEntities[i], pPed);
						}
					}
				}

			}
		}
		break;
		}
	}
}

void CMissionCleanup::Process()
{
	CPopulation::m_AllRandomPedsThisType = -1;
	CPopulation::PedDensityMultiplier = 1.0f;
	CCarCtrl::CarDensityMultiplier = 1.0f;
	CPed::nThreatReactionRangeMultiplier = 1;
	CPed::nEnterCarRangeMultiplier = 1;
	for (int i = 0; i < MAX_ALLOWED_COLLISIONS; i++)
		CTheScripts::AllowedCollision[i] = 0;
	FindPlayerPed()->m_pWanted->m_fCrimeSensitivity = 1.0f;
	CRoadBlocks::ClearScriptRoadBlocks();
	CRouteNode::Initialise();
	if (!CWorld::Players[CWorld::PlayerInFocus].m_pRemoteVehicle)
		TheCamera.Restore();
	TheCamera.SetWideScreenOff();
	CSpecialFX::bLiftCam = false;
	// TODO(LCS): CHud::m_ClockEventWarningMinutes = 0;
	// TODO(LCS): CHud::m_ClockEventFlashTimer = 0;
	CTimeCycle::StopExtraColour(0); // TODO: thiscall
	for (int i = 0; i < MISSION_AUDIO_SLOTS; i++)
		DMAudio.ClearMissionAudio(i);
	CWeather::ReleaseWeather();
	for (int i = 0; i < NUM_OF_SPECIAL_CHARS; i++)
		CStreaming::SetMissionDoesntRequireSpecialChar(i);
#ifdef GTA_NETWORK
	if (!gIsMultiplayerGame)
#endif
		CStreaming::ms_disableStreaming = false;
	if (CHud::m_ItemToFlash != ITEM_ARMOUR && CHud::m_ItemToFlash != ITEM_HEALTH)
		CHud::m_ItemToFlash = ITEM_NONE;
	CHud::SetHelpMessage(nil, false); // nil, false, false, true TODO(LCS)
	CUserDisplay::OnscnTimer.m_bDisabled = false;
	CWorld::Players[0].m_pPed->m_pWanted->m_bIgnoredByCops = false;
	CWorld::Players[0].m_pPed->m_pWanted->m_bIgnoredByEveryone = false;
	CWorld::Players[0].MakePlayerSafe(false);
	CWorld::Players[0].m_pPed->m_nFadeDrunkenness = 1;
	CWorld::Players[0].m_pPed->m_nDrunkCountdown = 0;
	CPad::GetPad(0)->SetDrunkInputDelay(0);
	CWorld::Players[0].m_bDriveByAllowed = true;
	CPad::GetPad(0)->unk_B4 = 1.0f;
	CPad::GetPad(0)->unk_B8 = 0.5f;
	DMAudio.ShutUpPlayerTalking(FALSE);
	CVehicle::bDisableRemoteDetonation = false;
	CVehicle::bDisableRemoteDetonationOnContact = false;
	CTheScripts::RiotIntensity = 0;
	CTheScripts::StoreVehicleIndex = -1;
	CTheScripts::StoreVehicleWasRandom = true;
	CTheScripts::UpsideDownCars.Init();
	CTheScripts::StuckCars.Init();
#ifdef FIX_BUGS
	// APPLY_BRAKES_TO_PLAYERS_CAR ends with the mission that asked for it.
	// VIC1 brakes the player's car at Vincenzo's and passes without letting
	// go: with nothing else releasing it (dying doesn't), the player's cars
	// stayed braked for the rest of the game.
	CPad::GetPad(0)->bApplyBrakes = false;
#endif
	for (int i = 0; i < MAX_CLEANUP; i++){
		if (m_sEntities[i].type == CLEANUP_UNUSED)
			continue;
		switch (m_sEntities[i].type) {
		case CLEANUP_CAR:
		{
			CVehicle* v = CPools::GetVehiclePool()->GetAt(m_sEntities[i].id);
			if (v)
				CTheScripts::CleanUpThisVehicle(v);
			break;
		}
		case CLEANUP_CHAR:
		{
			CPed* p = CPools::GetPedPool()->GetAt(m_sEntities[i].id);
			if (p)
				CTheScripts::CleanUpThisPed(p);
			break;
		}
		case CLEANUP_OBJECT:
		{
			CObject* o = CPools::GetObjectPool()->GetAt(m_sEntities[i].id);
			if (o)
				CTheScripts::CleanUpThisObject(o);
			break;
		}
		default:
			break;
		}
		RemoveEntityFromList(m_sEntities[i].id, m_sEntities[i].type);
	}
	for (int i = 1; i < MODELINFOSIZE; i++) {
		if (CStreaming::IsScriptOwnedModel(i))
			CStreaming::SetMissionDoesntRequireModel(i);
	}
#ifdef SECUROM
	if ((myrand() & 3) == 2){
		// if pirated game
		CWeather::ForceHurricaneWeather();
	}
#endif
}

/* NB: CUpsideDownCarCheck is not used by actual script at all
 * It has a weird usage: AreAnyCarsUpsideDown would fail any mission
 * just like death or arrest. */

void CUpsideDownCarCheck::Init()
{
	for (int i = 0; i < MAX_UPSIDEDOWN_CAR_CHECKS; i++){
		m_sCars[i].m_nVehicleIndex = -1;
		m_sCars[i].m_nUpsideDownTimer = 0;
	}
}

bool CUpsideDownCarCheck::IsCarUpsideDown(int32 id)
{
	CVehicle* pVehicle = CPools::GetVehiclePool()->GetAt(id);
	return IsCarUpsideDown(pVehicle);
}

bool CUpsideDownCarCheck::IsCarUpsideDown(CVehicle* pVehicle)
{
	script_assert(pVehicle);
	return pVehicle->GetUp().z <= UPSIDEDOWN_UP_THRESHOLD &&
		pVehicle->GetMoveSpeed().Magnitude() < UPSIDEDOWN_MOVE_SPEED_THRESHOLD &&
		pVehicle->GetTurnSpeed().Magnitude() < UPSIDEDOWN_TURN_SPEED_THRESHOLD;
}

void CUpsideDownCarCheck::UpdateTimers()
{
	uint32 timeStep = CTimer::GetTimeStepInMilliseconds();
	for (int i = 0; i < MAX_UPSIDEDOWN_CAR_CHECKS; i++){
		CVehicle* v = CPools::GetVehiclePool()->GetAt(m_sCars[i].m_nVehicleIndex);
		if (v){
			if (IsCarUpsideDown(m_sCars[i].m_nVehicleIndex))
				m_sCars[i].m_nUpsideDownTimer += timeStep;
			else
				m_sCars[i].m_nUpsideDownTimer = 0;
		}else{
			m_sCars[i].m_nVehicleIndex = -1;
			m_sCars[i].m_nUpsideDownTimer = 0;
		}
	}
}

bool CUpsideDownCarCheck::AreAnyCarsUpsideDown()
{
	for (int i = 0; i < MAX_UPSIDEDOWN_CAR_CHECKS; i++){
		if (m_sCars[i].m_nVehicleIndex >= 0 && m_sCars[i].m_nUpsideDownTimer > UPSIDEDOWN_TIMER_THRESHOLD)
			return true;
	}
	return false;
}

void CUpsideDownCarCheck::AddCarToCheck(int32 id)
{
	uint16 index = 0;
	while (index < MAX_UPSIDEDOWN_CAR_CHECKS && m_sCars[index].m_nVehicleIndex >= 0)
		index++;
#ifdef FIX_BUGS
	if (index >= MAX_UPSIDEDOWN_CAR_CHECKS)
		return;
#endif
	m_sCars[index].m_nVehicleIndex = id;
	m_sCars[index].m_nUpsideDownTimer = 0;
}

void CUpsideDownCarCheck::RemoveCarFromCheck(int32 id)
{
	for (int i = 0; i < MAX_UPSIDEDOWN_CAR_CHECKS; i++){
		if (m_sCars[i].m_nVehicleIndex == id){
			m_sCars[i].m_nVehicleIndex = -1;
			m_sCars[i].m_nUpsideDownTimer = 0;
		}
	}
}

bool CUpsideDownCarCheck::HasCarBeenUpsideDownForAWhile(int32 id)
{
	for (int i = 0; i < MAX_UPSIDEDOWN_CAR_CHECKS; i++){
		if (m_sCars[i].m_nVehicleIndex == id)
			return m_sCars[i].m_nUpsideDownTimer > UPSIDEDOWN_TIMER_THRESHOLD;
	}
	return false;
}

void stuck_car_data::Reset()
{
	m_nVehicleIndex = -1;
	m_vecPos = CVector(-5000.0f, -5000.0f, -5000.0f);
	m_nLastCheck = -1;
	m_fRadius = 0.0f;
	m_nStuckTime = 0;
	m_bStuck = false;
}

void CStuckCarCheck::Init()
{
	for (int i = 0; i < MAX_STUCK_CAR_CHECKS; i++) {
		m_sCars[i].Reset();
	}
}

void CStuckCarCheck::Process()
{
	uint32 timer = CTimer::GetTimeInMilliseconds();
	for (int i = 0; i < MAX_STUCK_CAR_CHECKS; i++){
		if (m_sCars[i].m_nVehicleIndex < 0)
			continue;
		if (timer <= m_sCars[i].m_nStuckTime + m_sCars[i].m_nLastCheck)
			continue;
		CVehicle* pv = CPools::GetVehiclePool()->GetAt(m_sCars[i].m_nVehicleIndex);
		if (!pv){
			m_sCars[i].Reset();
			continue;
		}
		float distance = (pv->GetPosition() - m_sCars[i].m_vecPos).Magnitude();
		m_sCars[i].m_bStuck = distance < m_sCars[i].m_fRadius;
		m_sCars[i].m_vecPos = pv->GetPosition();
		m_sCars[i].m_nLastCheck = timer;
	}
}

void CStuckCarCheck::AddCarToCheck(int32 id, float radius, uint32 time)
{
	CVehicle* pv = CPools::GetVehiclePool()->GetAt(id);
	if (!pv)
		return;
	int index = 0;
	while (index < MAX_STUCK_CAR_CHECKS && m_sCars[index].m_nVehicleIndex >= 0)
		index++;
#ifdef FIX_BUGS
	if (index >= MAX_STUCK_CAR_CHECKS)
		return;
#endif
	m_sCars[index].m_nVehicleIndex = id;
	m_sCars[index].m_vecPos = pv->GetPosition();
	m_sCars[index].m_nLastCheck = CTimer::GetTimeInMilliseconds();
	m_sCars[index].m_fRadius = radius;
	m_sCars[index].m_nStuckTime = time;
	m_sCars[index].m_bStuck = false;
}

void CStuckCarCheck::RemoveCarFromCheck(int32 id)
{
	for (int i = 0; i < MAX_STUCK_CAR_CHECKS; i++){
		if (m_sCars[i].m_nVehicleIndex == id){
			m_sCars[i].Reset();
		}
	}
}

bool CStuckCarCheck::HasCarBeenStuckForAWhile(int32 id)
{
	for (int i = 0; i < MAX_STUCK_CAR_CHECKS; i++){
		if (m_sCars[i].m_nVehicleIndex == id)
			return m_sCars[i].m_bStuck;
	}
	return false;
}

#ifdef __PS3__
static CRunningScript *pScriptRunning;	// the one ProcessOneCommand is in
static CRunningScript *pScriptBroken;	// to be stopped (CTheScripts::Process)
static int32 scriptDummyVar;		// where a broken script's writes go

// the last commands run (any script), for the log when one goes wrong
struct ScriptTrace { CRunningScript *script; uint16 op; uint32 ip; };
static ScriptTrace scriptTrace[32];
static int scriptTraceHead;

void
ScriptBroken(const char *file, int line)
{
	if (pScriptRunning == nil || pScriptBroken == pScriptRunning)
		return;
	pScriptBroken = pScriptRunning;
	char name[9];
	memcpy(name, pScriptRunning->m_abScriptName, 8);
	name[8] = '\0';
	uint32 ip = pScriptRunning->m_nIp;
	PS3_Logf("[script] %s: invalid bytes at ip %u (%s:%d), the script is stopped", name, ip, file, line);
	// its last commands, oldest first
	char buf[256];
	int n = 0;
	buf[0] = '\0';
	for (int i = 0; i < 32; i++) {
		ScriptTrace &t = scriptTrace[(scriptTraceHead + i) % 32];
		if (t.script == pScriptRunning && n < 200)
			n += snprintf(buf + n, sizeof(buf) - n, " %04x@%u", t.op, t.ip);
	}
	PS3_Logf("[script]   its last commands (opcode@ip):%s", buf);
	n = 0;
	uint32 from = ip >= 24 ? ip - 24 : 0;
	for (uint32 a = from; a < ip + 24 && a < CTheScripts::MainScriptSize + CTheScripts::LargestMissionScriptSize && n < 200; a++)
		n += snprintf(buf + n, sizeof(buf) - n, a == ip ? " [%02x]" : " %02x", CTheScripts::ScriptSpace[a]);
	PS3_Logf("[script]   bytes from %u:%s", from, buf);
}

// ---- patch 50: what a mission script waits for --------------------------------
// Vigilante / Brown Thunder (Ren): sometimes no criminals until R3 is
// pressed again, or none any more from some level on. Every 10 s with a
// mission running: the commands the mission scripts ran (opcode x count,
// most first: a script stuck in a loop shows the loop), the cars and peds
// of the world; and right away the random car queries (the vigilante's
// criminals) and the models a script waits more than 5 s for.
static uint16 ps3MissionOps[0x800];
static uint32 ps3MissionOpsTotal, ps3MissionLogMs;
static char ps3MissionNames[48];

// every script's commands, timed (Ren: the scripts took 25 ms a frame with
// ~50 mission commands; the others weren't counted): timebase ticks per
// opcode and per script name, reported every 10 s with the mission line
static uint64 ps3OpTicks[0x800];
static uint32 ps3OpCount[0x800];
static uint32 ps3AllTotal;
struct PS3ScriptTime { char name[9]; uint64 ticks; uint32 count; };
static PS3ScriptTime ps3ScriptTimes[64];
static int ps3NumScriptTimes;

static inline uint64
PS3_Tb(void)
{
	uint64 tb;
	__asm__ volatile("mftb %0" : "=r"(tb));
	return tb;
}

static void
PS3_ScriptTime(CRunningScript *s, int32 command, uint64 ticks)
{
	if (command >= 0 && command < 0x800) {
		ps3OpTicks[command] += ticks;
		ps3OpCount[command]++;
	}
	ps3AllTotal++;
}

// a script's whole CRunningScript::Process (its commands and what's around them)
static uint64 ps3SegTicks[4];	// CTheScripts::Process: before the scripts, the scripts, after, all of it
static void
PS3_ScriptProcessTime(CRunningScript *s, uint64 ticks, uint32 commands)
{
	int i;
	for (i = 0; i < ps3NumScriptTimes; i++)
		if (strncmp(ps3ScriptTimes[i].name, (const char*)s->m_abScriptName, 8) == 0)
			break;
	if (i == ps3NumScriptTimes) {
		if (i == (int)ARRAY_SIZE(ps3ScriptTimes))
			return;
		memcpy(ps3ScriptTimes[i].name, s->m_abScriptName, 8);
		ps3ScriptTimes[i].name[8] = '\0';
		ps3ScriptTimes[i].ticks = 0;
		ps3ScriptTimes[i].count = 0;
		ps3NumScriptTimes++;
	}
	ps3ScriptTimes[i].ticks += ticks;
	ps3ScriptTimes[i].count += commands;
}

static void
PS3_ScriptTimeReport(void)
{
	if (ps3AllTotal == 0)
		return;
	double usPerTick = 1000000.0 / (double)sysGetTimebaseFrequency();
	uint64 total = 0;
	for (int i = 0; i < 0x800; i++)
		total += ps3OpTicks[i];
	char line[480];
	int len = snprintf(line, sizeof(line), "[script] all scripts in 10 s: %u commands, %.0f ms; slowest opcodes (opcode x count = ms):",
	                   ps3AllTotal, total * usPerTick / 1000.0);
	for (int k = 0; k < 10 && len < (int)sizeof(line) - 24; k++) {
		int best = -1;
		for (int i = 0; i < 0x800; i++)
			if (ps3OpTicks[i] && (best < 0 || ps3OpTicks[i] > ps3OpTicks[best]))
				best = i;
		if (best < 0)
			break;
		len += snprintf(line + len, sizeof(line) - len, " %04x x%u = %.1f", best, ps3OpCount[best],
		                ps3OpTicks[best] * usPerTick / 1000.0);
		ps3OpTicks[best] = 0;
	}
	PS3_Log(line);
	PS3_Logf("[script]   CTheScripts::Process in 10 s: %.0f ms (before the scripts %.0f, the scripts %.0f, after %.0f)",
	         ps3SegTicks[3] * usPerTick / 1000.0, ps3SegTicks[0] * usPerTick / 1000.0,
	         ps3SegTicks[1] * usPerTick / 1000.0, ps3SegTicks[2] * usPerTick / 1000.0);
	memset(ps3SegTicks, 0, sizeof(ps3SegTicks));
	len = snprintf(line, sizeof(line), "[script]   slowest scripts, whole time (name x commands = ms):");
	for (int k = 0; k < 10 && len < (int)sizeof(line) - 32; k++) {
		int best = -1;
		for (int i = 0; i < ps3NumScriptTimes; i++)
			if (ps3ScriptTimes[i].ticks && (best < 0 || ps3ScriptTimes[i].ticks > ps3ScriptTimes[best].ticks))
				best = i;
		if (best < 0)
			break;
		len += snprintf(line + len, sizeof(line) - len, " %s x%u = %.1f", ps3ScriptTimes[best].name,
		                ps3ScriptTimes[best].count, ps3ScriptTimes[best].ticks * usPerTick / 1000.0);
		ps3ScriptTimes[best].ticks = 0;
	}
	PS3_Log(line);
	memset(ps3OpTicks, 0, sizeof(ps3OpTicks));
	memset(ps3OpCount, 0, sizeof(ps3OpCount));
	ps3AllTotal = 0;
	ps3NumScriptTimes = 0;
}

static void
PS3_ScriptCountOp(CRunningScript *s, int32 command)
{
	if (!s->m_bIsMissionScript)
		return;
	if (command >= 0 && command < 0x800 && ps3MissionOps[command] < 0xFFFF)
		ps3MissionOps[command]++;
	ps3MissionOpsTotal++;
	char name[9];
	memcpy(name, s->m_abScriptName, 8);
	name[8] = '\0';
	if (strstr(ps3MissionNames, name) == nil && strlen(ps3MissionNames) + strlen(name) + 2 < sizeof(ps3MissionNames)) {
		if (ps3MissionNames[0])
			strcat(ps3MissionNames, " ");
		strcat(ps3MissionNames, name);
	}
}

static void
PS3_ScriptReport(void)
{
	uint32 now = CTimer::GetTimeInMilliseconds();
	if (ps3MissionLogMs == 0 || now < ps3MissionLogMs) {
		ps3MissionLogMs = now;
		return;
	}
	if (now - ps3MissionLogMs < 10000)
		return;
	ps3MissionLogMs = now;
	PS3_ScriptTimeReport();
	if (ps3MissionOpsTotal) {
		char line[400];
		int len = snprintf(line, sizeof(line), "[script] mission (%s) in 10 s: %u commands; most run (opcode x count):",
		                   ps3MissionNames, ps3MissionOpsTotal);
		for (int k = 0; k < 12 && len < (int)sizeof(line) - 16; k++) {
			int best = -1;
			for (int i = 0; i < 0x800; i++)
				if (ps3MissionOps[i] && (best < 0 || ps3MissionOps[i] > ps3MissionOps[best]))
					best = i;
			if (best < 0)
				break;
			len += snprintf(line + len, sizeof(line) - len, " %04x x%u", best, ps3MissionOps[best]);
			ps3MissionOps[best] = 0;
		}
		PS3_Log(line);
		PS3_Logf("[script]   world: vehicles %d of %d (random %d, mission %d, parked %d, police %d; cap %d, density %.2f), peds %d (mission %u; cap %d), streaming %.1f of %.1f MB, %d models requested",
		         CPools::GetVehiclePool()->GetNoOfUsedSpaces(), CPools::GetVehiclePool()->GetSize(),
		         CCarCtrl::NumRandomCars, CCarCtrl::NumMissionCars, CCarCtrl::NumParkedCars, CCarCtrl::NumLawEnforcerCars,
		         CCarCtrl::MaxNumberOfCarsInUse, CCarCtrl::CarDensityMultiplier,
		         CPools::GetPedPool()->GetNoOfUsedSpaces(), CPopulation::ms_nTotalMissionPeds, CPopulation::MaxNumberOfPedsInUse,
		         CStreaming::ms_memoryUsed / 1048576.0f, CStreaming::ms_memoryAvailable / 1048576.0f, CStreaming::ms_numModelsRequested);
	}
	memset(ps3MissionOps, 0, sizeof(ps3MissionOps));
	ps3MissionOpsTotal = 0;
	ps3MissionNames[0] = '\0';
}

// GET_RANDOM_CAR_OF_TYPE_IN_AREA (and _NO_SAVE) from a mission: found or
// not, and how many cars it took (the command marks every random car of
// the area as a mission car, not only the one it returns). One line a
// second at most (the scripts ask every frame until there is one).
void
PS3_ScriptCarQuery(CRunningScript *s, const char *what, int32 handle, int32 marked, float x1, float y1, float x2, float y2, int32 model)
{
	if (!s->m_bIsMissionScript)
		return;
	static uint32 lastMs, calls, found;
	calls++;
	if (handle >= 0)
		found++;
	uint32 now = CTimer::GetTimeInMilliseconds();
	if (handle < 0 && now - lastMs < 1000 && now >= lastMs)
		return;
	lastMs = now;
	char name[9];
	memcpy(name, s->m_abScriptName, 8);
	name[8] = '\0';
	PS3_Logf("[script] %s %s area (%.0f, %.0f)-(%.0f, %.0f) model %d: %s (car %d, %d cars made mission cars; %u asks, %u found since the last line); vehicles %d, random %d, mission %d",
	         name, what, x1, y1, x2, y2, model, handle >= 0 ? "found" : "none", handle, marked, calls, found,
	         CPools::GetVehiclePool()->GetNoOfUsedSpaces(), CCarCtrl::NumRandomCars, CCarCtrl::NumMissionCars);
	calls = found = 0;
}

bool gPS3ChaosMission;

// a getter asked about an entity that isn't there: logged, the script goes on
void
PS3_ScriptGone(CRunningScript *s, const char *what, int32 handle)
{
	char name[9];
	memcpy(name, s->m_abScriptName, 8);
	name[8] = '\0';
	PS3_Logf("[script] %s: %s of handle %d, which isn't there (deleted?): (0, 0, 0), the script goes on", name, what, handle);
}

// HAS_MODEL_LOADED false for more than 5 s (then every 10 s): the model's
// streaming state
void
PS3_ScriptModelWait(CRunningScript *s, int32 model, bool loaded)
{
	static struct { CRunningScript *s; int32 model; uint32 since, logged; } w[8];
	uint32 now = CTimer::GetTimeInMilliseconds();
	int slot = -1, free = -1;
	for (int i = 0; i < 8; i++) {
		if (w[i].s == s && w[i].model == model) { slot = i; break; }
		if (w[i].s == nil && free < 0) free = i;
		else if (w[i].s && now - w[i].since > 60000 && free < 0) free = i;	// stale
	}
	if (loaded) {
		if (slot >= 0) {
			if (w[slot].logged) {
				char name[9];
				memcpy(name, s->m_abScriptName, 8);
				name[8] = '\0';
				PS3_Logf("[script] %s: model %d loaded after %.1f s", name, model, (now - w[slot].since) / 1000.0f);
			}
			w[slot].s = nil;
		}
		return;
	}
	if (slot < 0) {
		if (free < 0)
			return;
		w[free].s = s;
		w[free].model = model;
		w[free].since = now;
		w[free].logged = 0;
		return;
	}
	if (now - w[slot].since < 5000 || (w[slot].logged && now - w[slot].logged < 10000))
		return;
	w[slot].logged = now;
	char name[9];
	memcpy(name, s->m_abScriptName, 8);
	name[8] = '\0';
	CStreamingInfo &si = CStreaming::ms_aInfoForModel[model];
	CBaseModelInfo *mi = model >= 0 && model < MODELINFOSIZE ? CModelInfo::GetModelInfo(model) : nil;
	PS3_Logf("[script] %s waits %.0f s for model %d (%s): load state %d, flags 0x%x; streaming %.1f of %.1f MB, %d models requested",
	         name, (now - w[slot].since) / 1000.0f, model, mi ? mi->GetModelName() : "?", si.m_loadState, si.m_flags,
	         CStreaming::ms_memoryUsed / 1048576.0f, CStreaming::ms_memoryAvailable / 1048576.0f, CStreaming::ms_numModelsRequested);
}

static inline bool
GlobalVarOk(uint32 index)
{
	// a global is read as an int32/float in place: unaligned floats fault on the PPU
	if (index >= 8 && index + 4 <= (uint32)CTheScripts::GetSizeOfVariableSpace() && (index & 3) == 0)
		return true;
	script_bad();
	return false;
}

static inline bool
LocalVarOk(uint32 index, uint32 count)
{
	if (index < count)
		return true;
	script_bad();
	return false;
}
#define GLOBAL_OK(i) GlobalVarOk(i)
#define LOCAL_OK(i) LocalVarOk(i, ARRAY_SIZE(m_anLocalVariables))
#else
#define GLOBAL_OK(i) true
#define LOCAL_OK(i) true
#endif

void CRunningScript::CollectParameters(uint32* pIp, int16 total, int* pParameters)
{
	while (total--){
		switch (CTheScripts::Read1ByteFromScript(pIp))
		{
		case ARGUMENT_END:
			return;
		case ARGUMENT_INT_ZERO:
			*pParameters = 0;
			break;
		case ARGUMENT_FLOAT_ZERO:
			*pParameters = 0;
			break;
		case ARGUMENT_FLOAT_1BYTE:
			*pParameters = (uint32)(uint8)CTheScripts::Read1ByteFromScript(pIp) << 24;
			break;
		case ARGUMENT_FLOAT_2BYTES:
			*pParameters = (uint32)(uint16)CTheScripts::Read2BytesFromScript(pIp) << 16;
			break;
		case ARGUMENT_FLOAT_3BYTES:
			*pParameters = (uint32)(uint8)CTheScripts::Read1ByteFromScript(pIp) << 8;
			*pParameters |= (uint32)(uint16)CTheScripts::Read2BytesFromScript(pIp) << 16;
			break;
		case ARGUMENT_INT32:
		case ARGUMENT_FLOAT:
			*pParameters = CTheScripts::Read4BytesFromScript(pIp);
			break;
		case ARGUMENT_INT8:
			*pParameters = CTheScripts::Read1ByteFromScript(pIp);
			break;
		case ARGUMENT_INT16:
			*pParameters = CTheScripts::Read2BytesFromScript(pIp);
			break;
		default:
			*pIp -= 1;
			*pParameters = *GetPointerToScriptVariable(pIp, 0);
			break;
		}
		pParameters++;
	}
}


int32 CRunningScript::CollectNextParameterWithoutIncreasingPC(uint32 ip)
{
	uint32* pIp = &ip;
	int tmp;
	switch (CTheScripts::Read1ByteFromScript(pIp))
	{
	case ARGUMENT_END:
		return 0; // TODO(LCS)
	case ARGUMENT_INT_ZERO:
		return 0;
	case ARGUMENT_FLOAT_ZERO:
		return 0;
	case ARGUMENT_FLOAT_1BYTE:
		return (uint32)(uint8)CTheScripts::Read1ByteFromScript(pIp) << 24;
	case ARGUMENT_FLOAT_2BYTES:
		return (uint32)(uint16)CTheScripts::Read2BytesFromScript(pIp) << 16;
	case ARGUMENT_FLOAT_3BYTES:
		tmp = (uint32)(uint8)CTheScripts::Read1ByteFromScript(pIp) << 8;
		tmp |= (uint32)(uint16)CTheScripts::Read2BytesFromScript(pIp) << 16;
		return tmp;
	case ARGUMENT_INT32:
		return CTheScripts::Read4BytesFromScript(pIp);
	case ARGUMENT_INT8:
		return CTheScripts::Read1ByteFromScript(pIp);
	case ARGUMENT_INT16:
		return CTheScripts::Read2BytesFromScript(pIp);
	case ARGUMENT_FLOAT:
		return CTheScripts::Read4BytesFromScript(pIp);
	default:
		(*pIp)--;
		return *GetPointerToScriptVariable(pIp, 0);
	}
	return -1;
}

void CRunningScript::StoreParameters(uint32* pIp, int16 number)
{
	for (int16 i = 0; i < number; i++){
		*GetPointerToScriptVariable(pIp, 0) = ScriptParams[i];
	}
}

int32* GetPointerToScriptVariable(CRunningScript* pScript, uint32* pIp)
{
	uint8 type = CTheScripts::Read1ByteFromScript(pIp);
	if (type >= ARGUMENT_GLOBAL_ARRAY) {
		uint8 index_in_block = CTheScripts::Read1ByteFromScript(pIp);
		uint8 index_id = CTheScripts::Read1ByteFromScript(pIp);
		uint8 size = CTheScripts::Read1ByteFromScript(pIp);
		script_assert(size > 0);
		script_assert(pScript->m_anLocalVariables[pScript->m_nLocalsPointer + index_id] < size);
		uint8 index = Min(pScript->m_anLocalVariables[pScript->m_nLocalsPointer + index_id], size - 1);
		return (int32*)&CTheScripts::ScriptSpace[4 * (((int)(type - ARGUMENT_GLOBAL_ARRAY) << 8) + index + index_in_block)];
	}
	else if (type >= ARGUMENT_GLOBAL) {
		uint8 index_in_block = CTheScripts::Read1ByteFromScript(pIp);
		return (int32*)&CTheScripts::ScriptSpace[4 * (((int)(type - ARGUMENT_GLOBAL) << 8) + index_in_block)];
	}
	else if (type >= ARGUMENT_LOCAL_ARRAY) {
		uint8 index_id = CTheScripts::Read1ByteFromScript(pIp);
		uint8 size = CTheScripts::Read1ByteFromScript(pIp);
		script_assert(size > 0);
		script_assert(pScript->m_anLocalVariables[pScript->m_nLocalsPointer + index_id] < size);
		uint8 index = Min(pScript->m_anLocalVariables[pScript->m_nLocalsPointer + index_id], size - 1);
		return &pScript->m_anLocalVariables[pScript->m_nLocalsPointer + (type - ARGUMENT_LOCAL_ARRAY) + index];
	}
	else if (type >= ARGUMENT_LOCAL) {
		return &pScript->m_anLocalVariables[pScript->m_nLocalsPointer + (type - ARGUMENT_LOCAL)];
	}
	else if (type >= ARGUMENT_TIMER) {
		return &pScript->m_anLocalVariables[NUM_LOCAL_VARS + 8 + (type - ARGUMENT_TIMER)];
	}
	script_assert(false && "wrong type for variable");
	return &pScript->m_anLocalVariables[pScript->m_nLocalsPointer + (type - ARGUMENT_LOCAL)];
}

int32 *CRunningScript::GetPointerToScriptVariable(uint32* pIp, int16 type)
{
	return ::GetPointerToScriptVariable(this, pIp);
}

int CTheScripts::GetSaveVarIndex(int var)
{
	for (int i = 0; i < NumSaveVars; i++) {
		if (SavedVarIndices[i] == var)
			return i;
	}
	return -1;
}

void CRunningScript::Init()
{
#ifdef __PS3__
	if (pScriptBroken == this)
		pScriptBroken = nil;	// a new script in this slot
#endif
	sprintf(m_abScriptName, "id%02i", m_nId);
	next = prev = nil;
	SetIP(0);
	for (int i = 0; i < MAX_STACK_DEPTH; i++)
		m_anStack[i] = 0;
	m_nStackPointer = 0;
	m_nWakeTime = 0;
	m_bIsActive = false;
	m_bCondResult = false;
	m_bIsMissionScript = false;
	m_bSkipWakeTime = false;
	for (int i = 0; i < NUM_LOCAL_VARS + 8 + NUM_TIMERS; i++)
		m_anLocalVariables[i] = 0;
	m_nAndOrState = 0;
	m_bNotFlag = false;
	m_bDeatharrestEnabled = true;
	m_bDeatharrestExecuted = false;
	m_bMissionFlag = false;
	m_nLocalsPointer = 0;
}

void CTheScripts::Shutdown()
{
	if (gScriptsFile != -1) {
		CFileMgr::CloseFile(gScriptsFile);
		gScriptsFile = -1;
	}
	if (ScriptSpace) {
		base::cMainMemoryManager::Instance()->Free(ScriptSpace);
		ScriptSpace = nil;
		FSDestroyedFlag = false;
		OnAMissionFlag = 0;
	}
}

#ifdef USE_DEBUG_SCRIPT_LOADER
int CTheScripts::ScriptToLoad = 0;

int CTheScripts::OpenScript()
{
	CFileMgr::ChangeDir("\\");
	switch (ScriptToLoad) {
	case 0: return CFileMgr::OpenFile("DATA\\main.scm", "rb");
	case 1: return CFileMgr::OpenFile("DATA\\freeroam_lcs.scm", "rb");
	case 2: return CFileMgr::OpenFile("DATA\\main_d.scm", "rb");
	}
	return CFileMgr::OpenFile("DATA\\main.scm", "rb");
}
#endif

#ifdef __PS3__
extern bool gPS3PauseMenuDisabled;
#endif
bool CTheScripts::Init(bool loaddata)
{
#ifdef __PS3__
	gPS3PauseMenuDisabled = false;	// a load or a new game starts with it off; main.scm sets it
#endif
	bool retval = false;
	printf("CTheScripts::Init\n");
	pActiveScripts = pIdleScripts = nil;
	for (int i = 0; i < MAX_NUM_SCRIPTS; i++){
		ScriptsArray[i].Init();
		ScriptsArray[i].AddScriptToList(&pIdleScripts);
	}
	MissionCleanUp.Init();
	UpsideDownCars.Init();
	StuckCars.Init();
	StoreVehicleIndex = -1;
	StoreVehicleWasRandom = true;
	OnAMissionFlag = 0;
	LastMissionPassedTime = (uint32)-1;
	for (int i = 0; i < MAX_NUM_COLLECTIVES; i++) {
		CollectiveArray[i].colIndex = -1;
		CollectiveArray[i].pedIndex = 0;
	}
	NextFreeCollectiveIndex = 0;
	LastRandomPedId = -1;
	for (int i = 0; i < MAX_NUM_USED_OBJECTS; i++){
		memset(&UsedObjectArray[i].name, 0, sizeof(UsedObjectArray[i].name));
		UsedObjectArray[i].index = 0;
	}
#if defined FIX_BUGS || (!defined GTA_PS2 && !defined GTA_PSP)
	for (base::cSList<script_corona>::tSItem* i = CTheScripts::mCoronas.first; i;) {
		base::cSList<script_corona>::tSItem* next = i->next;
		delete i;
		i = next;
	}
	CTheScripts::mCoronas.first = nil;
#endif
	NumberOfUsedObjects = 0;
	ResetLCSWorldBuildingSwaps();
	if (ScriptSpace)
		Shutdown();
#ifdef USE_DEBUG_SCRIPT_LOADER
	// glfwGetKey doesn't work because of CGame::Initialise is blocking
	CPad::UpdatePads();
	if (CPad::GetPad(0)->GetChar('G')) ScriptToLoad = 0;
	if (CPad::GetPad(0)->GetChar('R')) ScriptToLoad = 1;
	if (CPad::GetPad(0)->GetChar('D')) ScriptToLoad = 2;
	int mainf = OpenScript();
#else
	CFileMgr::SetDir("DATA");
	int mainf = CFileMgr::OpenFile("main.scm", "rb");
#endif
	CFileMgr::Read(mainf, (char*)&MainScriptSize, sizeof(MainScriptSize));
	int nLargestMissionSize = 0;
	CFileMgr::Read(mainf, (char*)&nLargestMissionSize, sizeof(nLargestMissionSize));
#ifdef BIGENDIAN
	// main.scm is little-endian (its header, and the global variables below)
	MainScriptSize = LittleU32(MainScriptSize);
	nLargestMissionSize = LittleU32((uint32)nLargestMissionSize);
#endif
	if (!cSmallHeap::msInstance.IsLocked())
		cSmallHeap::msInstance.Lock();
	ScriptSpace = (uint8*)base::cMainMemoryManager::Instance()->Allocate(MainScriptSize + nLargestMissionSize);
	memset(ScriptSpace, 0, MainScriptSize + nLargestMissionSize);
	CFileMgr::Read(mainf, (char*)ScriptSpace, MainScriptSize);
#ifdef BIGENDIAN
	// The script code is read byte by byte (Read4BytesFromScript...), but the
	// global variables (from offset 8 up to the first jump's target) are
	// accessed as int32/float in place: keep them in host order.
	{
		int32 varSpace = GetSizeOfVariableSpace();
		for (int32 i = 8; i + 4 <= varSpace && (uint32)(i + 4) <= MainScriptSize; i += 4)
			*(uint32*)&ScriptSpace[i] = LittleU32(*(uint32*)&ScriptSpace[i]);
	}
#endif
#ifdef __PS3__
	PS3_Logf("[script] main.scm: main part %u bytes, largest mission %d bytes, variables %d bytes",
	         MainScriptSize, nLargestMissionSize, GetSizeOfVariableSpace());
#endif
	gScriptsFile = mainf;
	CFileMgr::SetDir("");
	ReadObjectNamesFromScript();
	UpdateObjectIndices();
	bAlreadyRunningAMissionScript = false;
	bUsingAMultiScriptFile = true;
	for (int i = 0; i < MAX_NUM_MISSION_SCRIPTS; i++)
		MultiScriptArray[i] = 0;
	NumberOfExclusiveMissionScripts = 0;
	NumberOfMissionScripts = 0;
	LargestMissionScriptSize = 0;
	ReadMultiScriptFileOffsetsFromScript();
	FailCurrentMission = 0;
	DbgFlag = false;
	//NumScriptDebugLines = 0;
	RiotIntensity = 0;
	bPlayerHasMetDebbieHarry = false;
	bPlayerIsInTheStatium = false;
	for (int i = 0; i < MAX_NUM_SCRIPT_SPHERES; i++){
		ScriptSphereArray[i].m_bInUse = false;
		ScriptSphereArray[i].m_Index = 1;
		ScriptSphereArray[i].m_Id = 0;
		ScriptSphereArray[i].m_vecCenter = CVector(0.0f, 0.0f, 0.0f);
		ScriptSphereArray[i].m_fRadius = 0.0f;
	}
	for (int i = 0; i < MAX_NUM_INTRO_TEXT_LINES; i++){
		IntroTextLines[i].Reset();
	}
	NumberOfIntroTextLinesThisFrame = 0;
	UseTextCommands = 0;
	for (int i = 0; i < MAX_NUM_INTRO_RECTANGLES; i++){
		IntroRectangles[i].m_bIsUsed = false;
		IntroRectangles[i].m_bBeforeFade = false;
		IntroRectangles[i].m_nTextureId = -1;
		IntroRectangles[i].m_sRect = CRect(0.0f, 0.0f, 0.0f, 0.0f);
		IntroRectangles[i].m_sColor = CRGBA(255, 255, 255, 255);
	}
	NumberOfIntroRectanglesThisFrame = 0;
	//RemoveScriptTextureDictionary(); // TODO(LCS) - probably not needed
	for (int i = 0; i < MAX_NUM_BUILDING_SWAPS; i++){
		BuildingSwapArray[i].m_pBuilding = nil;
		BuildingSwapArray[i].m_nNewModel = -1;
		BuildingSwapArray[i].m_nOldModel = -1;
		BuildingSwapArray[i].m_nPoolType = 0;
		BuildingSwapArray[i].m_nPoolHandle = 0;
	}
	for (int i = 0; i < MAX_NUM_INVISIBILITY_SETTINGS; i++) {
		InvisibilitySettingArray[i] = nil;
		InvisibilitySettingPoolType[i] = 0;
		InvisibilitySettingPoolHandle[i] = 0;
	}
	if (loaddata) {
		printf("loaddata = true\n");
		retval = GenericLoad();
		RecoverLCSWorldBuildingSwapState();
	}
	for (int i = 0; i < MAX_ALLOWED_COLLISIONS; i++)
		AllowedCollision[i] = 0;

#ifdef USE_ADVANCED_SCRIPT_DEBUG_OUTPUT
	LogAfterScriptInitializing();
#endif
#ifdef USE_MISSION_REPLAY_OVERRIDE_FOR_NON_MOBILE_SCRIPT
	AlreadySavedGame = false;
#endif
	return retval;
}

/*
void CTheScripts::RemoveScriptTextureDictionary()
{
	for (int i = 0; i < ARRAY_SIZE(CTheScripts::ScriptSprites); i++)
		CTheScripts::ScriptSprites[i].Delete();
	int slot = CTxdStore::FindTxdSlot("script");
	if (slot != -1)
		CTxdStore::RemoveTxd(slot);
}
*/

void CRunningScript::RemoveScriptFromList(CRunningScript** ppScript)
{
	if (prev)
		prev->next = next;
	else
		*ppScript = next;
	if (next)
		next->prev = prev;
}

void CRunningScript::AddScriptToList(CRunningScript** ppScript)
{
	next = *ppScript;
	prev = nil;
	if (*ppScript)
		(*ppScript)->prev = this;
	*ppScript = this;
}

CRunningScript* CTheScripts::StartNewScript(uint32 ip)
{
	CRunningScript* pNew = pIdleScripts;
	script_assert(pNew);
	pNew->RemoveScriptFromList(&pIdleScripts);
	pNew->m_nId = NextProcessId++;
	pNew->Init();
	pNew->SetIP(ip);
	pNew->AddScriptToList(&pActiveScripts);
	pNew->m_bIsActive = true;
	return pNew;
}

void CTheScripts::Process()
{
	if (CReplay::IsPlayingBack())
		return;
	if (!ScriptSpace)
		return;
#ifdef __PS3__
	PS3_ScriptReport();
	uint64 ps3P0 = PS3_Tb();
#endif
	CommandsExecuted = 0;
	ScriptsUpdated = 0;
	float timeStep = CTimer::GetTimeStepInMilliseconds();
	UpsideDownCars.UpdateTimers();
	StuckCars.Process();
	MissionCleanUp.CheckIfCollisionHasLoadedForMissionObjects();
	DrawScriptSpheres();
	if (FailCurrentMission)
		--FailCurrentMission;
	if (UseTextCommands){
		for (int i = 0; i < MAX_NUM_INTRO_TEXT_LINES; i++)
			IntroTextLines[i].Reset();
		NumberOfIntroTextLinesThisFrame = 0;
		for (int i = 0; i < MAX_NUM_INTRO_RECTANGLES; i++){
			IntroRectangles[i].m_bIsUsed = false;
			IntroRectangles[i].m_bBeforeFade = false;
		}
		NumberOfIntroRectanglesThisFrame = 0;
		if (UseTextCommands == 1)
			UseTextCommands = 0;
	}

#ifdef MISSION_REPLAY
	static uint32 TimeToWaitTill;
	static bool AlreadyResetHealth;
	switch (AllowMissionReplay) {
	case MISSION_RETRY_STAGE_START_PROCESSING:
		AllowMissionReplay = MISSION_RETRY_STAGE_WAIT_FOR_DELAY;
		TimeToWaitTill = CTimer::GetTimeInMilliseconds() + (AddExtraDeathDelay() > 1000 ? 4000 : 2500);
		break;
	case MISSION_RETRY_STAGE_WAIT_FOR_DELAY:
		if (TimeToWaitTill < CTimer::GetTimeInMilliseconds())
			AllowMissionReplay = MISSION_RETRY_STAGE_WAIT_FOR_MENU;
		break;
	case MISSION_RETRY_STAGE_WAIT_FOR_MENU:
		AllowMissionReplay = MISSION_RETRY_STAGE_WAIT_FOR_USER;
		RetryMission(MISSION_RETRY_TYPE_SUGGEST_TO_PLAYER);
		break;
	case MISSION_RETRY_STAGE_START_RESTARTING:
		AllowMissionReplay = MISSION_RETRY_STAGE_WAIT_FOR_TIMER_AFTER_RESTART;
		AlreadyResetHealth = false;
		TimeToWaitTill = CTimer::GetTimeInMilliseconds() + 500;
		break;
	case MISSION_RETRY_STAGE_WAIT_FOR_TIMER_AFTER_RESTART:
		if (!AlreadyResetHealth) {
			AlreadyResetHealth = true;
			CPlayerPed* pPlayerPed = FindPlayerPed();
			if (pPlayerPed) {
				CPlayerInfo* pPlayerInfo = pPlayerPed->GetPlayerInfoForThisPlayerPed();
				if (pPlayerInfo)
					pPlayerPed->m_fHealth = pPlayerInfo->m_nMaxHealth;
			}
		}
		if (TimeToWaitTill < CTimer::GetTimeInMilliseconds()) {
			AllowMissionReplay = MISSION_RETRY_STAGE_NORMAL;
			return;
		}
		break;
	}
	if (WaitForMissionActivate) {
		if (WaitForMissionActivate > CTimer::GetTimeInMilliseconds())
			return;
		WaitForMissionActivate = 0;
		WaitForSave = CTimer::GetTimeInMilliseconds() + 3000;
	}
	if (WaitForSave && WaitForSave > CTimer::GetTimeInMilliseconds())
		WaitForSave = 0;
#endif

#ifdef USE_ADVANCED_SCRIPT_DEBUG_OUTPUT
	LogBeforeScriptProcessing();
#endif

	CRunningScript* script = pActiveScripts;
	InTheScripts = true;
#ifdef __PS3__
	uint64 ps3P1 = PS3_Tb();
	ps3SegTicks[0] += ps3P1 - ps3P0;
#endif
	while (script != nil){
		CRunningScript* next = script->GetNext();
		++ScriptsUpdated;
#ifdef __PS3__
		uint64 ps3S0 = PS3_Tb();
		uint32 ps3C0 = ps3AllTotal;
#endif
		script->UpdateTimers(timeStep);
		script->Process();
#ifdef __PS3__
		PS3_ScriptProcessTime(script, PS3_Tb() - ps3S0, ps3AllTotal - ps3C0);
#endif
#ifdef __PS3__
		if (pScriptBroken != nil && pScriptBroken == script) {
			// as TERMINATE_THIS_SCRIPT, plus what a mission's cleanup does
			bool mission = script->m_bMissionFlag;
			script->RemoveScriptFromList(&pActiveScripts);
			script->AddScriptToList(&pIdleScripts);
			script->m_bIsActive = false;
			if (mission) {
				bAlreadyRunningAMissionScript = false;
				MissionCleanUp.Process();
				if (OnAMissionFlag)
					*(int32*)&ScriptSpace[OnAMissionFlag] = 0;
			}
			pScriptBroken = nil;
		}
		pScriptRunning = nil;
#endif
		script = next;
		if (script && !script->m_bIsActive)
			script = nil;
	}
	InTheScripts = false;
#ifdef __PS3__
	uint64 ps3P2 = PS3_Tb();
	ps3SegTicks[1] += ps3P2 - ps3P1;
#endif
	for (base::cSList<script_corona>::tSItem* i = CTheScripts::mCoronas.first; i; i = i->next) {
		CCoronas::RegisterCorona((uint32)(uintptr)i, i->item.r, i->item.g, i->item.b, 255, CVector(i->item.x, i->item.y, i->item.z),
			-i->item.size, 450.0f, i->item.type, i->item.flareType, 1, 0, 0, 0.0f);
	}
	DbgFlag = false;
#ifdef __PS3__
	{
		uint64 ps3P3 = PS3_Tb();
		ps3SegTicks[2] += ps3P3 - ps3P2;
		ps3SegTicks[3] += ps3P3 - ps3P0;
	}
#endif

#ifdef USE_ADVANCED_SCRIPT_DEBUG_OUTPUT
	LogAfterScriptProcessing();
#endif
}

CRunningScript* CTheScripts::StartTestScript()
{
	return StartNewScript(0);
}

bool CTheScripts::IsPlayerOnAMission()
{
	return OnAMissionFlag && *(int32*)&ScriptSpace[OnAMissionFlag] == 1;
}

void CRunningScript::Process()
{
#ifdef USE_ADVANCED_SCRIPT_DEBUG_OUTPUT
	LogOnStartProcessing();
#endif
	pCurrent = this;
	if (m_bIsMissionScript)
		DoDeatharrestCheck();
	if (m_bMissionFlag && CTheScripts::FailCurrentMission == 1 && m_nStackPointer == 1)
		SetIP(m_anStack[--m_nStackPointer]);
	if (CTimer::GetTimeInMilliseconds() >= m_nWakeTime){
		while (!ProcessOneCommand())
			;
		return;
	}
	if (!m_bSkipWakeTime)
		return;
	if (!CPad::GetPad(0)->GetCrossJustDown())
		return;
	m_nWakeTime = 0;
	for (int i = 0; i < NUMBIGMESSAGES; i++){
		if (CMessages::BIGMessages[i].m_Stack[0].m_pText != nil)
			CMessages::BIGMessages[i].m_Stack[0].m_nStartTime = 0;
	}
	if (CMessages::BriefMessages[0].m_pText != nil)
		CMessages::BriefMessages[0].m_nStartTime = 0;
}

#ifdef __PS3__
// patch 39: the scripts were written for 30 fps (PS2/PSP): a "WAIT 0" loop
// runs once a frame, and what it adds or takes every pass went twice as fast
// at 60 fps (Ren: the taxi TIP bar, TAXIDR's 46@ -= 0.25 a frame). An
// ADD/SUB of a constant that the same script line does on consecutive
// frames (3 in a row) to a variable nothing else touched in between is such
// a per-frame step: scaled by the frame time against 30 fps' (x0.5 at 60;
// time scale left out, slow motion ran the scripts at the same pace). Ints
// keep the remainder for the next frame. Once-only adds (a coordinate
// offset, a score) and values reset every frame aren't touched.
struct PS3ScriptStep {
	const CRunningScript *script;
	uint32 ip;
	uint32 frame;
	int32 value;	// the variable after our last step
	int32 streak;
	float frac;
	bool logged;
};
static PS3ScriptStep ps3Steps[128];
static int32 ps3StepsLogged;

static float
PS3_ScriptFrameScale(void)
{
	float ts = CTimer::GetTimeScale();
	float f = CTimer::GetTimeStep() / (ts > 0.01f ? ts : 0.01f) / CTimer::GetDefaultTimeStep();
	return f < 0.05f ? 0.05f : f > 1.0f ? 1.0f : f;
}

static PS3ScriptStep*
PS3_ScriptStepSite(const CRunningScript *s, uint32 ip, const int32 *ptr)
{
	PS3ScriptStep &st = ps3Steps[(ip ^ ((uintptr)s >> 4)) & 127];
	uint32 frame = CTimer::GetFrameCounter();
	if(st.script != s || st.ip != ip){
		st.script = s;
		st.ip = ip;
		st.streak = 0;
		st.frac = 0.0f;
		st.logged = false;
	}else if(st.frame + 1 == frame && *ptr == st.value)
		st.streak++;
	else if(st.frame != frame || *ptr != st.value)
		st.streak = 0;
	st.frame = frame;
	return &st;
}

static void
PS3_ScriptStepLog(PS3ScriptStep *st, const CRunningScript *s, float c, float f)
{
	if(st->logged || ps3StepsLogged >= 40)
		return;
	st->logged = true;
	ps3StepsLogged++;
	PS3_Logf("[fps] script %s at %u: %g every frame, x%.2f (the scripts' 30 fps)", s->m_abScriptName, st->ip, c, f);
}

// the step to add this frame (c: what the line says)
static float
PS3_ScriptStepF(const CRunningScript *s, uint32 ip, int32 *ptr, float c)
{
	PS3ScriptStep *st = PS3_ScriptStepSite(s, ip, ptr);
	float f = PS3_ScriptFrameScale();
	if(st->streak >= 2 && f < 0.95f){
		PS3_ScriptStepLog(st, s, c, f);
		c *= f;
	}
	return c;
}
static void
PS3_ScriptStepDoneF(const CRunningScript *s, uint32 ip, int32 *ptr)
{
	ps3Steps[(ip ^ ((uintptr)s >> 4)) & 127].value = *ptr;
}
static int32
PS3_ScriptStepI(const CRunningScript *s, uint32 ip, int32 *ptr, int32 c)
{
	PS3ScriptStep *st = PS3_ScriptStepSite(s, ip, ptr);
	float f = PS3_ScriptFrameScale();
	if(st->streak >= 2 && f < 0.95f){
		PS3_ScriptStepLog(st, s, (float)c, f);
		st->frac += c * f;
		int32 n = (int32)st->frac;
		st->frac -= n;
		return n;
	}
	return c;
}
#define PS3_STEP_IP (m_nIp - 2)
#endif

int8 CRunningScript::ProcessOneCommand()
{
	int8 retval = -1;
#ifdef __PS3__
	if (pScriptBroken == this)
		return 1;	// stopped at the end of CTheScripts::Process
	pScriptRunning = this;
	scriptTrace[scriptTraceHead].script = this;
	scriptTrace[scriptTraceHead].ip = m_nIp;
	scriptTrace[scriptTraceHead].op = CTheScripts::ScriptSpace[m_nIp] | CTheScripts::ScriptSpace[m_nIp + 1] << 8;
	scriptTraceHead = (scriptTraceHead + 1) % 32;
#endif
	++CTheScripts::CommandsExecuted;
	int32 command = (uint16)CTheScripts::Read2BytesFromScript(&m_nIp);
	m_bNotFlag = (command & 0x8000);
	command &= 0x7FFF;
#ifdef __PS3__
	PS3_ScriptCountOp(this, command);
	PS3_ScriptCrumb(m_abScriptName, command, m_nIp - 2);
#endif
#ifdef USE_ADVANCED_SCRIPT_DEBUG_OUTPUT
	LogBeforeProcessingCommand(command);
#endif
#ifdef __PS3__
	uint64 ps3T0 = PS3_Tb();
#endif
	if (command < 100)
		retval = ProcessCommands0To99(command);
	else if (command < 200)
		retval = ProcessCommands100To199(command);
	else if (command < 305)
		retval = ProcessCommands200To299(command);
	else if (command < 405)
		retval = ProcessCommands300To399(command);
	else if (command < 505)
		retval = ProcessCommands400To499(command);
	else if (command < 605)
		retval = ProcessCommands500To599(command);
	else if (command < 705)
		retval = ProcessCommands600To699(command);
	else if (command < 805)
		retval = ProcessCommands700To799(command);
	else if (command < 905)
		retval = ProcessCommands800To899(command);
	else if (command < 1005)
		retval = ProcessCommands900To999(command);
	else if (command < 1105)
		retval = ProcessCommands1000To1099(command);
	else if (command < 1205)
		retval = ProcessCommands1100To1199(command);
	else if (command < 1305)
		retval = ProcessCommands1200To1299(command);
	else if (command < 1405)
		retval = ProcessCommands1300To1399(command);
	else if (command < 1497)
		retval = ProcessCommands1400To1499(command);
	else if (command < 1600)
		retval = ProcessCommands1500To1599(command);
	else if (command < 1700)
		retval = ProcessCommands1600To1699(command);
	else
		script_assert(false);
#ifdef __PS3__
	PS3_ScriptTime(this, command, PS3_Tb() - ps3T0);
#endif
#ifdef USE_MISSION_REPLAY_OVERRIDE_FOR_NON_MOBILE_SCRIPT
	if (!AlreadySavedGame)
#endif
	{
#ifdef USE_ADVANCED_SCRIPT_DEBUG_OUTPUT
		LogAfterProcessingCommand(command);
#elif defined USE_BASIC_SCRIPT_DEBUG_OUTPUT
		if (m_bMissionFlag) {
			char tmp[128];
			sprintf(tmp, "Comm %d Cmp %d", command, m_bCondResult);
			CDebug::DebugAddText(tmp);
		}
#endif
	}
	return retval;
}

int8 CRunningScript::ProcessCommands0To99(int32 command)
{
	float *fScriptVar1;
	int *nScriptVar1;
	switch (command) {
		/*
	case COMMAND_NOP:
		return 0;
		*/
	case COMMAND_WAIT:
		CollectParameters(&m_nIp, 1);
		m_nWakeTime = CTimer::GetTimeInMilliseconds() +
			(CMessages::ConsumeMissionTitleScriptWait() ? 1500 : GET_INTEGER_PARAM(0));
		m_bSkipWakeTime = false;
		return 1;
	case COMMAND_GOTO:
		CollectParameters(&m_nIp, 1);
		SetIP(GET_INTEGER_PARAM(0) >= 0 ? GET_INTEGER_PARAM(0) : CTheScripts::MainScriptSize - GET_INTEGER_PARAM(0));
		/* Known issue: GOTO to 0. It might have been "better" to use > instead of >= */
		/* simply because it never makes sense to jump to start of the script */
		/* but jumping to start of a custom mission is an issue for simple mission-like scripts */
		/* However, it's not an issue for actual mission scripts, because they follow a structure */
		/* and never start with a loop. */
		return 0;
	case COMMAND_SHAKE_CAM:
		CollectParameters(&m_nIp, 1);
		CamShakeNoPos(&TheCamera, GET_INTEGER_PARAM(0) / 1000.0f);
		return 0;
	case COMMAND_SET_VAR_INT:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
		*ptr = GET_INTEGER_PARAM(0);
		return 0;
	}
	case COMMAND_SET_VAR_FLOAT:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
		*(float*)ptr = GET_FLOAT_PARAM(0);
		return 0;
	}
	case COMMAND_SET_LVAR_INT:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
		*ptr = GET_INTEGER_PARAM(0);
		return 0;
	}
	case COMMAND_SET_LVAR_FLOAT:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
		*(float*)ptr = GET_FLOAT_PARAM(0);
		return 0;
	}
	case COMMAND_ADD_VAL_TO_INT_VAR:
	{
#ifdef __PS3__
		uint32 ps3Ip = PS3_STEP_IP;
#endif
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
#ifdef __PS3__
		*ptr += PS3_ScriptStepI(this, ps3Ip, ptr, GET_INTEGER_PARAM(0));
		PS3_ScriptStepDoneF(this, ps3Ip, ptr);
#else
		*ptr += GET_INTEGER_PARAM(0);
#endif
		return 0;
	}
	case COMMAND_ADD_VAL_TO_FLOAT_VAR:
	{
#ifdef __PS3__
		uint32 ps3Ip = PS3_STEP_IP;
#endif
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
#ifdef __PS3__
		*(float*)ptr += PS3_ScriptStepF(this, ps3Ip, ptr, GET_FLOAT_PARAM(0));
		PS3_ScriptStepDoneF(this, ps3Ip, ptr);
#else
		*(float*)ptr += GET_FLOAT_PARAM(0);
#endif
		return 0;
	}
	case COMMAND_ADD_VAL_TO_INT_LVAR:
	{
#ifdef __PS3__
		uint32 ps3Ip = PS3_STEP_IP;
#endif
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
#ifdef __PS3__
		*ptr += PS3_ScriptStepI(this, ps3Ip, ptr, GET_INTEGER_PARAM(0));
		PS3_ScriptStepDoneF(this, ps3Ip, ptr);
#else
		*ptr += GET_INTEGER_PARAM(0);
#endif
		return 0;
	}
	case COMMAND_ADD_VAL_TO_FLOAT_LVAR:
	{
#ifdef __PS3__
		uint32 ps3Ip = PS3_STEP_IP;
#endif
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
#ifdef __PS3__
		*(float*)ptr += PS3_ScriptStepF(this, ps3Ip, ptr, GET_FLOAT_PARAM(0));
		PS3_ScriptStepDoneF(this, ps3Ip, ptr);
#else
		*(float*)ptr += GET_FLOAT_PARAM(0);
#endif
		return 0;
	}
	case COMMAND_SUB_VAL_FROM_INT_VAR:
	{
#ifdef __PS3__
		uint32 ps3Ip = PS3_STEP_IP;
#endif
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
#ifdef __PS3__
		*ptr -= PS3_ScriptStepI(this, ps3Ip, ptr, GET_INTEGER_PARAM(0));
		PS3_ScriptStepDoneF(this, ps3Ip, ptr);
#else
		*ptr -= GET_INTEGER_PARAM(0);
#endif
		return 0;
	}
	case COMMAND_SUB_VAL_FROM_FLOAT_VAR:
	{
#ifdef __PS3__
		uint32 ps3Ip = PS3_STEP_IP;
#endif
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
#ifdef __PS3__
		*(float*)ptr -= PS3_ScriptStepF(this, ps3Ip, ptr, GET_FLOAT_PARAM(0));
		PS3_ScriptStepDoneF(this, ps3Ip, ptr);
#else
		*(float*)ptr -= GET_FLOAT_PARAM(0);
#endif
		return 0;
	}
	case COMMAND_SUB_VAL_FROM_INT_LVAR:
	{
#ifdef __PS3__
		uint32 ps3Ip = PS3_STEP_IP;
#endif
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
#ifdef __PS3__
		*ptr -= PS3_ScriptStepI(this, ps3Ip, ptr, GET_INTEGER_PARAM(0));
		PS3_ScriptStepDoneF(this, ps3Ip, ptr);
#else
		*ptr -= GET_INTEGER_PARAM(0);
#endif
		return 0;
	}
	case COMMAND_SUB_VAL_FROM_FLOAT_LVAR:
	{
#ifdef __PS3__
		uint32 ps3Ip = PS3_STEP_IP;
#endif
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
#ifdef __PS3__
		*(float*)ptr -= PS3_ScriptStepF(this, ps3Ip, ptr, GET_FLOAT_PARAM(0));
		PS3_ScriptStepDoneF(this, ps3Ip, ptr);
#else
		*(float*)ptr -= GET_FLOAT_PARAM(0);
#endif
		return 0;
	}
	case COMMAND_MULT_INT_VAR_BY_VAL:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
		*ptr *= GET_INTEGER_PARAM(0);
		return 0;
	}
	case COMMAND_MULT_FLOAT_VAR_BY_VAL:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
		*(float*)ptr *= GET_FLOAT_PARAM(0);
		return 0;
	}
	case COMMAND_MULT_INT_LVAR_BY_VAL:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
		*ptr *= GET_INTEGER_PARAM(0);
		return 0;
	}
	case COMMAND_MULT_FLOAT_LVAR_BY_VAL:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
		*(float*)ptr *= GET_FLOAT_PARAM(0);
		return 0;
	}
	case COMMAND_DIV_INT_VAR_BY_VAL:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
		*ptr /= GET_INTEGER_PARAM(0);
		return 0;
	}
	case COMMAND_DIV_FLOAT_VAR_BY_VAL:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
		*(float*)ptr /= GET_FLOAT_PARAM(0);
		return 0;
	}
	case COMMAND_DIV_INT_LVAR_BY_VAL:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
		*ptr /= GET_INTEGER_PARAM(0);
		return 0;
	}
	case COMMAND_DIV_FLOAT_LVAR_BY_VAL:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
		*(float*)ptr /= GET_FLOAT_PARAM(0);
		return 0;
	}
	case COMMAND_IS_INT_VAR_GREATER_THAN_NUMBER:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
		UpdateCompareFlag(*ptr > GET_INTEGER_PARAM(0));
		return 0;
	}
	case COMMAND_IS_INT_LVAR_GREATER_THAN_NUMBER:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
		UpdateCompareFlag(*ptr > GET_INTEGER_PARAM(0));
		return 0;
	}
	case COMMAND_IS_NUMBER_GREATER_THAN_INT_VAR:
	{
		CollectParameters(&m_nIp, 1);
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		UpdateCompareFlag(GET_INTEGER_PARAM(0) > *ptr);
		return 0;
	}
	case COMMAND_IS_NUMBER_GREATER_THAN_INT_LVAR:
	{
		CollectParameters(&m_nIp, 1);
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		UpdateCompareFlag(GET_INTEGER_PARAM(0) > *ptr);
		return 0;
	}
	case COMMAND_IS_INT_VAR_GREATER_THAN_INT_VAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		UpdateCompareFlag(*ptr1 > *ptr2);
		return 0;
	}
	case COMMAND_IS_INT_LVAR_GREATER_THAN_INT_VAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		UpdateCompareFlag(*ptr1 > *ptr2);
		return 0;
	}
	case COMMAND_IS_INT_VAR_GREATER_THAN_INT_LVAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		UpdateCompareFlag(*ptr1 > *ptr2);
		return 0;
	}
	case COMMAND_IS_INT_LVAR_GREATER_THAN_INT_LVAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		UpdateCompareFlag(*ptr1 > *ptr2);
		return 0;
	}
	case COMMAND_IS_FLOAT_VAR_GREATER_THAN_NUMBER:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
		UpdateCompareFlag(*(float*)ptr > GET_FLOAT_PARAM(0));
		return 0;
	}
	case COMMAND_IS_FLOAT_LVAR_GREATER_THAN_NUMBER:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
		UpdateCompareFlag(*(float*)ptr > GET_FLOAT_PARAM(0));
		return 0;
	}
	case COMMAND_IS_NUMBER_GREATER_THAN_FLOAT_VAR:
	{
		CollectParameters(&m_nIp, 1);
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		UpdateCompareFlag(GET_FLOAT_PARAM(0) > *(float*)ptr);
		return 0;
	}
	case COMMAND_IS_NUMBER_GREATER_THAN_FLOAT_LVAR:
	{
		CollectParameters(&m_nIp, 1);
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		UpdateCompareFlag(GET_FLOAT_PARAM(0) > *(float*)ptr);
		return 0;
	}
	case COMMAND_IS_FLOAT_VAR_GREATER_THAN_FLOAT_VAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		UpdateCompareFlag(*(float*)ptr1 > *(float*)ptr2);
		return 0;
	}
	case COMMAND_IS_FLOAT_LVAR_GREATER_THAN_FLOAT_VAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		UpdateCompareFlag(*(float*)ptr1 > *(float*)ptr2);
		return 0;
	}
	case COMMAND_IS_FLOAT_VAR_GREATER_THAN_FLOAT_LVAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		UpdateCompareFlag(*(float*)ptr1 > *(float*)ptr2);
		return 0;
	}
	case COMMAND_IS_FLOAT_LVAR_GREATER_THAN_FLOAT_LVAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		UpdateCompareFlag(*(float*)ptr1 > *(float*)ptr2);
		return 0;
	}
	case COMMAND_IS_INT_VAR_GREATER_OR_EQUAL_TO_NUMBER:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
		UpdateCompareFlag(*ptr >= GET_INTEGER_PARAM(0));
		return 0;
	}
	case COMMAND_IS_INT_LVAR_GREATER_OR_EQUAL_TO_NUMBER:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
		UpdateCompareFlag(*ptr >= GET_INTEGER_PARAM(0));
		return 0;
	}
	case COMMAND_IS_NUMBER_GREATER_OR_EQUAL_TO_INT_VAR:
	{
		CollectParameters(&m_nIp, 1);
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		UpdateCompareFlag(GET_INTEGER_PARAM(0) >= *ptr);
		return 0;
	}
	case COMMAND_IS_NUMBER_GREATER_OR_EQUAL_TO_INT_LVAR:
	{
		CollectParameters(&m_nIp, 1);
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		UpdateCompareFlag(GET_INTEGER_PARAM(0) >= *ptr);
		return 0;
	}
	case COMMAND_IS_INT_VAR_GREATER_OR_EQUAL_TO_INT_VAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		UpdateCompareFlag(*ptr1 >= *ptr2);
		return 0;
	}
	case COMMAND_IS_INT_LVAR_GREATER_OR_EQUAL_TO_INT_VAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		UpdateCompareFlag(*ptr1 >= *ptr2);
		return 0;
	}
	case COMMAND_IS_INT_VAR_GREATER_OR_EQUAL_TO_INT_LVAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		UpdateCompareFlag(*ptr1 >= *ptr2);
		return 0;
	}
	case COMMAND_IS_INT_LVAR_GREATER_OR_EQUAL_TO_INT_LVAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		UpdateCompareFlag(*ptr1 >= *ptr2);
		return 0;
	}
	case COMMAND_IS_FLOAT_VAR_GREATER_OR_EQUAL_TO_NUMBER:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
		UpdateCompareFlag(*(float*)ptr >= GET_FLOAT_PARAM(0));
		return 0;
	}
	case COMMAND_IS_FLOAT_LVAR_GREATER_OR_EQUAL_TO_NUMBER:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
		UpdateCompareFlag(*(float*)ptr >= GET_FLOAT_PARAM(0));
		return 0;
	}
	case COMMAND_IS_NUMBER_GREATER_OR_EQUAL_TO_FLOAT_VAR:
	{
		CollectParameters(&m_nIp, 1);
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		UpdateCompareFlag(GET_FLOAT_PARAM(0) >= *(float*)ptr);
		return 0;
	}
	case COMMAND_IS_NUMBER_GREATER_OR_EQUAL_TO_FLOAT_LVAR:
	{
		CollectParameters(&m_nIp, 1);
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		UpdateCompareFlag(GET_FLOAT_PARAM(0) >= *(float*)ptr);
		return 0;
	}
	case COMMAND_IS_FLOAT_VAR_GREATER_OR_EQUAL_TO_FLOAT_VAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		UpdateCompareFlag(*(float*)ptr1 >= *(float*)ptr2);
		return 0;
	}
	case COMMAND_IS_FLOAT_LVAR_GREATER_OR_EQUAL_TO_FLOAT_VAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		UpdateCompareFlag(*(float*)ptr1 >= *(float*)ptr2);
		return 0;
	}
	case COMMAND_IS_FLOAT_VAR_GREATER_OR_EQUAL_TO_FLOAT_LVAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		UpdateCompareFlag(*(float*)ptr1 >= *(float*)ptr2);
		return 0;
	}
	case COMMAND_IS_FLOAT_LVAR_GREATER_OR_EQUAL_TO_FLOAT_LVAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		UpdateCompareFlag(*(float*)ptr1 >= *(float*)ptr2);
		return 0;
	}
	case COMMAND_IS_INT_VAR_EQUAL_TO_NUMBER:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
		static const int32 LCS_FINALE_CHECKPOINT_GLOBAL = 535;
		if (!CGeneral::faststricmp(m_abScriptName, "SALH5") &&
		    ptr == (int32*)&CTheScripts::ScriptSpace[LCS_FINALE_CHECKPOINT_GLOBAL * sizeof(int32)] &&
		    GET_INTEGER_PARAM(0) == 0 &&
		    FinalMissionMusic::ConsumeLCSFinaleBoatCheckpointRequest())
			*ptr = 1;
		UpdateCompareFlag(*ptr == GET_INTEGER_PARAM(0));
		return 0;
	}
	case COMMAND_IS_INT_LVAR_EQUAL_TO_NUMBER:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
		UpdateCompareFlag(*ptr == GET_INTEGER_PARAM(0));
		return 0;
	}
	case COMMAND_IS_INT_VAR_EQUAL_TO_INT_VAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		UpdateCompareFlag(*ptr1 == *ptr2);
		return 0;
	}
	case COMMAND_IS_INT_VAR_EQUAL_TO_INT_LVAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		UpdateCompareFlag(*ptr1 == *ptr2);
		return 0;
	}
	case COMMAND_IS_INT_LVAR_EQUAL_TO_INT_LVAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		UpdateCompareFlag(*ptr1 == *ptr2);
		return 0;
	}
	//case COMMAND_IS_INT_VAR_NOT_EQUAL_TO_NUMBER:
	//case COMMAND_IS_INT_LVAR_NOT_EQUAL_TO_NUMBER:
	//case COMMAND_IS_INT_VAR_NOT_EQUAL_TO_INT_VAR:
	//case COMMAND_IS_INT_LVAR_NOT_EQUAL_TO_INT_LVAR:
	//case COMMAND_IS_INT_VAR_NOT_EQUAL_TO_INT_LVAR:
	case COMMAND_IS_FLOAT_VAR_EQUAL_TO_NUMBER:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
		UpdateCompareFlag(*(float*)ptr == GET_FLOAT_PARAM(0));
		return 0;
	}
	case COMMAND_IS_FLOAT_LVAR_EQUAL_TO_NUMBER:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
		UpdateCompareFlag(*(float*)ptr == GET_FLOAT_PARAM(0));
		return 0;
	}
	case COMMAND_IS_FLOAT_VAR_EQUAL_TO_FLOAT_VAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		UpdateCompareFlag(*(float*)ptr1 == *(float*)ptr2);
		return 0;
	}
	case COMMAND_IS_FLOAT_VAR_EQUAL_TO_FLOAT_LVAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		UpdateCompareFlag(*(float*)ptr1 == *(float*)ptr2);
		return 0;
	}
	case COMMAND_IS_FLOAT_LVAR_EQUAL_TO_FLOAT_LVAR:
	{
		int32* ptr1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		int32* ptr2 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		UpdateCompareFlag(*(float*)ptr1 == *(float*)ptr2);
		return 0;
	}
	//case COMMAND_IS_FLOAT_VAR_NOT_EQUAL_TO_NUMBER:
	//case COMMAND_IS_FLOAT_LVAR_NOT_EQUAL_TO_NUMBER:
	//case COMMAND_IS_FLOAT_VAR_NOT_EQUAL_TO_FLOAT_VAR:
	//case COMMAND_IS_FLOAT_LVAR_NOT_EQUAL_TO_FLOAT_LVAR:
	//case COMMAND_IS_FLOAT_VAR_NOT_EQUAL_TO_FLOAT_LVAR:
	case COMMAND_GOTO_IF_TRUE:
		CollectParameters(&m_nIp, 1);
		if (m_bCondResult)
			SetIP(GET_INTEGER_PARAM(0) >= 0 ? GET_INTEGER_PARAM(0) : CTheScripts::MainScriptSize - GET_INTEGER_PARAM(0));
		return 0;
	case COMMAND_GOTO_IF_FALSE:
		CollectParameters(&m_nIp, 1);
		if (!m_bCondResult)
			SetIP(GET_INTEGER_PARAM(0) >= 0 ? GET_INTEGER_PARAM(0) : CTheScripts::MainScriptSize - GET_INTEGER_PARAM(0));
		/* Check COMMAND_GOTO note. */
		return 0;
	case COMMAND_TERMINATE_THIS_SCRIPT:
		if (!CGeneral::faststricmp(m_abScriptName, "SALH5"))
			FinalMissionMusic::EndLCSFinale();
		if (m_bMissionFlag)
			CTheScripts::bAlreadyRunningAMissionScript = false;
		RemoveScriptFromList(&CTheScripts::pActiveScripts);
		AddScriptToList(&CTheScripts::pIdleScripts);
		m_bIsActive = false;
#ifdef MISSION_REPLAY
		if (m_bMissionFlag) {
			CPlayerInfo* pPlayerInfo = &CWorld::Players[CWorld::PlayerInFocus];
#if 0 // makeing autosave is pointless and is a bit buggy
			if (pPlayerInfo->m_pPed->GetPedState() != PED_DEAD && pPlayerInfo->m_WBState == WBSTATE_PLAYING && !m_bDeatharrestExecuted)
				SaveGameForPause(SAVE_TYPE_QUICKSAVE);
#endif
			oldTargetX = oldTargetY = 0.0f;
			if (AllowMissionReplay == MISSION_RETRY_STAGE_WAIT_FOR_SCRIPT_TO_TERMINATE)
				AllowMissionReplay = MISSION_RETRY_STAGE_START_PROCESSING;
			// I am fairly sure they forgot to set return value here
		}
#endif
		return 1;
	case COMMAND_START_NEW_SCRIPT:
	{
		CollectParameters(&m_nIp, 1);
		script_assert(GET_INTEGER_PARAM(0) >= 0);
		CRunningScript* pNew = CTheScripts::StartNewScript(GET_INTEGER_PARAM(0));
		CollectParameters(&m_nIp, NUM_LOCAL_VARS, pNew->m_anLocalVariables);
		return 0;
	}
	case COMMAND_GOSUB:
		CollectParameters(&m_nIp, 1);
		script_assert(m_nStackPointer < MAX_STACK_DEPTH);
		m_anStack[m_nStackPointer++] = m_nIp;
		SetIP(GET_INTEGER_PARAM(0) >= 0 ? GET_INTEGER_PARAM(0) : CTheScripts::MainScriptSize - GET_INTEGER_PARAM(0));
		return 0;
	case COMMAND_RETURN:
		ReturnFromGosubOrFunction();
		return 0;
	case COMMAND_LINE:
		CollectParameters(&m_nIp, 6);
		/* Something must have been here */
		return 0;
	case COMMAND_CREATE_PLAYER:
	{
		CollectParameters(&m_nIp, 4);
		int32 index = GET_INTEGER_PARAM(0);
		script_assert(index < NUMPLAYERS);
		printf("&&&&&&&&&&&&&Creating player: %d\n", index);
		if (!CStreaming::HasModelLoaded(MI_PLAYER)) {
			CStreaming::RequestSpecialModel(MI_PLAYER, "player", STREAMFLAGS_DONT_REMOVE | STREAMFLAGS_DEPENDENCY);
			CStreaming::LoadAllRequestedModels(false);
		}
		CPlayerPed::SetupPlayerPed(index);
		CWorld::Players[index].m_pPed->CharCreatedBy = MISSION_CHAR;
		CPlayerPed::DeactivatePlayerPed(index);
		CVector pos = GET_VECTOR_PARAM(1);
		if (pos.z <= MAP_Z_LOW_LIMIT)
			pos.z = CWorld::FindGroundZForCoord(pos.x, pos.y);
		pos.z += CWorld::Players[index].m_pPed->GetDistanceFromCentreOfMassToBaseOfModel();
		CWorld::Players[index].m_pPed->SetPosition(pos);
		CTheScripts::ClearSpaceForMissionEntity(pos, CWorld::Players[index].m_pPed);
		CPlayerPed::ReactivatePlayerPed(index);
		SET_INTEGER_PARAM(0, index);
		StoreParameters(&m_nIp, 1);
		return 0;
	}
	case COMMAND_GET_PLAYER_COORDINATES:
	{
		CVector pos;
		CollectParameters(&m_nIp, 1);
		if (CWorld::Players[GET_INTEGER_PARAM(0)].m_pPed->bInVehicle && CWorld::Players[GET_INTEGER_PARAM(0)].m_pPed->m_pMyVehicle)
			pos = CWorld::Players[GET_INTEGER_PARAM(0)].m_pPed->m_pMyVehicle->GetPosition();
		else
			pos = CWorld::Players[GET_INTEGER_PARAM(0)].m_pPed->GetPosition();
		SET_VECTOR_PARAM(0, pos);
		StoreParameters(&m_nIp, 3);
		return 0;
	}
	case COMMAND_SET_PLAYER_COORDINATES:
	{
		CollectParameters(&m_nIp, 4);
		CVector pos = GET_VECTOR_PARAM(1);
		int index = GET_INTEGER_PARAM(0);
		if (pos.z <= MAP_Z_LOW_LIMIT)
			pos.z = CWorld::FindGroundZForCoord(pos.x, pos.y);
		CPlayerPed* ped = CWorld::Players[index].m_pPed;
		if (ped->bInVehicle && ped->m_pMyVehicle) {
			pos.z += ped->m_pMyVehicle->GetDistanceFromCentreOfMassToBaseOfModel();
			ped->m_pMyVehicle->Teleport(pos); // removed dumb stuff that was present here
			CTheScripts::ClearSpaceForMissionEntity(pos, ped->m_pMyVehicle);
			return 0;
		}
		pos.z += ped->GetDistanceFromCentreOfMassToBaseOfModel();
		CVector vOldPos = ped->GetPosition();
		ped->Teleport(pos);
		CTheScripts::ClearSpaceForMissionEntity(pos, ped);
		if (ped) { // great time to check
			for (int i = 0; i < ped->m_numNearPeds; i++) {
				CPed* pTestedPed = ped->m_nearPeds[i];
				if (!pTestedPed || !IsPedPointerValid(pTestedPed) || pTestedPed->bIsFrozen)
					continue;
				if (pTestedPed->m_pedInObjective == ped && pTestedPed->m_objective == OBJECTIVE_FOLLOW_CHAR_IN_FORMATION) {
					CVector vFollowerPos = pTestedPed->GetFormationPosition();
					CTheScripts::ClearSpaceForMissionEntity(vFollowerPos, ped);
					bool bFound = false;
					vFollowerPos.z = CWorld::FindGroundZFor3DCoord(vFollowerPos.x, vFollowerPos.y, vFollowerPos.z + 1.0f, &bFound) + 1.0f;
					if (bFound) {
						if (CWorld::GetIsLineOfSightClear(vFollowerPos, ped->GetPosition(), true, false, false, true, false, false)) {
							pTestedPed->Teleport(vFollowerPos);
						}
					}
				}
				else if (pTestedPed->m_leader == ped && !pTestedPed->bIsFrozen) {
					CVector vFollowerPos;
					if (pTestedPed->m_pedFormation)
						vFollowerPos = pTestedPed->GetFormationPosition();
					else
						vFollowerPos = ped->GetPosition() + pTestedPed->GetPosition() - vOldPos;
					CTheScripts::ClearSpaceForMissionEntity(vFollowerPos, ped);
					bool bFound = false;
					vFollowerPos.z = CWorld::FindGroundZFor3DCoord(vFollowerPos.x, vFollowerPos.y, vFollowerPos.z + 1.0f, &bFound) + 1.0f;
					if (bFound) {
						if (CWorld::GetIsLineOfSightClear(vFollowerPos, ped->GetPosition(), true, false, false, true, false, false)) {
							pTestedPed->Teleport(vFollowerPos);
						}
					}
				}
			}
		}
		return 0;
	}
	case COMMAND_IS_PLAYER_IN_AREA_2D:
	{
		CollectParameters(&m_nIp, 6);
		CPlayerPed* ped = CWorld::Players[GET_INTEGER_PARAM(0)].m_pPed;
		float x1 = GET_FLOAT_PARAM(1);
		float y1 = GET_FLOAT_PARAM(2);
		float x2 = GET_FLOAT_PARAM(3);
		float y2 = GET_FLOAT_PARAM(4);
		if (!ped->bInVehicle)
			UpdateCompareFlag(ped->IsWithinArea(x1, y1, x2, y2));
		else
			UpdateCompareFlag(ped->m_pMyVehicle->IsWithinArea(x1, y1, x2, y2));
		if (GET_INTEGER_PARAM(5))
			CTheScripts::HighlightImportantArea((uintptr)this + m_nIp, x1, y1, x2, y2, MAP_Z_LOW_LIMIT);
		/*
		if (CTheScripts::DbgFlag)
			CTheScripts::DrawDebugSquare(x1, y1, x2, y2);
		*/
		return 0;
	}
	case COMMAND_IS_PLAYER_IN_AREA_3D:
	{
		CollectParameters(&m_nIp, 8);
		CPlayerPed* ped = CWorld::Players[GET_INTEGER_PARAM(0)].m_pPed;
		float x1 = GET_FLOAT_PARAM(1);
		float y1 = GET_FLOAT_PARAM(2);
		float z1 = GET_FLOAT_PARAM(3);
		float x2 = GET_FLOAT_PARAM(4);
		float y2 = GET_FLOAT_PARAM(5);
		float z2 = GET_FLOAT_PARAM(6);
		if (ped->bInVehicle)
			UpdateCompareFlag(ped->m_pMyVehicle->IsWithinArea(x1, y1, z1, x2, y2, z2));
		else
			UpdateCompareFlag(ped->IsWithinArea(x1, y1, z1, x2, y2, z2));
		if (GET_INTEGER_PARAM(7))
			CTheScripts::HighlightImportantArea((uintptr)this + m_nIp, x1, y1, x2, y2, (z1 + z2) / 2);
		/*
		if (CTheScripts::DbgFlag)
			CTheScripts::DrawDebugCube(x1, y1, z1, x2, y2, z2);
		*/
		return 0;
	}
	case COMMAND_ADD_INT_VAR_TO_INT_VAR:
		nScriptVar1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*nScriptVar1 += *GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_ADD_INT_LVAR_TO_INT_VAR:
		nScriptVar1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*nScriptVar1 += *GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_ADD_INT_VAR_TO_INT_LVAR:
		nScriptVar1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*nScriptVar1 += *GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_ADD_INT_LVAR_TO_INT_LVAR:
		nScriptVar1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*nScriptVar1 += *GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_ADD_FLOAT_VAR_TO_FLOAT_VAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*fScriptVar1 += *(float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_ADD_FLOAT_LVAR_TO_FLOAT_VAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*fScriptVar1 += *(float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_ADD_FLOAT_VAR_TO_FLOAT_LVAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*fScriptVar1 += *(float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_ADD_FLOAT_LVAR_TO_FLOAT_LVAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*fScriptVar1 += *(float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_SUB_INT_VAR_FROM_INT_VAR:
		nScriptVar1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*nScriptVar1 -= *GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_SUB_INT_LVAR_FROM_INT_LVAR:
		nScriptVar1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*nScriptVar1 -= *GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_SUB_FLOAT_VAR_FROM_FLOAT_VAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*fScriptVar1 -= *(float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_SUB_FLOAT_LVAR_FROM_FLOAT_LVAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*fScriptVar1 -= *(float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	default:
		script_bad();
		break;
	}
	return -1;
}

int8 CRunningScript::ProcessCommands100To199(int32 command)
{
	float *fScriptVar1;
	int *nScriptVar1;
	switch (command) {
	case COMMAND_SUB_INT_LVAR_FROM_INT_VAR:
		nScriptVar1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*nScriptVar1 -= *GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_SUB_INT_VAR_FROM_INT_LVAR:
		nScriptVar1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*nScriptVar1 -= *GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_SUB_FLOAT_LVAR_FROM_FLOAT_VAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*fScriptVar1 -= *(float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_SUB_FLOAT_VAR_FROM_FLOAT_LVAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*fScriptVar1 -= *(float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_MULT_INT_VAR_BY_INT_VAR:
		nScriptVar1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*nScriptVar1 *= *GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_MULT_INT_VAR_BY_INT_LVAR:
		nScriptVar1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*nScriptVar1 *= *GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_MULT_INT_LVAR_BY_INT_VAR:
		nScriptVar1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*nScriptVar1 *= *GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_MULT_INT_LVAR_BY_INT_LVAR:
		nScriptVar1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*nScriptVar1 *= *GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_MULT_FLOAT_VAR_BY_FLOAT_VAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*fScriptVar1 *= *(float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_MULT_FLOAT_VAR_BY_FLOAT_LVAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*fScriptVar1 *= *(float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_MULT_FLOAT_LVAR_BY_FLOAT_VAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*fScriptVar1 *= *(float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_MULT_FLOAT_LVAR_BY_FLOAT_LVAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*fScriptVar1 *= *(float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_DIV_INT_VAR_BY_INT_VAR:
		nScriptVar1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*nScriptVar1 /= *GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_DIV_INT_VAR_BY_INT_LVAR:
		nScriptVar1 = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*nScriptVar1 /= *GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_DIV_INT_LVAR_BY_INT_VAR:
		nScriptVar1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*nScriptVar1 /= *GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_DIV_INT_LVAR_BY_INT_LVAR:
		nScriptVar1 = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*nScriptVar1 /= *GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_DIV_FLOAT_VAR_BY_FLOAT_VAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*fScriptVar1 /= *(float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_DIV_FLOAT_VAR_BY_FLOAT_LVAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*fScriptVar1 /= *(float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_DIV_FLOAT_LVAR_BY_FLOAT_VAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*fScriptVar1 /= *(float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_DIV_FLOAT_LVAR_BY_FLOAT_LVAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*fScriptVar1 /= *(float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_ADD_TIMED_VAL_TO_FLOAT_VAR:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
		*(float*)ptr += CTimer::GetTimeStep() * GET_FLOAT_PARAM(0);
		return 0;
	}
	case COMMAND_ADD_TIMED_VAL_TO_FLOAT_LVAR:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
		*(float*)ptr += CTimer::GetTimeStep() * GET_FLOAT_PARAM(0);
		return 0;
	}
	case COMMAND_ADD_TIMED_FLOAT_VAR_TO_FLOAT_VAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*fScriptVar1 += CTimer::GetTimeStep() * *(float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_ADD_TIMED_FLOAT_VAR_TO_FLOAT_LVAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*fScriptVar1 += CTimer::GetTimeStep() * *(float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_ADD_TIMED_FLOAT_LVAR_TO_FLOAT_VAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*fScriptVar1 += CTimer::GetTimeStep() * *(float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_ADD_TIMED_FLOAT_LVAR_TO_FLOAT_LVAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*fScriptVar1 += CTimer::GetTimeStep() * *(float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_SUB_TIMED_VAL_FROM_FLOAT_VAR:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CollectParameters(&m_nIp, 1);
		*(float*)ptr -= CTimer::GetTimeStep() * GET_FLOAT_PARAM(0);
		return 0;
	}
	case COMMAND_SUB_TIMED_VAL_FROM_FLOAT_LVAR:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		CollectParameters(&m_nIp, 1);
		*(float*)ptr -= CTimer::GetTimeStep() * GET_FLOAT_PARAM(0);
		return 0;
	}
	case COMMAND_SUB_TIMED_FLOAT_VAR_FROM_FLOAT_VAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*fScriptVar1 -= CTimer::GetTimeStep() * *(float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_SUB_TIMED_FLOAT_VAR_FROM_FLOAT_LVAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*fScriptVar1 -= CTimer::GetTimeStep() * *(float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_SUB_TIMED_FLOAT_LVAR_FROM_FLOAT_VAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*fScriptVar1 -= CTimer::GetTimeStep() * *(float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	case COMMAND_SUB_TIMED_FLOAT_LVAR_FROM_FLOAT_LVAR:
		fScriptVar1 = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*fScriptVar1 -= CTimer::GetTimeStep() * *(float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	case COMMAND_SET_VAR_INT_TO_VAR_INT:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*ptr = *GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	}
	case COMMAND_SET_VAR_INT_TO_LVAR_INT:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*ptr = *GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	}
	case COMMAND_SET_LVAR_INT_TO_VAR_INT:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*ptr = *GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	}
	case COMMAND_SET_LVAR_INT_TO_LVAR_INT:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*ptr = *GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	}
	case COMMAND_SET_VAR_FLOAT_TO_VAR_FLOAT:
	{
		float* ptr = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*ptr = *(float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	}
	case COMMAND_SET_VAR_FLOAT_TO_LVAR_FLOAT:
	{
		float* ptr = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*ptr = *(float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	}
	case COMMAND_SET_LVAR_FLOAT_TO_VAR_FLOAT:
	{
		float* ptr = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*ptr = *(float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	}
	case COMMAND_SET_LVAR_FLOAT_TO_LVAR_FLOAT:
	{
		float* ptr = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*ptr = *(float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	}
	case COMMAND_CSET_VAR_INT_TO_VAR_FLOAT:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*ptr = *(float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	}
	case COMMAND_CSET_VAR_INT_TO_LVAR_FLOAT:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*ptr = *(float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	}
	case COMMAND_CSET_LVAR_INT_TO_VAR_FLOAT:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*ptr = *(float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	}
	case COMMAND_CSET_LVAR_INT_TO_LVAR_FLOAT:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*ptr = *(float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	}
	case COMMAND_CSET_VAR_FLOAT_TO_VAR_INT:
	{
		float* ptr = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*ptr = *GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	}
	case COMMAND_CSET_VAR_FLOAT_TO_LVAR_INT:
	{
		float* ptr = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*ptr = *GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	}
	case COMMAND_CSET_LVAR_FLOAT_TO_VAR_INT:
	{
		float* ptr = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*ptr = *GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		return 0;
	}
	case COMMAND_CSET_LVAR_FLOAT_TO_LVAR_INT:
	{
		float* ptr = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*ptr = *GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		return 0;
	}
	case COMMAND_ABS_VAR_INT:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*ptr = ABS(*ptr);
		return 0;
	}
	case COMMAND_ABS_LVAR_INT:
	{
		int32* ptr = GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*ptr = ABS(*ptr);
		return 0;
	}
	case COMMAND_ABS_VAR_FLOAT:
	{
		float* ptr = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		*ptr = ABS(*ptr);
		return 0;
	}
	case COMMAND_ABS_LVAR_FLOAT:
	{
		float* ptr = (float*)GetPointerToScriptVariable(&m_nIp, VAR_LOCAL);
		*ptr = ABS(*ptr);
		return 0;
	}
	case COMMAND_GENERATE_RANDOM_FLOAT:
	{
		float* ptr = (float*)GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL);
		CGeneral::GetRandomNumber();
		CGeneral::GetRandomNumber();
		CGeneral::GetRandomNumber(); /* To make it EXTRA random! */
#ifdef FIX_BUGS
		*ptr = CGeneral::GetRandomNumberInRange(0.0f, 1.0f);
#else
		*ptr = CGeneral::GetRandomNumber() / 65536.0f;
		/* Between 0 and 0.5 on PC (oh well...), never used in original script. */
#endif

		return 0;
	}
	case COMMAND_GENERATE_RANDOM_INT:
		*GetPointerToScriptVariable(&m_nIp, VAR_GLOBAL) = CGeneral::GetRandomNumber();
		return 0;
	case COMMAND_CREATE_CHAR:
	{
		CollectParameters(&m_nIp, 5);
		switch (GET_INTEGER_PARAM(1)) {
		case MI_COP:
			if (GET_INTEGER_PARAM(0) == PEDTYPE_COP)
				SET_INTEGER_PARAM(1, COP_STREET);
			break;
		case MI_SWAT:
			if (GET_INTEGER_PARAM(0) == PEDTYPE_COP)
				SET_INTEGER_PARAM(1, COP_SWAT);
			break;
		case MI_FBI:
			if (GET_INTEGER_PARAM(0) == PEDTYPE_COP)
				SET_INTEGER_PARAM(1, COP_FBI);
			break;
		case MI_ARMY:
			if (GET_INTEGER_PARAM(0) == PEDTYPE_COP)
				SET_INTEGER_PARAM(1, COP_ARMY);
			break;
		case MI_MEDIC:
			if (GET_INTEGER_PARAM(0) == PEDTYPE_EMERGENCY)
				SET_INTEGER_PARAM(1, PEDTYPE_EMERGENCY);
			break;
		case MI_FIREMAN:
			if (GET_INTEGER_PARAM(0) == PEDTYPE_FIREMAN)
				SET_INTEGER_PARAM(1, PEDTYPE_FIREMAN);
			break;
		default:
			break;
		}
		CPed* ped;
		if (GET_INTEGER_PARAM(0) == PEDTYPE_COP)
			ped = new CCopPed((eCopType)GET_INTEGER_PARAM(1));
		else if (GET_INTEGER_PARAM(0) == PEDTYPE_EMERGENCY || GET_INTEGER_PARAM(0) == PEDTYPE_FIREMAN)
			ped = new CEmergencyPed(GET_INTEGER_PARAM(1));
		else
			ped = new CCivilianPed((ePedType)GET_INTEGER_PARAM(0), GET_INTEGER_PARAM(1));
		ped->CharCreatedBy = MISSION_CHAR;
		ped->bRespondsToThreats = false;
		ped->bAllowMedicsToReviveMe = false;
		ped->bIsPlayerFriend = false;
		CVector pos = GET_VECTOR_PARAM(2);
		if (pos.z <= MAP_Z_LOW_LIMIT)
			pos.z = CWorld::FindGroundZForCoord(pos.x, pos.y);
		pos.z += 1.0f;
		ped->SetPosition(pos);
		ped->SetOrientation(0.0f, 0.0f, 0.0f);
		CTheScripts::ClearSpaceForMissionEntity(pos, ped);
		if (m_bIsMissionScript)
			ped->bIsStaticWaitingForCollision = true;
		CWorld::Add(ped);
		ped->m_nZoneLevel = CTheZones::GetLevelFromPosition(&pos);
		CPopulation::ms_nTotalMissionPeds++;
		SET_INTEGER_PARAM(0, CPools::GetPedPool()->GetIndex(ped));
		StoreParameters(&m_nIp, 1);
		if (m_bIsMissionScript)
			CTheScripts::MissionCleanUp.AddEntityToList(GET_INTEGER_PARAM(0), CLEANUP_CHAR);
		return 0;
	}
	case COMMAND_DELETE_CHAR:
	{
		CollectParameters(&m_nIp, 1);
		CPed* ped = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		CTheScripts::RemoveThisPed(ped);
		if (m_bIsMissionScript)
			CTheScripts::MissionCleanUp.RemoveEntityFromList(GET_INTEGER_PARAM(0), CLEANUP_CHAR);
		return 0;
	}
	case COMMAND_CHAR_WANDER_DIR:
	{
		CollectParameters(&m_nIp, 2);
		CPed* ped = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(ped);
		ped->ClearAll();
		int8 path = GET_INTEGER_PARAM(1);
		if (GET_INTEGER_PARAM(1) < 0 || GET_INTEGER_PARAM(1) > 7)
			// Max number GetRandomNumberInRange returns is max-1
#ifdef FIX_BUGS
			path = CGeneral::GetRandomNumberInRange(0, 8);
#else
			path = CGeneral::GetRandomNumberInRange(0, 7);
#endif

		ped->SetWanderPath(path);
		return 0;
	}
	//case COMMAND_CHAR_WANDER_RANGE:
	case COMMAND_CHAR_FOLLOW_PATH:
	{
		CollectParameters(&m_nIp, 6);
		CPed* ped = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(ped);
		if (ped->GetPedState() == PED_ATTACK || ped->GetPedState() == PED_FIGHT || !ped->IsPedInControl())
			return 0;
		CVector pos = GET_VECTOR_PARAM(1);
		if (pos.z <= MAP_Z_LOW_LIMIT)
			pos.z = CWorld::FindGroundZForCoord(pos.x, pos.y);
		float radius = GET_FLOAT_PARAM(4);
		eMoveState state;
		switch (GET_INTEGER_PARAM(5)) {
		case 0: state = PEDMOVE_WALK; break;
		case 1: state = PEDMOVE_RUN; break;
		default: state = PEDMOVE_WALK; break;
		}
		ped->ClearAll();
		ped->m_pathNodeTimer = 0;
		ped->SetFollowPath(pos, radius, state, nil, nil, 999999);
		return 0;
	}
	case COMMAND_CHAR_SET_IDLE:
	{
		CollectParameters(&m_nIp, 1);
		CPed* ped = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(ped);
		ped->bScriptObjectiveCompleted = false;
		ped->SetObjective(OBJECTIVE_WAIT_ON_FOOT);
		return 0;
	}
	case COMMAND_GET_CHAR_COORDINATES:
	{
		CollectParameters(&m_nIp, 1);
		CPed* ped = CPools::GetPedPool()->GetAt(ScriptParams[0]);
#ifdef __PS3__
		// patch 51: a char that isn't there any more (deleted under the
		// script, see ClearSpaceForMissionEntity): (0, 0, 0) and the script
		// goes on (its next IS_CHAR_DEAD says he's gone) instead of stopping
		if (ped == nil) {
			PS3_ScriptGone(this, "GET_CHAR_COORDINATES", ScriptParams[0]);
			ScriptParams[0] = ScriptParams[1] = ScriptParams[2] = 0;
			StoreParameters(&m_nIp, 3);
			return 0;
		}
#endif
		script_check(ped);
		CVehicle* vehicle;
		CVector pos;
		/* Seems a bit clumsy but I'll leave original flow */
		if (ped->bInVehicle)
			vehicle = ped->m_pMyVehicle;
		else
			vehicle = nil;
		if (vehicle)
			pos = vehicle->GetPosition();
		else
			pos = ped->GetPosition();
		SET_VECTOR_PARAM(0, pos);
		StoreParameters(&m_nIp, 3);
		return 0;
	}
	case COMMAND_SET_CHAR_COORDINATES:
	{
		CollectParameters(&m_nIp, 4);
		CPed* ped = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(ped);
		CVehicle* vehicle;
		if (ped->bInVehicle)
			vehicle = ped->m_pMyVehicle;
		else
			vehicle = nil;
		CVector pos = GET_VECTOR_PARAM(1);
		if (pos.z <= MAP_Z_LOW_LIMIT)
			pos.z = CWorld::FindGroundZForCoord(pos.x, pos.y);
		// removed dumb stuff again
		if (!vehicle) {
			pos.z += ped->GetDistanceFromCentreOfMassToBaseOfModel();
			ped->Teleport(pos);
			CTheScripts::ClearSpaceForMissionEntity(pos, ped);
			for (int i = 0; i < ped->m_numNearPeds; i++) {
				CPed* pNearPed = ped->m_nearPeds[i];
				if (pNearPed->m_leader == ped) {
					pNearPed->Teleport(pos);
					pNearPed->PositionAnyPedOutOfCollision();
				}
			}
		}
		else {
			pos.z += vehicle->GetDistanceFromCentreOfMassToBaseOfModel();
			vehicle->Teleport(pos);
			CTheScripts::ClearSpaceForMissionEntity(pos, vehicle);
		}
		return 0;
	}
	case COMMAND_IS_CHAR_STILL_ALIVE:
	{
		CollectParameters(&m_nIp, 1);
		CPed* ped = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		UpdateCompareFlag(ped && !ped->DyingOrDead());
		return 0;
	}
	case COMMAND_IS_CHAR_IN_AREA_2D:
	{
		CollectParameters(&m_nIp, 6);
		CPed* ped = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(ped);
		CVehicle* vehicle;
		if (ped->bInVehicle)
			vehicle = ped->m_pMyVehicle;
		else
			vehicle = nil;
		float x1 = GET_FLOAT_PARAM(1);
		float y1 = GET_FLOAT_PARAM(2);
		float x2 = GET_FLOAT_PARAM(3);
		float y2 = GET_FLOAT_PARAM(4);
		if (vehicle)
			UpdateCompareFlag(ped->m_pMyVehicle->IsWithinArea(x1, y1, x2, y2));
		else
			UpdateCompareFlag(ped->IsWithinArea(x1, y1, x2, y2));
		if (GET_INTEGER_PARAM(5))
			CTheScripts::HighlightImportantArea((uintptr)this + m_nIp, x1, y1, x2, y2, MAP_Z_LOW_LIMIT);
		/*
		if (CTheScripts::DbgFlag)
			CTheScripts::DrawDebugSquare(x1, y1, x2, y2);
		*/
		return 0;
	}
	case COMMAND_IS_CHAR_IN_AREA_3D:
	{
		CollectParameters(&m_nIp, 8);
		CPed* ped = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(ped);
		CVehicle* vehicle;
		if (ped->bInVehicle)
			vehicle = ped->m_pMyVehicle;
		else
			vehicle = nil;
		float x1 = GET_FLOAT_PARAM(1);
		float y1 = GET_FLOAT_PARAM(2);
		float z1 = GET_FLOAT_PARAM(3);
		float x2 = GET_FLOAT_PARAM(4);
		float y2 = GET_FLOAT_PARAM(5);
		float z2 = GET_FLOAT_PARAM(6);
		if (vehicle)
			UpdateCompareFlag(ped->m_pMyVehicle->IsWithinArea(x1, y1, z1, x2, y2, z2));
		else
			UpdateCompareFlag(ped->IsWithinArea(x1, y1, z1, x2, y2, z2));
		if (GET_INTEGER_PARAM(7))
			CTheScripts::HighlightImportantArea((uintptr)this + m_nIp, x1, y1, x2, y2, (z1 + z2) / 2);
		/*
		if (CTheScripts::DbgFlag)
			CTheScripts::DrawDebugCube(x1, y1, z1, x2, y2, z2);
		*/
		return 0;
	}
	case COMMAND_CREATE_CAR:
	{
		CollectParameters(&m_nIp, 4);
		int32 handle;
		if (CModelInfo::IsBoatModel(GET_INTEGER_PARAM(0))) {
			CBoat* boat = new CBoat(GET_INTEGER_PARAM(0), MISSION_VEHICLE);
			CVector pos = GET_VECTOR_PARAM(1);
			if (pos.z <= MAP_Z_LOW_LIMIT)
				pos.z = CWorld::FindGroundZForCoord(pos.x, pos.y);
			pos.z += boat->GetDistanceFromCentreOfMassToBaseOfModel();
			boat->SetPosition(pos);
			CTheScripts::ClearSpaceForMissionEntity(pos, boat);
			boat->SetStatus(STATUS_ABANDONED);
			boat->bIsLocked = true;
			boat->AutoPilot.m_nCarMission = MISSION_NONE;
			boat->AutoPilot.m_nTempAction = TEMPACT_NONE;
			boat->AutoPilot.m_nCruiseSpeed = boat->AutoPilot.m_fMaxTrafficSpeed = 20.0f;
			/* Boats use water buoyancy and must not wait for streamed ground
			 * collision.  Keeping them static here can leave scripted boats out
			 * of the normal update/render path while attached entities survive. */
			boat->bIsStaticWaitingForCollision = false;
			CWorld::Add(boat);
			handle = CPools::GetVehiclePool()->GetIndex(boat);
		}
		else {
			CVehicle* car;

			if (!CModelInfo::IsBikeModel(GET_INTEGER_PARAM(0)))
				car = new CAutomobile(GET_INTEGER_PARAM(0), MISSION_VEHICLE);
			else {
				car = new CBike(GET_INTEGER_PARAM(0), MISSION_VEHICLE);
				((CBike*)(car))->bIsStanding = true;
			}
			CVector pos = GET_VECTOR_PARAM(1);
			if (pos.z <= MAP_Z_LOW_LIMIT)
				pos.z = CWorld::FindGroundZForCoord(pos.x, pos.y);
			pos.z += car->GetDistanceFromCentreOfMassToBaseOfModel();
			car->SetPosition(pos);
			CTheScripts::ClearSpaceForMissionEntity(pos, car);
			car->SetStatus(STATUS_ABANDONED);
			car->bIsLocked = true;
			CCarCtrl::JoinCarWithRoadSystem(car);
			car->AutoPilot.m_nCarMission = MISSION_NONE;
			car->AutoPilot.m_nTempAction = TEMPACT_NONE;
			car->AutoPilot.m_nDrivingStyle = DRIVINGSTYLE_STOP_FOR_CARS;
			car->AutoPilot.m_nCruiseSpeed = car->AutoPilot.m_fMaxTrafficSpeed = 9.0f;
			car->AutoPilot.m_nCurrentLane = car->AutoPilot.m_nNextLane = 0;
			car->bEngineOn = false;
			car->m_nZoneLevel = CTheZones::GetLevelFromPosition(&pos); // TODO: gpTheZones
			car->bHasBeenOwnedByPlayer = true;
			if (m_bIsMissionScript)
				car->bIsStaticWaitingForCollision = true;
			CWorld::Add(car);
			handle = CPools::GetVehiclePool()->GetIndex(car);
		}
		SET_INTEGER_PARAM(0, handle);
		StoreParameters(&m_nIp, 1);
#ifdef GTA_NETWORK
		// TODO(LCS): register car
#endif
		if (m_bIsMissionScript)
			CTheScripts::MissionCleanUp.AddEntityToList(handle, CLEANUP_CAR);
		return 0;
	}
	case COMMAND_DELETE_CAR:
	{
		CollectParameters(&m_nIp, 1);
		CVehicle* car = CPools::GetVehiclePool()->GetAt(GET_INTEGER_PARAM(0));
		if (car) {
			CWorld::Remove(car);
			CWorld::RemoveReferencesToDeletedObject(car);
			delete car;
		}
		if (m_bIsMissionScript)
			CTheScripts::MissionCleanUp.RemoveEntityFromList(GET_INTEGER_PARAM(0), CLEANUP_CAR);
		return 0;
	}
	case COMMAND_CAR_GOTO_COORDINATES:
	{
		CollectParameters(&m_nIp, 4);
		CVehicle* car = CPools::GetVehiclePool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(car);
		CVector pos = GET_VECTOR_PARAM(1);
		if (pos.z <= MAP_Z_LOW_LIMIT)
			pos.z = CWorld::FindGroundZForCoord(pos.x, pos.y);
		pos.z += car->GetDistanceFromCentreOfMassToBaseOfModel();
		uint8 nOldMission = car->AutoPilot.m_nCarMission;
		if (CCarCtrl::JoinCarWithRoadSystemGotoCoors(car, pos, false))
			car->AutoPilot.m_nCarMission = MISSION_GOTOCOORDS_STRAIGHT;
		else
			car->AutoPilot.m_nCarMission = MISSION_GOTOCOORDS;
		car->SetStatus(STATUS_PHYSICS);
		car->bEngineOn = true;
		car->AutoPilot.m_nCruiseSpeed = Max(1, car->AutoPilot.m_nCruiseSpeed);
		if (nOldMission != car->AutoPilot.m_nCarMission)
			car->AutoPilot.m_nAntiReverseTimer = CTimer::GetTimeInMilliseconds();
		return 0;
	}
	case COMMAND_CAR_WANDER_RANDOMLY:
	{
		CollectParameters(&m_nIp, 1);
		CVehicle* car = CPools::GetVehiclePool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(car);
		CCarCtrl::JoinCarWithRoadSystem(car);
		car->AutoPilot.m_nCarMission = MISSION_CRUISE;
		car->bEngineOn = true;
		car->AutoPilot.m_nCruiseSpeed = Max(1, car->AutoPilot.m_nCruiseSpeed);
		car->AutoPilot.m_nAntiReverseTimer = CTimer::GetTimeInMilliseconds();
		return 0;
	}
	case COMMAND_CAR_SET_IDLE:
	{
		CollectParameters(&m_nIp, 1);
		CVehicle* car = CPools::GetVehiclePool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(car);
		car->AutoPilot.m_nCarMission = MISSION_NONE;
		return 0;
	}
	case COMMAND_GET_CAR_COORDINATES:
	{
		CollectParameters(&m_nIp, 1);
		CVehicle* car = CPools::GetVehiclePool()->GetAt(ScriptParams[0]);
#ifdef __PS3__
		if (car == nil) {	// as GET_CHAR_COORDINATES
			PS3_ScriptGone(this, "GET_CAR_COORDINATES", ScriptParams[0]);
			ScriptParams[0] = ScriptParams[1] = ScriptParams[2] = 0;
			StoreParameters(&m_nIp, 3);
			return 0;
		}
#endif
		script_check(car);
		*(CVector*)&ScriptParams[0] = car->GetPosition();
		StoreParameters(&m_nIp, 3);
		return 0;
	}
	case COMMAND_SET_CAR_COORDINATES:
	{
		CollectParameters(&m_nIp, 4);
		CVehicle* car = CPools::GetVehiclePool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(car);
		CVector pos = GET_VECTOR_PARAM(1);
		if (pos.z <= MAP_Z_LOW_LIMIT)
			pos.z = CWorld::FindGroundZForCoord(pos.x, pos.y);
		pos.z += car->GetDistanceFromCentreOfMassToBaseOfModel();
		car->SetIsStatic(false);
		/* Again weird usage of virtual functions. */
		if (car->IsBoat()) {
			car->Teleport(pos);
			CTheScripts::ClearSpaceForMissionEntity(pos, car);
		}
		else {
			car->Teleport(pos);
			CTheScripts::ClearSpaceForMissionEntity(pos, car);
			/* May the following be inlined CCarCtrl function? */
			switch (car->AutoPilot.m_nCarMission) {
			case MISSION_CRUISE:
				CCarCtrl::JoinCarWithRoadSystem(car);
				break;
			case MISSION_RAMPLAYER_FARAWAY:
			case MISSION_RAMPLAYER_CLOSE:
			case MISSION_BLOCKPLAYER_FARAWAY:
			case MISSION_BLOCKPLAYER_CLOSE:
			case MISSION_BLOCKPLAYER_HANDBRAKESTOP:
				CCarCtrl::JoinCarWithRoadSystemGotoCoors(car, FindPlayerCoors(), false);
				break;
			case MISSION_GOTOCOORDS:
			case MISSION_GOTOCOORDS_STRAIGHT:
				CCarCtrl::JoinCarWithRoadSystemGotoCoors(car, car->AutoPilot.m_vecDestinationCoors, false);
				break;
			case MISSION_GOTOCOORDS_ACCURATE:
			case MISSION_GOTO_COORDS_STRAIGHT_ACCURATE:
				CCarCtrl::JoinCarWithRoadSystemGotoCoors(car, car->AutoPilot.m_vecDestinationCoors, false);
				break;
			case MISSION_RAMCAR_FARAWAY:
			case MISSION_RAMCAR_CLOSE:
			case MISSION_BLOCKCAR_FARAWAY:
			case MISSION_BLOCKCAR_CLOSE:
			case MISSION_BLOCKCAR_HANDBRAKESTOP:
				CCarCtrl::JoinCarWithRoadSystemGotoCoors(car, car->AutoPilot.m_pTargetCar->GetPosition(), false);
				break;
			default:
				break;
			}
		}
		return 0;
	}
	case COMMAND_IS_CAR_STILL_ALIVE:
	{
		CollectParameters(&m_nIp, 1);
		CVehicle* car = CPools::GetVehiclePool()->GetAt(GET_INTEGER_PARAM(0));
		UpdateCompareFlag(car && car->GetStatus() != STATUS_WRECKED && (car->IsBoat() || !car->bIsInWater));
		return 0;
	}
	case COMMAND_SET_CAR_CRUISE_SPEED:
	{
		CollectParameters(&m_nIp, 2);
		CVehicle* car = CPools::GetVehiclePool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(car);
		car->AutoPilot.m_nCruiseSpeed = Min(GET_FLOAT_PARAM(1), 60.0f * car->pHandling->Transmission.fMaxCruiseVelocity);
		return 0;
	}
	case COMMAND_SET_CAR_DRIVING_STYLE:
	{
		CollectParameters(&m_nIp, 2);
		CVehicle* car = CPools::GetVehiclePool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(car);
		car->AutoPilot.m_nDrivingStyle = (uint8)GET_INTEGER_PARAM(1);
		return 0;
	}
	case COMMAND_SET_CAR_MISSION:
	{
		CollectParameters(&m_nIp, 2);
		CVehicle* car = CPools::GetVehiclePool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(car);
		if ((uint8)GET_INTEGER_PARAM(1) != car->AutoPilot.m_nCarMission) {
			car->AutoPilot.m_nCarMission = (uint8)GET_INTEGER_PARAM(1);
			car->AutoPilot.m_nAntiReverseTimer = CTimer::GetTimeInMilliseconds();
		}
		car->bEngineOn = true;
		return 0;
	}
	case COMMAND_IS_CAR_IN_AREA_2D:
	{
		CollectParameters(&m_nIp, 6);
		CVehicle* vehicle = CPools::GetVehiclePool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(vehicle);
		float x1 = GET_FLOAT_PARAM(1);
		float y1 = GET_FLOAT_PARAM(2);
		float x2 = GET_FLOAT_PARAM(3);
		float y2 = GET_FLOAT_PARAM(4);
		UpdateCompareFlag(vehicle->IsWithinArea(x1, y1, x2, y2));
		if (GET_INTEGER_PARAM(5))
			CTheScripts::HighlightImportantArea((uintptr)this + m_nIp, x1, y1, x2, y2, MAP_Z_LOW_LIMIT);
		/*
		if (CTheScripts::DbgFlag)
			CTheScripts::DrawDebugSquare(x1, y1, x2, y2);
		*/
		return 0;
	}
	case COMMAND_IS_CAR_IN_AREA_3D:
	{
		CollectParameters(&m_nIp, 8);
		CVehicle* vehicle = CPools::GetVehiclePool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(vehicle);
		float x1 = GET_FLOAT_PARAM(1);
		float y1 = GET_FLOAT_PARAM(2);
		float z1 = GET_FLOAT_PARAM(3);
		float x2 = GET_FLOAT_PARAM(4);
		float y2 = GET_FLOAT_PARAM(5);
		float z2 = GET_FLOAT_PARAM(6);
		UpdateCompareFlag(vehicle->IsWithinArea(x1, y1, z1, x2, y2, z2));
		if (GET_INTEGER_PARAM(7))
			CTheScripts::HighlightImportantArea((uintptr)this + m_nIp, x1, y1, x2, y2, (z1 + z2) / 2);
		/*
		if (CTheScripts::DbgFlag)
			CTheScripts::DrawDebugCube(x1, y1, z1, x2, y2, z2);
		*/
		return 0;
	}
	case COMMAND_SPECIAL_0:
	case COMMAND_SPECIAL_1:
	case COMMAND_SPECIAL_2:
	case COMMAND_SPECIAL_3:
	case COMMAND_SPECIAL_4:
	case COMMAND_SPECIAL_5:
	case COMMAND_SPECIAL_6:
	case COMMAND_SPECIAL_7:
		script_bad();
		return 0;
	case COMMAND_PRINT_BIG:
	{
		const char *rawKey = (const char*)&CTheScripts::ScriptSpace[m_nIp];
		wchar* key = CTheScripts::GetTextByKeyFromScript(&m_nIp);
		if (!CGeneral::faststricmp(m_abScriptName, "SALH5") &&
		    !strncmp(rawKey, "M_FAIL", KEY_LENGTH_IN_SCRIPT))
			FinalMissionMusic::EndLCSFinale();
#ifdef MISSION_REPLAY
		if (strcmp((char*)&CTheScripts::ScriptSpace[m_nIp - KEY_LENGTH_IN_SCRIPT], "M_FAIL") == 0) {
			if (AllowMissionReplay == MISSION_RETRY_STAGE_WAIT_FOR_TIMER_AFTER_RESTART)
				AllowMissionReplay = MISSION_RETRY_STAGE_NORMAL;
			if (CanAllowMissionReplay())
				AllowMissionReplay = MISSION_RETRY_STAGE_WAIT_FOR_SCRIPT_TO_TERMINATE;
		}
#endif
		CollectParameters(&m_nIp, 2);
		CMessages::AddBigMessage(key, GET_INTEGER_PARAM(0), GET_INTEGER_PARAM(1) - 1);
		return 0;
	}
	case COMMAND_PRINT:
	{
		const char *rawKey = (const char*)&CTheScripts::ScriptSpace[m_nIp];
		wchar* key = CTheScripts::GetTextByKeyFromScript(&m_nIp);
		if (!CGeneral::faststricmp(m_abScriptName, "SALH5")) {
			if (!strncmp(rawKey, "SALH510", KEY_LENGTH_IN_SCRIPT))
				FinalMissionMusic::StartLCSFinaleFM();
			else if (!strncmp(rawKey, "SALH515", KEY_LENGTH_IN_SCRIPT))
				FinalMissionMusic::EnterLCSBoatChase();
			else if (!strncmp(rawKey, "SALH522", KEY_LENGTH_IN_SCRIPT))
				FinalMissionMusic::BeginLCSLighthouseTransition();
			else if (!strncmp(rawKey, "SALH53A", KEY_LENGTH_IN_SCRIPT))
				FinalMissionMusic::BeginLCSLighthouseClimax();
			else if (!strncmp(rawKey, "SALH5TD", KEY_LENGTH_IN_SCRIPT))
				FinalMissionMusic::BeginLCSLighthouseBattle();
		}
		CollectParameters(&m_nIp, 2);
		CMessages::AddMessage(key, GET_INTEGER_PARAM(0), GET_INTEGER_PARAM(1));
		return 0;
	}
	case COMMAND_PRINT_NOW:
	{
		const char *rawKey = (const char*)&CTheScripts::ScriptSpace[m_nIp];
		wchar* key = CTheScripts::GetTextByKeyFromScript(&m_nIp);
		if (!CGeneral::faststricmp(m_abScriptName, "SALH5")) {
			if (!strncmp(rawKey, "SALH510", KEY_LENGTH_IN_SCRIPT))
				FinalMissionMusic::StartLCSFinaleFM();
			else if (!strncmp(rawKey, "SALH515", KEY_LENGTH_IN_SCRIPT))
				FinalMissionMusic::EnterLCSBoatChase();
			else if (!strncmp(rawKey, "SALH522", KEY_LENGTH_IN_SCRIPT))
				FinalMissionMusic::BeginLCSLighthouseTransition();
			else if (!strncmp(rawKey, "SALH53A", KEY_LENGTH_IN_SCRIPT))
				FinalMissionMusic::BeginLCSLighthouseClimax();
			else if (!strncmp(rawKey, "SALH5TD", KEY_LENGTH_IN_SCRIPT))
				FinalMissionMusic::BeginLCSLighthouseBattle();
		}
		CollectParameters(&m_nIp, 2);
		CMessages::AddMessageJumpQ(key, GET_INTEGER_PARAM(0), GET_INTEGER_PARAM(1));
		return 0;
	}
	case COMMAND_PRINT_SOON:
	{
		const char *rawKey = (const char*)&CTheScripts::ScriptSpace[m_nIp];
		wchar* key = CTheScripts::GetTextByKeyFromScript(&m_nIp);
		if (!CGeneral::faststricmp(m_abScriptName, "SALH5")) {
			if (!strncmp(rawKey, "SALH510", KEY_LENGTH_IN_SCRIPT))
				FinalMissionMusic::StartLCSFinaleFM();
			else if (!strncmp(rawKey, "SALH515", KEY_LENGTH_IN_SCRIPT))
				FinalMissionMusic::EnterLCSBoatChase();
			else if (!strncmp(rawKey, "SALH522", KEY_LENGTH_IN_SCRIPT))
				FinalMissionMusic::BeginLCSLighthouseTransition();
			else if (!strncmp(rawKey, "SALH53A", KEY_LENGTH_IN_SCRIPT))
				FinalMissionMusic::BeginLCSLighthouseClimax();
			else if (!strncmp(rawKey, "SALH5TD", KEY_LENGTH_IN_SCRIPT))
				FinalMissionMusic::BeginLCSLighthouseBattle();
		}
		CollectParameters(&m_nIp, 2);
		CMessages::AddMessageSoon(key, GET_INTEGER_PARAM(0), GET_INTEGER_PARAM(1));
		return 0;
	}
	case COMMAND_CLEAR_PRINTS:
		CMessages::ClearMessages();
		return 0;
	case COMMAND_GET_TIME_OF_DAY:
		SET_INTEGER_PARAM(0, CClock::GetHours());
		SET_INTEGER_PARAM(1, CClock::GetMinutes());
		StoreParameters(&m_nIp, 2);
		return 0;
	case COMMAND_SET_TIME_OF_DAY:
		CollectParameters(&m_nIp, 2);
		CClock::SetGameClock(GET_INTEGER_PARAM(0), GET_INTEGER_PARAM(1));
		return 0;
	case COMMAND_GET_MINUTES_TO_TIME_OF_DAY:
		CollectParameters(&m_nIp, 2);
		SET_INTEGER_PARAM(0, CClock::GetGameClockMinutesUntil(GET_INTEGER_PARAM(0), GET_INTEGER_PARAM(1)));
		StoreParameters(&m_nIp, 1);
		return 0;
	case COMMAND_IS_POINT_ON_SCREEN:
	{
		CollectParameters(&m_nIp, 4);
		CVector pos = GET_VECTOR_PARAM(0);
		if (pos.z <= -100)
			pos.z = CWorld::FindGroundZForCoord(pos.x, pos.y);
		UpdateCompareFlag(TheCamera.IsSphereVisible(pos, GET_FLOAT_PARAM(3)));
		return 0;
	}
	case COMMAND_DEBUG_ON:
		CTheScripts::DbgFlag = true;
		return 0;
	case COMMAND_DEBUG_OFF:
		CTheScripts::DbgFlag = false;
		return 0;
	case COMMAND_RETURN_TRUE:
		UpdateCompareFlag(true);
		ReturnFromGosubOrFunction();
		return 0;
	case COMMAND_RETURN_FALSE:
		UpdateCompareFlag(false);
		ReturnFromGosubOrFunction();
		return 0;
	//case COMMAND_VAR_INT:
	default:
		script_bad();
		break;
	}
	return -1;
}

int8 CRunningScript::ProcessCommands200To299(int32 command)
{
	switch (command) {
	/* Special commands.
	case COMMAND_VAR_FLOAT:
	case COMMAND_LVAR_INT:
	case COMMAND_LVAR_FLOAT:
	case COMMAND_LBRACKET:
	case COMMAND_RBRACKET:
	case COMMAND_REPEAT:
	case COMMAND_ENDREPEAT:
	case COMMAND_IF:
	case COMMAND_IFNOT:
	case COMMAND_ELSE:
	case COMMAND_ENDIF:
	case COMMAND_WHILE:
	case COMMAND_WHILENOT:
	case COMMAND_ENDWHILE:
	case COMMAND_214:
	case COMMAND_215:
	case COMMAND_216:
	case COMMAND_217:
	case COMMAND_218:
	*/
	case COMMAND_ANDOR:
		CollectParameters(&m_nIp, 1);
		m_nAndOrState = GET_INTEGER_PARAM(0);
		if (m_nAndOrState == ANDOR_NONE){
			m_bCondResult = false; // pointless
		}else if (m_nAndOrState >= ANDS_1 && m_nAndOrState <= ANDS_8){
			m_bCondResult = true;
		}else if (m_nAndOrState >= ORS_1 && m_nAndOrState <= ORS_8){
			m_bCondResult = false;
		}else{
			script_assert(0 && "COMMAND_ANDOR: invalid ANDOR state");
		}
		return 0;
	case COMMAND_LAUNCH_MISSION:
	{
		CollectParameters(&m_nIp, 1);
		CRunningScript* pNew = CTheScripts::StartNewScript(GET_INTEGER_PARAM(0));
		pNew->m_bIsMissionScript = true;
		return 0;
	}
	case COMMAND_MISSION_HAS_FINISHED:
	{
		if (!m_bIsMissionScript)
			return 0;
		CTheScripts::MissionCleanUp.Process();
		return 0;
	}
	case COMMAND_STORE_CAR_CHAR_IS_IN:
	{
		CollectParameters(&m_nIp, 1);
		CPed* ped = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(ped);
		CVehicle* pCurrent = nil;
		if (ped->bInVehicle) {
			pCurrent = ped->m_pMyVehicle;
		}
		script_check(pCurrent); // GetIndex(0) doesn't look good
		int handle = CPools::GetVehiclePool()->GetIndex(pCurrent);
		if (handle != CTheScripts::StoreVehicleIndex && m_bIsMissionScript){
			CVehicle* pOld = CPools::GetVehiclePool()->GetAt(CTheScripts::StoreVehicleIndex);
			if (pOld){
				CCarCtrl::RemoveFromInterestingVehicleList(pOld);
				if (pOld->VehicleCreatedBy == MISSION_VEHICLE && CTheScripts::StoreVehicleWasRandom){
					pOld->VehicleCreatedBy = RANDOM_VEHICLE;
					pOld->bIsLocked = false;
					CCarCtrl::NumRandomCars++;
					CCarCtrl::NumMissionCars--;
					CTheScripts::MissionCleanUp.RemoveEntityFromList(CTheScripts::StoreVehicleIndex, CLEANUP_CAR);
				}
			}

			CTheScripts::StoreVehicleIndex = handle;
			switch (pCurrent->VehicleCreatedBy){
			case RANDOM_VEHICLE:
				pCurrent->VehicleCreatedBy = MISSION_VEHICLE;
				CCarCtrl::NumMissionCars++;
				CCarCtrl::NumRandomCars--;
				CTheScripts::StoreVehicleWasRandom = true;
				CTheScripts::MissionCleanUp.AddEntityToList(CTheScripts::StoreVehicleIndex, CLEANUP_CAR);
				break;
			case PARKED_VEHICLE:
				pCurrent->VehicleCreatedBy = MISSION_VEHICLE;
				CCarCtrl::NumMissionCars++;
				CCarCtrl::NumParkedCars--;
				CTheScripts::StoreVehicleWasRandom = true;
				CTheScripts::MissionCleanUp.AddEntityToList(CTheScripts::StoreVehicleIndex, CLEANUP_CAR);
				break;
			case MISSION_VEHICLE:
			case PERMANENT_VEHICLE:
				CTheScripts::StoreVehicleWasRandom = false;
				break;
			default:
				break;
			}
		}
		SET_INTEGER_PARAM(0, CTheScripts::StoreVehicleIndex);
		StoreParameters(&m_nIp, 1);
		return 0;
	}
	case COMMAND_STORE_CAR_PLAYER_IS_IN:
	{
		CollectParameters(&m_nIp, 1);
		CPed* ped = CWorld::Players[GET_INTEGER_PARAM(0)].m_pPed;
		script_assert(ped);
		if (!ped->bInVehicle)
			return 0; // No value written to output variable
		CVehicle* pCurrent = ped->m_pMyVehicle;
		script_check(pCurrent); // Here pCurrent shouldn't be NULL anyway
		int handle = CPools::GetVehiclePool()->GetIndex(pCurrent);
		if (handle != CTheScripts::StoreVehicleIndex && m_bIsMissionScript) {
			CVehicle* pOld = CPools::GetVehiclePool()->GetAt(CTheScripts::StoreVehicleIndex);
			if (pOld){
				CCarCtrl::RemoveFromInterestingVehicleList(pOld);
				if (pOld->VehicleCreatedBy == MISSION_VEHICLE && CTheScripts::StoreVehicleWasRandom){
					pOld->VehicleCreatedBy = RANDOM_VEHICLE;
					pOld->bIsLocked = false;
					CCarCtrl::NumRandomCars++;
					CCarCtrl::NumMissionCars--;
					CTheScripts::MissionCleanUp.RemoveEntityFromList(CTheScripts::StoreVehicleIndex, CLEANUP_CAR);
				}
			}

			CTheScripts::StoreVehicleIndex = handle;
			switch (pCurrent->VehicleCreatedBy) {
			case RANDOM_VEHICLE:
				pCurrent->VehicleCreatedBy = MISSION_VEHICLE;
				CCarCtrl::NumMissionCars++;
				CCarCtrl::NumRandomCars--;
				CTheScripts::StoreVehicleWasRandom = true;
				CTheScripts::MissionCleanUp.AddEntityToList(CTheScripts::StoreVehicleIndex, CLEANUP_CAR);
				break;
			case PARKED_VEHICLE:
				pCurrent->VehicleCreatedBy = MISSION_VEHICLE;
				CCarCtrl::NumMissionCars++;
				CCarCtrl::NumParkedCars--;
				CTheScripts::StoreVehicleWasRandom = true;
				CTheScripts::MissionCleanUp.AddEntityToList(CTheScripts::StoreVehicleIndex, CLEANUP_CAR);
				break;
			case MISSION_VEHICLE:
			case PERMANENT_VEHICLE:
				CTheScripts::StoreVehicleWasRandom = false;
				break;
			default:
				break;
			}
		}
		SET_INTEGER_PARAM(0, CTheScripts::StoreVehicleIndex);
		StoreParameters(&m_nIp, 1);
		return 0;
	}
	case COMMAND_IS_CHAR_IN_CAR:
	{
		CollectParameters(&m_nIp, 2);
		CPed* pPed = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		CVehicle* pCheckedVehicle = CPools::GetVehiclePool()->GetAt(GET_INTEGER_PARAM(1));
		CVehicle* pActualVehicle = pPed->bInVehicle ? pPed->m_pMyVehicle : nil;
		UpdateCompareFlag(pActualVehicle && pActualVehicle == pCheckedVehicle);
		return 0;
	}
	case COMMAND_IS_PLAYER_IN_CAR:
	{
		CollectParameters(&m_nIp, 2);
		CVehicle* pCheckedVehicle = CPools::GetVehiclePool()->GetAt(GET_INTEGER_PARAM(1));
		CPed* pPed = CWorld::Players[GET_INTEGER_PARAM(0)].m_pPed;
		UpdateCompareFlag(pPed->bInVehicle && pPed->m_pMyVehicle == pCheckedVehicle);
		return 0;
	}
	case COMMAND_IS_CHAR_IN_MODEL:
	{
		CollectParameters(&m_nIp, 2);
		CPed* pPed = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		CVehicle* pActualVehicle = pPed->bInVehicle ? pPed->m_pMyVehicle : nil;
		UpdateCompareFlag(pActualVehicle && pActualVehicle->GetModelIndex() == GET_INTEGER_PARAM(1));
		return 0;
	}
	case COMMAND_IS_PLAYER_IN_MODEL:
	{
		CollectParameters(&m_nIp, 2);
		CPed* pPed = CWorld::Players[GET_INTEGER_PARAM(0)].m_pPed;
		UpdateCompareFlag(pPed->bInVehicle && pPed->m_pMyVehicle->GetModelIndex() == GET_INTEGER_PARAM(1));
		return 0;
	}
	case COMMAND_IS_CHAR_IN_ANY_CAR:
	{
		CollectParameters(&m_nIp, 1);
		CPed* pPed = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		UpdateCompareFlag(pPed->bInVehicle && pPed->m_pMyVehicle);
		return 0;
	}
	case COMMAND_IS_PLAYER_IN_ANY_CAR:
	{
		CollectParameters(&m_nIp, 1);
		CPed* pPed = CWorld::Players[GET_INTEGER_PARAM(0)].m_pPed;
		UpdateCompareFlag(pPed->bInVehicle && pPed->m_pMyVehicle);
		return 0;
	}
	case COMMAND_IS_BUTTON_PRESSED:
	{
		CollectParameters(&m_nIp, 2);
		UpdateCompareFlag(GetPadState(GET_INTEGER_PARAM(0), GET_INTEGER_PARAM(1)) != 0);
		return 0;
	}
	/*
	case COMMAND_GET_PAD_STATE:
	{
		CollectParameters(&m_nIp, 1);
		SET_INTEGER_PARAM(0, GetPadState(GET_INTEGER_PARAM(0), GET_INTEGER_PARAM(1)));
		StoreParameters(&m_nIp, 1);
		return 0;
	}
	*/
	case COMMAND_LOCATE_PLAYER_ANY_MEANS_2D:
	case COMMAND_LOCATE_PLAYER_ON_FOOT_2D:
	case COMMAND_LOCATE_PLAYER_IN_CAR_2D:
	case COMMAND_LOCATE_STOPPED_PLAYER_ANY_MEANS_2D:
	case COMMAND_LOCATE_STOPPED_PLAYER_ON_FOOT_2D:
	case COMMAND_LOCATE_STOPPED_PLAYER_IN_CAR_2D:
		LocatePlayerCommand(command, &m_nIp);
		return 0;
	case COMMAND_LOCATE_PLAYER_ANY_MEANS_CHAR_2D:
	case COMMAND_LOCATE_PLAYER_ON_FOOT_CHAR_2D:
	case COMMAND_LOCATE_PLAYER_IN_CAR_CHAR_2D:
		LocatePlayerCharCommand(command, &m_nIp);
		return 0;
	case COMMAND_LOCATE_CHAR_ANY_MEANS_2D:
	case COMMAND_LOCATE_CHAR_ON_FOOT_2D:
	case COMMAND_LOCATE_CHAR_IN_CAR_2D:
	case COMMAND_LOCATE_STOPPED_CHAR_ANY_MEANS_2D:
	case COMMAND_LOCATE_STOPPED_CHAR_ON_FOOT_2D:
	case COMMAND_LOCATE_STOPPED_CHAR_IN_CAR_2D:
		LocateCharCommand(command, &m_nIp);
		return 0;
	case COMMAND_LOCATE_CHAR_ANY_MEANS_CHAR_2D:
	case COMMAND_LOCATE_CHAR_ON_FOOT_CHAR_2D:
	case COMMAND_LOCATE_CHAR_IN_CAR_CHAR_2D:
		LocateCharCharCommand(command, &m_nIp);
		return 0;
	case COMMAND_LOCATE_PLAYER_ANY_MEANS_3D:
	case COMMAND_LOCATE_PLAYER_ON_FOOT_3D:
	case COMMAND_LOCATE_PLAYER_IN_CAR_3D:
	case COMMAND_LOCATE_STOPPED_PLAYER_ANY_MEANS_3D:
	case COMMAND_LOCATE_STOPPED_PLAYER_ON_FOOT_3D:
	case COMMAND_LOCATE_STOPPED_PLAYER_IN_CAR_3D:
		LocatePlayerCommand(command, &m_nIp);
		return 0;
	case COMMAND_LOCATE_PLAYER_ANY_MEANS_CHAR_3D:
	case COMMAND_LOCATE_PLAYER_ON_FOOT_CHAR_3D:
	case COMMAND_LOCATE_PLAYER_IN_CAR_CHAR_3D:
		LocatePlayerCharCommand(command, &m_nIp);
		return 0;
	case COMMAND_LOCATE_CHAR_ANY_MEANS_3D:
	case COMMAND_LOCATE_CHAR_ON_FOOT_3D:
	case COMMAND_LOCATE_CHAR_IN_CAR_3D:
	case COMMAND_LOCATE_STOPPED_CHAR_ANY_MEANS_3D:
	case COMMAND_LOCATE_STOPPED_CHAR_ON_FOOT_3D:
	case COMMAND_LOCATE_STOPPED_CHAR_IN_CAR_3D:
		LocateCharCommand(command, &m_nIp);
		return 0;
	case COMMAND_LOCATE_CHAR_ANY_MEANS_CHAR_3D:
	case COMMAND_LOCATE_CHAR_ON_FOOT_CHAR_3D:
	case COMMAND_LOCATE_CHAR_IN_CAR_CHAR_3D:
		LocateCharCharCommand(command, &m_nIp);
		return 0;
	case COMMAND_CREATE_OBJECT:
	{
		CollectParameters(&m_nIp, 4);
		int mi = GET_INTEGER_PARAM(0) >= 0 ? GET_INTEGER_PARAM(0) : CTheScripts::UsedObjectArray[-GET_INTEGER_PARAM(0)].index;
		CObject* pObj = new CObject(mi, false);
		pObj->ObjectCreatedBy = MISSION_OBJECT;
		CVector pos = GET_VECTOR_PARAM(1);
		if (pos.z <= MAP_Z_LOW_LIMIT)
			pos.z = CWorld::FindGroundZForCoord(pos.x, pos.y);
		pos.z += pObj->GetDistanceFromCentreOfMassToBaseOfModel();
		pObj->SetPosition(pos);
		pObj->SetOrientation(0.0f, 0.0f, 0.0f);
		pObj->GetMatrix().UpdateRW();
		pObj->UpdateRwFrame();
		CTheScripts::ClearSpaceForMissionEntity(pos, pObj);
		CWorld::Add(pObj);
		SET_INTEGER_PARAM(0, CPools::GetObjectPool()->GetIndex(pObj));
		StoreParameters(&m_nIp, 1);
		if (m_bIsMissionScript)
			CTheScripts::MissionCleanUp.AddEntityToList(GET_INTEGER_PARAM(0), CLEANUP_OBJECT);
		return 0;
	}
	case COMMAND_DELETE_OBJECT:
	{
		CollectParameters(&m_nIp, 1);
		if (GET_INTEGER_PARAM(0) > (NUMOBJECTS + 2) << 8)
			return 0;
		CObject* pObj = CPools::GetObjectPool()->GetAt(GET_INTEGER_PARAM(0));
		if (pObj){
			CWorld::Remove(pObj);
			CWorld::RemoveReferencesToDeletedObject(pObj);
			delete pObj;
		}
		if (m_bIsMissionScript)
			CTheScripts::MissionCleanUp.RemoveEntityFromList(GET_INTEGER_PARAM(0), CLEANUP_OBJECT);
		return 0;
	}
	case COMMAND_ADD_SCORE:
		CollectParameters(&m_nIp, 2);
		CWorld::Players[GET_INTEGER_PARAM(0)].m_nMoney += GET_INTEGER_PARAM(1);
		if (CWorld::Players[GET_INTEGER_PARAM(0)].m_nMoney < 0)
			CWorld::Players[GET_INTEGER_PARAM(0)].m_nMoney = 0;
		return 0;
	case COMMAND_IS_SCORE_GREATER:
		CollectParameters(&m_nIp, 2);
		UpdateCompareFlag(CWorld::Players[GET_INTEGER_PARAM(0)].m_nMoney > GET_INTEGER_PARAM(1));
		return 0;
	case COMMAND_STORE_SCORE:
		CollectParameters(&m_nIp, 1);
		SET_INTEGER_PARAM(0, CWorld::Players[GET_INTEGER_PARAM(0)].m_nMoney);
		StoreParameters(&m_nIp, 1);
		return 0;
	case COMMAND_GIVE_REMOTE_CONTROLLED_CAR_TO_PLAYER:
	{
		CollectParameters(&m_nIp, 5);
		CVector pos = GET_VECTOR_PARAM(1);
		if (pos.z <= MAP_Z_LOW_LIMIT)
			pos.z = CWorld::FindGroundZForCoord(pos.x, pos.y);
		CVehicle* pVehicle = CRemote::GivePlayerRemoteControlledCar(pos.x, pos.y, pos.z, DEGTORAD(GET_FLOAT_PARAM(4)), MI_RCBANDIT);
		SET_INTEGER_PARAM(0, CPools::GetVehiclePool()->GetIndex(pVehicle));
		StoreParameters(&m_nIp, 1);
		return 0;
	}
	case COMMAND_ALTER_WANTED_LEVEL:
		CollectParameters(&m_nIp, 2);
		CWorld::Players[GET_INTEGER_PARAM(0)].m_pPed->SetWantedLevel(GET_INTEGER_PARAM(1));
		return 0;
	case COMMAND_ALTER_WANTED_LEVEL_NO_DROP:
		CollectParameters(&m_nIp, 2);
		CWorld::Players[GET_INTEGER_PARAM(0)].m_pPed->SetWantedLevelNoDrop(GET_INTEGER_PARAM(1));
		return 0;
	case COMMAND_IS_WANTED_LEVEL_GREATER:
		CollectParameters(&m_nIp, 2);
		UpdateCompareFlag(CWorld::Players[GET_INTEGER_PARAM(0)].m_pPed->m_pWanted->GetWantedLevel() > GET_INTEGER_PARAM(1));
		return 0;
	case COMMAND_CLEAR_WANTED_LEVEL:
		CollectParameters(&m_nIp, 1);
		CWorld::Players[GET_INTEGER_PARAM(0)].m_pPed->SetWantedLevel(0);
		return 0;
	case COMMAND_SET_DEATHARREST_STATE:
		CollectParameters(&m_nIp, 1);
		m_bDeatharrestEnabled = (GET_INTEGER_PARAM(0) == 1);
		return 0;
	case COMMAND_HAS_DEATHARREST_BEEN_EXECUTED:
		UpdateCompareFlag(m_bDeatharrestExecuted);
		return 0;
	/*
	case COMMAND_ADD_AMMO_TO_PLAYER:
	{
		CollectParameters(&m_nIp, 3);
		CWorld::Players[GET_INTEGER_PARAM(0)].m_pPed->GrantAmmo((eWeaponType)GET_INTEGER_PARAM(1), GET_INTEGER_PARAM(2));
		return 0;
	}
	*/
	case COMMAND_ADD_AMMO_TO_CHAR:
	{
		CollectParameters(&m_nIp, 3);
		CPed* pPed = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(pPed);
		pPed->GrantAmmo((eWeaponType)GET_INTEGER_PARAM(1), GET_INTEGER_PARAM(2));
		return 0;
	}
	//case COMMAND_ADD_AMMO_TO_CAR:
	case COMMAND_IS_PLAYER_STILL_ALIVE:
		CollectParameters(&m_nIp, 1);
		UpdateCompareFlag(CWorld::Players[GET_INTEGER_PARAM(0)].m_WBState != WBSTATE_WASTED);
		return 0;
	case COMMAND_IS_PLAYER_DEAD:
		CollectParameters(&m_nIp, 1);
		UpdateCompareFlag(CWorld::Players[GET_INTEGER_PARAM(0)].m_WBState == WBSTATE_WASTED);
		return 0;
	case COMMAND_IS_CHAR_DEAD:
	{
		CollectParameters(&m_nIp, 1);
		CPed* pPed = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		UpdateCompareFlag(!pPed || pPed->DyingOrDead() || pPed->m_objective == OBJECTIVE_LEAVE_CAR_AND_DIE);
		return 0;
	}
	case COMMAND_IS_CAR_DEAD:
	{
		CollectParameters(&m_nIp, 1);
		CVehicle* pVehicle = CPools::GetVehiclePool()->GetAt(GET_INTEGER_PARAM(0));
		UpdateCompareFlag(!pVehicle || pVehicle->GetStatus() == STATUS_WRECKED || pVehicle->bIsDrowning);
		return 0;
	}
	case COMMAND_SET_CHAR_THREAT_SEARCH:
	{
		CollectParameters(&m_nIp, 2);
		CPed* pPed = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(pPed);
		pPed->m_fearFlags |= GET_INTEGER_PARAM(1);
		return 0;
	}
	//case COMMAND_SET_CHAR_THREAT_REACTION:
	case COMMAND_SET_CHAR_OBJ_NO_OBJ:
	{
		CollectParameters(&m_nIp, 1);
		CPed* pPed = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(pPed);
		pPed->bScriptObjectiveCompleted = false;
		pPed->ClearObjective();
		return 0;
	}
	//case COMMAND_ORDER_DRIVER_OUT_OF_CAR:
	//case COMMAND_ORDER_CHAR_TO_DRIVE_CAR:
	//case COMMAND_ADD_PATROL_POINT:
	//case COMMAND_IS_PLAYER_IN_GANGZONE:
	case COMMAND_IS_PLAYER_IN_ZONE:
	{
		CollectParameters(&m_nIp, 1);
		CPlayerInfo* pPlayer = &CWorld::Players[GET_INTEGER_PARAM(0)];
		char label[12];
		CTheScripts::ReadTextLabelFromScript(&m_nIp, label);
		int zoneToCheck = CTheZones::FindZoneByLabelAndReturnIndex(label, ZONE_DEFAULT);
		if (zoneToCheck != -1)
			m_nIp += KEY_LENGTH_IN_SCRIPT; /* why only if zone != -1? */
		CVector pos = pPlayer->GetPos();
		CZone* pZone = CTheZones::GetNavigationZone(zoneToCheck);
		UpdateCompareFlag(CTheZones::PointLiesWithinZone(&pos, pZone));
		return 0;
	}
	case COMMAND_IS_PLAYER_PRESSING_HORN:
		CollectParameters(&m_nIp, 1);
		UpdateCompareFlag(CWorld::Players[GET_INTEGER_PARAM(0)].m_pPed->GetPedState() == PED_DRIVING &&
			CPad::GetPad(GET_INTEGER_PARAM(0))->GetHorn());
		return 0;
	case COMMAND_HAS_CHAR_SPOTTED_PLAYER:
	{
		CollectParameters(&m_nIp, 2);
		CPed* pPed = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(pPed);
		UpdateCompareFlag(pPed->OurPedCanSeeThisOne(CWorld::Players[GET_INTEGER_PARAM(1)].m_pPed));
		return 0;
	}
#ifdef SUPPORT_GINPUT_SCRIPT
	case COMMAND_HAS_PAD_IN_HANDS:
		UpdateCompareFlag(CPad::GetPad(0)->IsAffectedByController);
		return 0;
#else
	//case COMMAND_ORDER_CHAR_TO_BACKDOOR:
#endif
	//case COMMAND_ADD_CHAR_TO_GANG:
	case COMMAND_IS_CHAR_OBJECTIVE_PASSED:
	{
		CollectParameters(&m_nIp, 1);
		CPed* pPed = CPools::GetPedPool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(pPed);
		UpdateCompareFlag(pPed->bScriptObjectiveCompleted);
		return 0;
	}
	/* Not implemented.
	case COMMAND_SET_CHAR_DRIVE_AGGRESSION:
	case COMMAND_SET_CHAR_MAX_DRIVESPEED:
	*/
	case COMMAND_CREATE_CHAR_INSIDE_CAR:
	{
		CollectParameters(&m_nIp, 3);
		CVehicle* pVehicle = CPools::GetVehiclePool()->GetAt(GET_INTEGER_PARAM(0));
		script_assert(pVehicle);
		switch (GET_INTEGER_PARAM(2)) {
		case MI_COP:
			if (GET_INTEGER_PARAM(1) == PEDTYPE_COP)
				SET_INTEGER_PARAM(2, COP_STREET);
			break;
		case MI_SWAT:
			if (GET_INTEGER_PARAM(1) == PEDTYPE_COP)
				SET_INTEGER_PARAM(2, COP_SWAT);
			break;
		case MI_FBI:
			if (GET_INTEGER_PARAM(1) == PEDTYPE_COP)
				SET_INTEGER_PARAM(2, COP_FBI);
			break;
		case MI_ARMY:
			if (GET_INTEGER_PARAM(1) == PEDTYPE_COP)
				SET_INTEGER_PARAM(2, COP_ARMY);
			break;
		case MI_MEDIC:
			if (GET_INTEGER_PARAM(1) == PEDTYPE_EMERGENCY)
				SET_INTEGER_PARAM(2, PEDTYPE_EMERGENCY);
			break;
		case MI_FIREMAN:
			if (GET_INTEGER_PARAM(1) == PEDTYPE_FIREMAN)
				SET_INTEGER_PARAM(2, PEDTYPE_FIREMAN);
			break;
		default:
			break;
		}
		CPed* pPed;
		if (GET_INTEGER_PARAM(1) == PEDTYPE_COP)
			pPed = new CCopPed((eCopType)GET_INTEGER_PARAM(2));
		else if (GET_INTEGER_PARAM(1) == PEDTYPE_EMERGENCY || GET_INTEGER_PARAM(1) == PEDTYPE_FIREMAN)
			pPed = new CEmergencyPed(GET_INTEGER_PARAM(2));
		else
			pPed = new CCivilianPed((ePedType)GET_INTEGER_PARAM(1), GET_INTEGER_PARAM(2));
		pPed->CharCreatedBy = MISSION_CHAR;
		pPed->bRespondsToThreats = false;
		pPed->bAllowMedicsToReviveMe = false;
		pPed->bIsPlayerFriend = false;
		if (pVehicle->bIsBus)
			pPed->bRenderPedInCar = false;
		pPed->SetPosition(pVehicle->GetPosition());
		pPed->SetOrientation(0.0f, 0.0f, 0.0f);
		pPed->SetPedState(PED_DRIVING);
		script_assert(!pVehicle->pDriver);
		pVehicle->pDriver = pPed;
		pVehicle->pDriver->RegisterReference((CEntity**)&pVehicle->pDriver);
		pPed->m_pMyVehicle = pVehicle;
		pPed->m_pMyVehicle->RegisterReference((CEntity**)&pPed->m_pMyVehicle);
		pPed->bInVehicle = true;
		pVehicle->SetStatus(STATUS_PHYSICS);
		if (!pVehicle->IsBoat())
			pVehicle->AutoPilot.m_nCarMission = MISSION_CRUISE;
		pVehicle->bEngineOn = true;
		pPed->bUsesCollision = false;
		pPed->AddInCarAnims(pVehicle, true);
		pPed->m_nZoneLevel = CTheZones::GetLevelFromPosition(&pPed->GetPosition());
		CWorld::Add(pPed);
		CPopulation::ms_nTotalMissionPeds++;
		SET_INTEGER_PARAM(0, CPools::GetPedPool()->GetIndex(pPed));
		StoreParameters(&m_nIp, 1);
		if (m_bIsMissionScript)
			CTheScripts::MissionCleanUp.AddEntityToList(GET_INTEGER_PARAM(0), CLEANUP_CHAR);
		return 0;
	}
	case COMMAND_WARP_PLAYER_FROM_CAR_TO_COORD:
	{
		CollectParameters(&m_nIp, 4);
		CVector pos = GET_VECTOR_PARAM(1);
		CPlayerInfo* pPlayer = &CWorld::Players[GET_INTEGER_PARAM(0)];
		if (pos.z <= MAP_Z_LOW_LIMIT)
			pos.z = CWorld::FindGroundZForCoord(pos.x, pos.y);
		if (pPlayer->m_pPed->bInVehicle){
			script_assert(pPlayer->m_pPed->m_pMyVehicle);
			if (pPlayer->m_pPed->m_pMyVehicle->bIsBus)
				pPlayer->m_pPed->bRenderPedInCar = true;
			if (pPlayer->m_pPed->m_pMyVehicle->pDriver == pPlayer->m_pPed){
				pPlayer->m_pPed->m_pMyVehicle->RemoveDriver();
				pPlayer->m_pPed->m_pMyVehicle->SetStatus(STATUS_ABANDONED);
				pPlayer->m_pPed->m_pMyVehicle->bEngineOn = false;
				pPlayer->m_pPed->m_pMyVehicle->AutoPilot.m_nCruiseSpeed = 0;
			}else{
				pPlayer->m_pPed->m_pMyVehicle->RemovePassenger(pPlayer->m_pPed);
			}
		}
		pPlayer->m_pPed->RemoveInCarAnims();
		pPlayer->m_pPed->bInVehicle = false;
		pPlayer->m_pPed->m_pMyVehicle = nil;
		pPlayer->m_pPed->SetPedState(PED_IDLE);
		pPlayer->m_pPed->bUsesCollision = true;
		pPlayer->m_pPed->SetMoveSpeed(0.0f, 0.0f, 0.0f);
		pPlayer->m_pPed->ReplaceWeaponWhenExitingVehicle();
		if (pPlayer->m_pPed->m_pVehicleAnim)
			pPlayer->m_pPed->m_pVehicleAnim->blendDelta = -1000.0f;
		pPlayer->m_pPed->m_pVehicleAnim = nil;
		pPlayer->m_pPed->SetMoveState(PEDMOVE_NONE);
		CAnimManager::BlendAnimation(pPlayer->m_pPed->GetClump(), pPlayer->m_pPed->m_animGroup, ANIM_STD_IDLE, 1000.0f);
		pPlayer->m_pPed->RestartNonPartialAnims();
		AudioManager.PlayerJustLeftCar();
		pos.z += pPlayer->m_pPed->GetDistanceFromCentreOfMassToBaseOfModel();
		pPlayer->m_pPed->Teleport(pos);
		CTheScripts::ClearSpaceForMissionEntity(pos, pPlayer->m_pPed);
		return 0;
	}
	//case COMMAND_MAKE_CHAR_DO_NOTHING:
	default:
		script_bad();
		break;
	}
	return -1;
}

void CRunningScript::ReturnFromGosubOrFunction()
{
	uint32 val = m_nIp = m_anStack[--m_nStackPointer];
	if (!(m_nIp & BIT(STACKVALUE_IS_FUNCTION_CALL_BIT)))
		return;
	if (m_nIp & BIT(STACKVALUE_INVERT_RETURN_BIT))
		m_bCondResult = !m_bCondResult;
	m_nIp = m_nIp & STACKVALUE_IP_MASK;
	uint8 nInputParameters = CTheScripts::Read1ByteFromScript(&m_nIp);
	uint8 nOutputParameters = CTheScripts::Read1ByteFromScript(&m_nIp);
	uint8 nLocalsOffset = CTheScripts::Read1ByteFromScript(&m_nIp);
	for (int i = 0; i < nOutputParameters; i++)
		ScriptParams[i] = m_anLocalVariables[m_nLocalsPointer + nInputParameters + i];
	m_nIp += val >> STACKVALUE_IP_PARAMS_OFFSET;
	m_nLocalsPointer -= nLocalsOffset;
	StoreParameters(&m_nIp, nOutputParameters);
}

#ifdef MISSION_REPLAY

bool CRunningScript::CanAllowMissionReplay()
{
	if (AllowMissionReplay != MISSION_RETRY_STAGE_NORMAL)
		return false;
	for (int i = 0; i < ARRAY_SIZE(MissionScripts); i++) {
		if (!CGeneral::faststricmp(m_abScriptName, MissionScripts[i]))
			return true;
	}
	return false;
}

uint32 AddExtraDeathDelay()
{
	return 1000;
}

void RetryMission(int type, int unk)
{
	if (type == MISSION_RETRY_TYPE_SUGGEST_TO_PLAYER) {
		doingMissionRetry = true;
		FrontEndMenuManager.m_nCurrScreen = MENUPAGE_MISSION_RETRY;
		FrontEndMenuManager.m_bAttemptingMissionRetry = true;
		FrontEndMenuManager.RequestFrontEndStartUp();
	}
	else if (type == MISSION_RETRY_TYPE_BEGIN_RESTARTING) {
		doingMissionRetry = false;
		AllowMissionReplay = MISSION_RETRY_STAGE_START_RESTARTING;
		CTheScripts::MissionCleanUp.Process();
	}
}

#endif
