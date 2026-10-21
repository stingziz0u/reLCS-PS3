#include "common.h"

#include "FinalMissionMusic.h"

#include "Camera.h"
#include "Font.h"
#include "KeyGen.h"
#include "ModelInfo.h"
#include "MusicManager.h"
#include "Ped.h"
#include "Pools.h"
#include "Replay.h"
#include "Script.h"
#include "Timer.h"
#include "Vehicle.h"
#include "World.h"
#include "sampman.h"

namespace FinalMissionMusic
{
#ifdef __PS3__
// reLCS-PS3: the 3DS port's added "Chase" music isn't the original game's
// (and its files aren't in the LCS data): off. What it does for the finale
// besides the music (the chase boat that mustn't blow up at the lighthouse,
// the boat checkpoint) runs all the same.
int8 Enabled = 0;
#else
int8 Enabled = 1;
#endif
namespace
{
	enum eTrackState
	{
		TRACK_STOPPED,
		TRACK_FM,
		TRACK_LOOP,
		TRACK_CLIMAX_FADING,
		TRACK_CLIMAX_SILENT,
		TRACK_FADING
	};

	static const char *LCS_FM_PATH = "AUDIO\\MUSIC\\CHASE_FM.WAV";
	static const char *LCS_LOOP_PATH = "AUDIO\\MUSIC\\CHASE_LOOP.WAV";
	static const uint32 FADE_OUT_TIME_MS = 1000;
	static const uint32 CLIMAX_FADE_OUT_TIME_MS = 1500;

	bool gLCSFinaleActive;
	eTrackState gTrackState = TRACK_STOPPED;
	uint32 gFadeStartTime;
	bool gStreamStarted;
	bool gWasInCar;
	uint32 gTrackNameFrames;
	bool gBoatChaseActive;
	bool gLighthouseTransition;
	bool gLighthouseBattleActive;
	bool gBoatCheckpointArmed;
	int32 gSalvatoreChaseBoat = -1;

	bool
	IsSalvatore(CPed *ped)
	{
		if (!ped)
			return false;
		const uint32 modelKey = CModelInfo::GetModelInfo(ped->GetModelIndex())->GetNameHashKey();
		return modelKey == CKeyGen::GetUppercaseKey("SAL_CON") ||
		       modelKey == CKeyGen::GetUppercaseKey("SAL_01");
	}

	void
	RememberSalvatoreChaseBoat()
	{
		/* Toni occupies a scripted gunner position during SALH5, so the regular
		 * player-vehicle fields are not a reliable identity for this boat. The
		 * mission boat is the one carrying Salvatore; retain its pool handle even
		 * if the script detaches its occupants immediately before the transition. */
		for (int32 i = 0; i < CPools::GetVehiclePool()->GetSize(); i++) {
			CVehicle *candidate = CPools::GetVehiclePool()->GetSlot(i);
			if (!candidate || candidate->GetVehicleAppearance() != VEHICLE_APPEARANCE_BOAT)
				continue;
			if (IsSalvatore(candidate->pDriver)) {
				gSalvatoreChaseBoat = CPools::GetVehiclePool()->GetIndex(candidate);
				return;
			}
			for (int32 j = 0; j < ARRAY_SIZE(candidate->pPassengers); j++)
				if (IsSalvatore(candidate->pPassengers[j])) {
					gSalvatoreChaseBoat = CPools::GetVehiclePool()->GetIndex(candidate);
					return;
				}
		}

		/* Script-created occupants can briefly be between seat states while still
		 * retaining m_pMyVehicle. Catch that frame without using position/camera
		 * guesses that could select an enemy boat. */
		for (int32 i = 0; i < CPools::GetPedPool()->GetSize(); i++) {
			CPed *ped = CPools::GetPedPool()->GetSlot(i);
			if (IsSalvatore(ped) && ped->m_pMyVehicle &&
			    ped->m_pMyVehicle->GetVehicleAppearance() == VEHICLE_APPEARANCE_BOAT) {
				gSalvatoreChaseBoat = CPools::GetVehiclePool()->GetIndex(ped->m_pMyVehicle);
				return;
			}
		}
	}

