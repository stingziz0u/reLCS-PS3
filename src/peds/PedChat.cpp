#include "common.h"
#ifdef __PS3__
#include "ps3_platform.h"	// PS3_Logf
#include "Vehicle.h"
#endif
#include "Camera.h"
#include "DMAudio.h"
#include "General.h"
#include "Ped.h"

// Corresponds to ped sounds (from SOUND_PED_DEATH to SOUND_PED_TAXI_CALL)
PedAudioData CommentWaitTime[56] = {
  { 500, 800, 500, 2 },
  { 500, 800, 500, 2 },
  { 500, 800, 500, 2 },
  { 500, 800, 500, 2 },
  { 100, 2, 100, 2 },
  { 500, 500, 2000, 1000 },
  { 2000, 50, 2050, 1000 },
  { 5000, 2000, 7000, 3000 },
  { 5000, 2000, 7000, 3000 },
  { 300, 200, 500, 200 },
  { 3000, 1000, 4000, 1000 },
  { 6000, 6000, 6000, 6000 },
  { 4000, 1000, 5000, 1000 },
  { 3000, 1000, 4000, 1000 },
  { 1000, 1000, 2000, 2000 },
  { 1000, 500, 2000, 1500 },
  { 1700, 1000, 3000, 1000 },
  { 800, 200, 1000, 500 },
  { 800, 200, 1000, 500 },
  { 800, 400, 2000, 1000 },
  { 800, 400, 2000, 1000 },
  { 2000, 2000, 4000, 4000 },
  { 2000, 2000, 4000, 1000 },
  { 4000, 1000, 5000, 1000 },
  { 800, 400, 1200, 500 },
  { 5000, 1000, 6000, 2000 },
  { 5000, 1000, 6000, 2000 },
  { 5000, 1000, 6000, 2000 },
  { 5000, 1000, 6000, 2000 },
  { 5000, 1000, 6000, 2000 },
  { 5000, 1000, 6000, 2000 },
  { 5000, 1000, 6000, 2000 },
  { 4000, 2000, 7000, 2000 },
  { 1000, 300, 2000, 1000 },
  { 1500, 1000, 2500, 1000 },
  { 200, 200, 200, 200 },
  { 2000, 1000, 4000, 1000 },
  { 2000, 1000, 4000, 1000 },
  { 1000, 500, 3000, 1000 },
  { 1000, 500, 1000, 1000 },
  { 3000, 2000, 5000, 1000 },
  { 3000, 2000, 5000, 1000 },
  { 3000, 2000, 3000, 2000 },
  { 2000, 1000, 3000, 1000 },
  { 2500, 1000, 5000, 5000 },
  { 2000, 1000, 3000, 2000 },
  { 4000, 1000, 5000, 1000 },
  { 1000, 500, 2000, 4000 },
  { 1000, 500, 2000, 5000 },
  { 2000, 500, 2500, 500 },
  { 1000, 500, 3000, 2000 },
  { 1600, 1000, 2000, 2000 },
  { 2000, 1000, 4000, 2000 },
  { 1500, 1000, 2500, 1000 },
  { 1000, 1000, 5000, 5000 },
  { 0, 0, 0, 0 }
};

bool
CPed::ServiceTalkingWhenDead(void)
{
	return m_queuedSound == SOUND_PED_DEATH;
}

void
CPed::ServiceTalking(void)
{
	if (bBodyPartJustCameOff && m_bodyPartBleeding == PED_HEAD)
		return;

	if (!CGame::germanGame && m_pFire)
		m_queuedSound = SOUND_PED_BURNING;

	if (m_queuedSound != SOUND_NO_SOUND) {
		if (m_queuedSound == SOUND_PED_DEATH)
			m_soundStart = CTimer::GetTimeInMilliseconds() - 1;

		if (CTimer::GetTimeInMilliseconds() > m_soundStart) {
			DMAudio.PlayOneShot(m_audioEntityId, m_queuedSound, 1.0f);
			m_lastSoundStart = CTimer::GetTimeInMilliseconds();
			m_soundStart =
				CommentWaitTime[m_queuedSound - SOUND_PED_DEATH].m_nFixedDelayTime
				+ CTimer::GetTimeInMilliseconds()
				+ CGeneral::GetRandomNumberInRange(0, CommentWaitTime[m_queuedSound - SOUND_PED_DEATH].m_nOverrideFixedDelayTime);

			if (m_queuedSound == SOUND_PED_PLAYER_BEFORESEX && IsPlayer())
				m_soundStart += 2000;

			m_lastQueuedSound = m_queuedSound;
			m_queuedSound = SOUND_NO_SOUND;
		}
	}
}