	void
	SetChaseBoatDamageable(bool damageable)
	{
		if (gSalvatoreChaseBoat < 0)
			return;
		CVehicle *vehicle = CPools::GetVehiclePool()->GetAt(gSalvatoreChaseBoat);
		if (vehicle && vehicle->GetVehicleAppearance() == VEHICLE_APPEARANCE_BOAT)
			vehicle->bCanBeDamaged = damageable;
	}

	bool
	StartTrack(const char *path, bool loop)
	{
		SampleManager.StopMissionMusicStream();
		gStreamStarted = Enabled && SampleManager.StartMissionMusicStream(path, loop);
		return gStreamStarted;
	}

	void
	SetLighthouseGunnersOnPlayer()
	{
		if (!gLighthouseBattleActive)
			return;
		CPed *player = FindPlayerPed();
		if (!player)
			return;
		for (int32 i = 0; i < CPools::GetPedPool()->GetSize(); i++) {
			CPed *ped = CPools::GetPedPool()->GetSlot(i);
			if (!ped || ped == player || ped->DyingOrDead() || !ped->m_attachedTo ||
			    !ped->m_attachedTo->IsVehicle())
				continue;
			CVehicle *vehicle = (CVehicle*)ped->m_attachedTo;
			if (!vehicle->IsRealHeli())
				continue;

			/* SALH5 only points its two attached helicopter gunners at Toni and
			 * sets the stop-and-shoot flags.  This engine path does not turn that
			 * look target into a combat objective, so they otherwise remain idle.
			 * Attached peds explicitly support the kill objective. */
			ped->SetObjective(OBJECTIVE_KILL_CHAR_ANY_MEANS, player);
		}
	}