#ifdef __PS3__
extern uint32 gPS3SayWatchUntil;	// Ped.cpp (patch 40)
#endif

void
CPed::Say(uint16 audio)
{
#ifdef __PS3__
	// patch 41: patch 40 only watched Say(sound, delay); the taxi passenger's
	// scream (SFX_MALE_PANIC_*: SOUND_PED_FLEE_SPRINT) comes through here
	if (CTimer::GetTimeInMilliseconds() < gPS3SayWatchUntil && !IsPlayer()) {
		CVehicle *v = m_pMyVehicle;
		PS3_Logf("[sfx] ped %d (%s, created by %d) says %d: state %d, objective %d, in a vehicle %d, held hostage %d, "
		         "flee after exiting %d, flee scream %d, flee when standing %d; vehicle speed %.4f turn %.4f %.4f %.4f",
		         GetModelIndex(), CModelInfo::GetModelInfo(GetModelIndex())->GetModelName(), CharCreatedBy, audio,
		         m_nPedState, m_objective, bInVehicle, bHeldHostageInCar, bFleeAfterExitingCar, bMakeFleeScream,
		         bFleeWhenStanding, v ? v->m_vecMoveSpeed.Magnitude() : -1.0f, v ? v->m_vecTurnSpeed.x : 0.0f,
		         v ? v->m_vecTurnSpeed.y : 0.0f, v ? v->m_vecTurnSpeed.z : 0.0f);
	}
#endif
	if (3.0f + TheCamera.GetPosition().z < GetPosition().z)
		return;

	if (TheCamera.m_CameraAverageSpeed > 1.65f) {
		if (audio != SOUND_PED_DAMAGE && audio != SOUND_PED_HIT && audio != SOUND_PED_LAND)
			return;

	} else if (TheCamera.m_CameraAverageSpeed > 1.25f) {
		if (audio != SOUND_PED_DEATH &&
			audio != SOUND_PED_DAMAGE && audio != SOUND_PED_HIT && audio != SOUND_PED_LAND &&
			audio != SOUND_PED_TAXI_WAIT && audio != SOUND_PED_EVADE)
			return;

	} else if (TheCamera.m_CameraAverageSpeed > 0.9f) {
		switch (audio) {
			case SOUND_PED_DEATH:
			case SOUND_PED_DAMAGE:
			case SOUND_PED_HIT:
			case SOUND_PED_LAND:
			case SOUND_PED_BURNING:
			case SOUND_PED_FLEE_SPRINT:
			case SOUND_PED_TAXI_WAIT:
			case SOUND_PED_EVADE:
			case SOUND_PED_CRASH_VEHICLE:
			case SOUND_PED_CRASH_CAR:
			case SOUND_PED_ANNOYED_DRIVER:
				break;
			default:
				return;
		}
	}

	if (audio < m_queuedSound) {
		if (audio != m_lastQueuedSound || audio == SOUND_PED_DEATH

		// See VC Ped Speech patch
#ifdef FIX_BUGS
			|| CommentWaitTime[audio - SOUND_PED_DEATH].m_nOverrideMaxRandomDelayTime
				+ (uint32)CGeneral::GetRandomNumberInRange(0, CommentWaitTime[audio - SOUND_PED_DEATH].m_nMaxRandomDelayTime)
#else
			|| CommentWaitTime[m_queuedSound - SOUND_PED_DEATH].m_nOverrideMaxRandomDelayTime
			+ (uint32)CGeneral::GetRandomNumberInRange(0, CommentWaitTime[m_queuedSound - SOUND_PED_DEATH].m_nMaxRandomDelayTime)
#endif
				+ m_lastSoundStart <= CTimer::GetTimeInMilliseconds()) {
			m_queuedSound = audio;
		}
	}
}