	void
	ApplyVolume(uint8 volume)
	{
		/* Scripted LCS scenes use the widescreen camera rather than the regular
		 * cutscene manager. Keep the stream running and duck it to exactly 50%. */
		if (TheCamera.m_WideScreenOn)
			volume /= 2;
		SampleManager.SetMissionMusicStreamVolume(volume);
	}
}

void
ArmLCSFinaleBoatCheckpoint()
{
	gBoatCheckpointArmed = true;
}

void
CancelLCSFinaleBoatCheckpoint()
{
	gBoatCheckpointArmed = false;
}

bool
ConsumeLCSFinaleBoatCheckpointRequest()
{
	if (!gBoatCheckpointArmed)
		return false;

	/* Consume SKIP only at SALH5's real boat-checkpoint selector ($535).
	 * This mirrors the value written when the player reaches the docks and the
	 * value retained by the native hospital-taxi retry path. */
	gBoatCheckpointArmed = false;
	return true;
}

void
BeginLCSFinale()
{
	SampleManager.StopMissionMusicStream();
	gLCSFinaleActive = true;
	gTrackState = TRACK_STOPPED;
	gFadeStartTime = 0;
	gStreamStarted = false;
	gWasInCar = false;
	gTrackNameFrames = 0;
	gBoatChaseActive = false;
	gLighthouseTransition = false;
	gLighthouseBattleActive = false;
	/* Keep an armed SKIP request until SALH5 reaches its native death/arrest
	 * checkpoint gate. EnterLCSBoatChase clears the consumed state after SALH515
	 * proves that the boat stage has actually started. */
	gSalvatoreChaseBoat = -1;
	/* SCRIPT_NAME runs as soon as the mission is accepted, before its opening
	 * cutscene.  SALH510 starts the playable section and calls the function
	 * below; keeping this function silent also makes cutscene skipping safe. */
}

void
StartLCSFinaleFM()
{
	if (!gLCSFinaleActive || gTrackState != TRACK_STOPPED)
		return;
	gTrackState = TRACK_FM;
	StartTrack(LCS_FM_PATH, false);
}

void
SwitchLCSFinaleToLoop()
{
	if (!gLCSFinaleActive || gTrackState == TRACK_LOOP || gTrackState == TRACK_FADING)
		return;
	gTrackState = TRACK_LOOP;
	StartTrack(LCS_LOOP_PATH, true);
}

void
EnterLCSBoatChase()
{
	if (!gLCSFinaleActive)
		return;
	gBoatChaseActive = true;
	gBoatCheckpointArmed = false;
	gLighthouseTransition = false;
	StartLCSFinaleFM();
}

void
BeginLCSLighthouseTransition()
{
	if (!gLCSFinaleActive)
		return;
	RememberSalvatoreChaseBoat();
	gLighthouseTransition = true;
	SetChaseBoatDamageable(false);
	/* SALH522 is still part of the boat chase.  Keep the player boat protected
	 * for the native stage cleanup here, but leave the music playing. */
}

void
BeginLCSLighthouseClimax()
{
	if (!gLCSFinaleActive || gLighthouseBattleActive ||
	    gTrackState == TRACK_CLIMAX_FADING || gTrackState == TRACK_CLIMAX_SILENT)
		return;
	/* SALH53A is the first line of the Massimo/Salvatore confrontation before
	 * the helicopter fight.  Fade through this scene, then SALH5TD hard-cuts
	 * to the edited LOOP whether the scene played normally or was skipped. */
	if (!gStreamStarted || gTrackState == TRACK_STOPPED)
		gTrackState = TRACK_CLIMAX_SILENT;
	else {
		gTrackState = TRACK_CLIMAX_FADING;
		gFadeStartTime = CTimer::GetTimeInMilliseconds();
	}
}

void
BeginLCSLighthouseBattle()
{
	if (!gLCSFinaleActive || gLighthouseBattleActive)
		return;
	gLighthouseBattleActive = true;
	/* SALH5TD is emitted after the lighthouse cutscene.  LOOP is an edited
	 * climax segment, so this transition deliberately restarts it even when
	 * the short test FM caused LOOP to begin earlier in the boat chase. */
	gTrackState = TRACK_LOOP;
	StartTrack(LCS_LOOP_PATH, true);
}

bool
ShouldSuppressLCSBoatExplosion(CVehicle *vehicle)
{
	return gLCSFinaleActive && gLighthouseTransition && vehicle &&
	       vehicle->GetVehicleAppearance() == VEHICLE_APPEARANCE_BOAT &&
	       CPools::GetVehiclePool()->GetIndex(vehicle) == gSalvatoreChaseBoat;
}

void
FadeOutLCSFinale()
{
	if (!gLCSFinaleActive || gTrackState == TRACK_STOPPED || gTrackState == TRACK_FADING)
		return;
	gTrackState = TRACK_FADING;
	gFadeStartTime = CTimer::GetTimeInMilliseconds();
}

void
EndLCSFinale()
{
	SetChaseBoatDamageable(true);
	if (gLCSFinaleActive || gStreamStarted)
		SampleManager.StopMissionMusicStream();
	gLCSFinaleActive = false;
	gTrackState = TRACK_STOPPED;
	gFadeStartTime = 0;
	gStreamStarted = false;
	gWasInCar = false;
	gTrackNameFrames = 0;
	gBoatChaseActive = false;
	gLighthouseTransition = false;
	gLighthouseBattleActive = false;
	gBoatCheckpointArmed = false;
	gSalvatoreChaseBoat = -1;
}

void
Update()
{
	if (!gLCSFinaleActive)
		return;

	if (gBoatChaseActive && !gLighthouseTransition) {
		RememberSalvatoreChaseBoat();
	}
	SetLighthouseGunnersOnPlayer();

	/* A manual pause behaves like the radio. Scripted camera/cutscene pauses do
	 * not stop the finale track; they use the 50% volume duck below. */
	// Music is optional; LCS mission repairs above must continue to run.
	static bool wasEnabled = true;
	if (!Enabled) {
		if (gStreamStarted) SampleManager.StopMissionMusicStream();
		gStreamStarted = false;
		wasEnabled = false;
		return;
	}
	if (!wasEnabled) {
		wasEnabled = true;
		if (!gStreamStarted && gTrackState == TRACK_FM) StartTrack(LCS_FM_PATH, false);
		else if (!gStreamStarted && gTrackState == TRACK_LOOP) StartTrack(LCS_LOOP_PATH, true);
	}
	const bool manuallyPaused = CTimer::GetIsUserPaused();
	SampleManager.PauseMissionMusicStream(manuallyPaused);

	if (!manuallyPaused && gTrackState == TRACK_FM && gStreamStarted &&
	    SampleManager.HasMissionMusicStreamFinished())
		SwitchLCSFinaleToLoop();

	uint8 volume = MAX_VOLUME;
	if (gTrackState == TRACK_CLIMAX_FADING) {
		const uint32 elapsed = CTimer::GetTimeInMilliseconds() - gFadeStartTime;
		if (elapsed >= CLIMAX_FADE_OUT_TIME_MS) {
			SampleManager.StopMissionMusicStream();
			gTrackState = TRACK_CLIMAX_SILENT;
			gStreamStarted = false;
			return;
		}
		volume = (MAX_VOLUME * (CLIMAX_FADE_OUT_TIME_MS - elapsed)) /
		         CLIMAX_FADE_OUT_TIME_MS;
	} else if (gTrackState == TRACK_FADING) {
		const uint32 elapsed = CTimer::GetTimeInMilliseconds() - gFadeStartTime;
		if (elapsed >= FADE_OUT_TIME_MS) {
			SampleManager.StopMissionMusicStream();
			gTrackState = TRACK_STOPPED;
			gStreamStarted = false;
			return; // Keep the radio locked until SALH5 itself ends.
		}
		volume = (MAX_VOLUME * (FADE_OUT_TIME_MS - elapsed)) / FADE_OUT_TIME_MS;
	}

	if (gTrackState != TRACK_STOPPED)
		ApplyVolume(volume);
}

bool
IsFinaleActive()
{
	return gLCSFinaleActive;
}

bool
IsRadioLocked()
{
	return Enabled && gLCSFinaleActive;
}

void
DisplayTrackName()
{
	if (!Enabled) return;
	if (!gLCSFinaleActive)
		return;

	const bool inCar = MusicManager.PlayerInCar();
	if (CTimer::GetIsPaused() || TheCamera.m_WideScreenOn ||
	    CReplay::IsPlayingBack() || !inCar) {
		gWasInCar = false;
		gTrackNameFrames = 0;
		return;
	}

	if (!gWasInCar) {
		gWasInCar = true;
		gTrackNameFrames = 60;
	} else if (gTrackNameFrames != 0) {
#ifdef FIX_BUGS
		const uint32 passed = CTimer::GetLogicalFramesPassed();
		gTrackNameFrames = passed >= gTrackNameFrames ? 0 : gTrackNameFrames - passed;
#else
		gTrackNameFrames--;
#endif
	}
	if (gTrackNameFrames == 0)
		return;

	static wchar title[] = { '\'', 'C', 'h', 'a', 's', 'e', '\'', 0 };
	CFont::SetJustifyOff();
	CFont::SetBackgroundOff();
	CFont::SetDropShadowPosition(2);
	CFont::SetScale(PSP_SCREEN_SCALE_X(0.5f), PSP_SCREEN_SCALE_Y(0.88f));
	CFont::SetPropOn();
	CFont::SetFontStyle(FONT_BANK);
	CFont::SetCentreOn();
	CFont::SetCentreSize(PSP_SCREEN_SCALE_X(260.0f));
	CFont::SetDropColor(CRGBA(0, 0, 0, 255));
	CFont::SetColor(CRGBA(77, 155, 210, 255));
	CFont::PrintString(SCREEN_WIDTH / 2, PSP_SCREEN_SCALE_Y(7.0f), title);
	CFont::DrawFonts();
	CFont::SetCentreSize(SCREEN_STRETCH_X(DEFAULT_SCREEN_WIDTH));
}
}
