// Mission spawner (patch 74, only with USRDIR/missionspawner.txt): L3 + Select
// together opens it, Left/Right go through the missions and the odd jobs
// (races, Bumps and Grinds, the RC ones, the vehicle jobs...), L1/R1 by 10
// (held = repeat), X starts the one shown, O closes. A mission that is
// running ends first (its cleanup), then Toni goes where the one chosen
// starts, in its vehicle if it has one (the autotest's table, ps3_autotest.cpp)
// and it starts. A testing tool, as the car spawner (ps3_carspawn.cpp).

#include "common.h"
#include "ps3_platform.h"
#include "ps3_glfw.h"
#include "Pad.h"
#include "Timer.h"
#include "Font.h"
#include "Text.h"
#include "World.h"
#include "PlayerPed.h"
#include "Script.h"
#include "Camera.h"
#include "CutsceneMgr.h"
#include "Replay.h"
#include "Frontend.h"

void PS3_MissionScriptName(int idx, char *out);	// ps3_autotest.cpp
void PS3_LaunchMission(int idx);		// ps3_autotest.cpp

#define BTN(b) (1u << (b))
#define B_CROSS  BTN(GLFW_GAMEPAD_BUTTON_A)
#define B_CIRCLE BTN(GLFW_GAMEPAD_BUTTON_B)
#define B_L1     BTN(GLFW_GAMEPAD_BUTTON_LEFT_BUMPER)
#define B_R1     BTN(GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER)
#define B_L3     BTN(GLFW_GAMEPAD_BUTTON_LEFT_THUMB)
#define B_SELECT BTN(GLFW_GAMEPAD_BUTTON_BACK)
#define B_LEFT   BTN(GLFW_GAMEPAD_BUTTON_DPAD_LEFT)
#define B_RIGHT  BTN(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT)

// main.scm's mission scripts from 9 on (0-8 set up a new game), as reLCS's
// debug menu names them
#define FIRST_MISSION 9
static const char *const missionNames[] = {
	"Home Sweet Home", "Taxi Driver", "Paramedic", "Vigilante", "Karmageddon (odd job)", "Firefighter",
	"Trash Dash", "RC Triad Take-Down", "Thrashin' RC", "Ragin' RC", "Chasin' RC", "GO GO Faggio",
	"Noodle Punk", "Pizza Boy", "Wong Side of the Tracks", "Bumps and Grinds: Course 1",
	"Bumps and Grinds: Course 2", "Bumps and Grinds: Course 3", "Bumps and Grinds: Course 4",
	"Bumps and Grinds: Course 5", "Bumps and Grinds: Course 6", "Bumps and Grinds: Course 7",
	"Bumps and Grinds: Course 8", "Bumps and Grinds: Course 9", "Bumps and Grinds: Course 10",
	"Car Salesman", "Bike Salesman", "Race: Low Rider Rumble", "Race: Deimos Dash",
	"Race: Wi-Cheetah Run", "Race: Red Light Racing", "Race: Torrington TT", "Race: Gangsta GP",
	"Scooter Shooter", "AWOL Angel", "9mm Mayhem", "Scrapyard Challenge",
	"See the Sight Before your Flight", "SlashTV",
	"Slacker (Vincenzo)", "Dealing Revenge (Vincenzo)", "Snuff (Vincenzo)", "Smash and Grab (Vincenzo)",
	"Hot Wheels (Vincenzo)", "The Portland Chainsaw Masquerade (Vincenzo)",
	"The Offer (Salvatore)", "Ho Selecta! (Salvatore)", "Frighteners (Salvatore)",
	"Rollercoaster Ride (Salvatore)", "Contra-Banned (Salvatore)", "Sindacco Sabotage (Salvatore)",
	"The Trouble with Triads (Salvatore)", "Driving Mr. Leone (Salvatore)",
	"JD's conversation", "Bone Voyeur! (JD)", "Don in 60 Seconds (JD)", "A Volatile Situation (JD)",
	"Blow up 'Dolls' (JD)", "Salvatore's Salvation (JD)", "The Guns of Leone (JD)",
	"Calm before the Storm (JD)", "The Made Man (JD)",
	"Snappy Dresser (Ma Cipriani)", "Big Rumble in Little China (Ma Cipriani)",
	"Grease Sucho (Ma Cipriani)", "Dead Meat (Ma Cipriani)", "No Son of Mine (Ma Cipriani)",
	"Shop 'til you Strop (Maria)", "Taken for a Ride (Maria)", "Booby Prize (Maria)",
	"Biker Heat (Maria)", "Overdose of Trouble (Maria)",
	"Making Toni (Salvatore)", "A Walk in the Park (Salvatore)", "Caught in the Act (Salvatore)",
	"Search and Rescue (Salvatore)", "Taking the Peace (Salvatore)", "Shoot the Messenger (Salvatore)",
	"Sayonara Sindaccos (Leon McAffrey)", "The Whole 9 Yardies (Leon McAffrey)",
	"Crazy '69' (Leon McAffrey)", "Night of the Livid Dreads (Leon McAffrey)",
	"Munitions Dump (Leon McAffrey)",
	"The Morgue Party Candidate (Donald Love)", "Steering the Vote (Donald Love)",
	"Cam-Pain (Donald Love)", "Friggin' the Riggin' (Donald Love)", "Love & Bullets (Donald Love)",
	"Counterfeit Count (Donald Love)", "Love on the Rocks (Donald Love)",
	"L.C. Confidential (Church Confessional)", "The Passion of the Heist (Church Confessional)",
	"Karmageddon (Church Confessional)", "False Idols (Church Confessional)",
	"Rough Justice (Salvatore)", "Dead Reckoning (Salvatore)", "Shogun Showdown (Salvatore)",
	"The Shoreside Redemption (Salvatore)", "The Sicilian Gambit (Salvatore)",
	"Panlantic Land Grab (Donald Love)", "Stop the Press (Donald Love)",
	"Morgue Party Resurrection (Donald Love)", "No Money, Mo' Problems (Donald Love)",
	"Bringing the House Down (Donald Love)", "Love on the Run (Donald Love)",
	"More Deadly than the Male (Toshiko Kasen)", "Cash Clash (Toshiko Kasen)",
	"A Date with Death (Toshiko Kasen)", "Cash in Kazuki's Chips (Toshiko Kasen)",
};

enum { MS_CLOSED, MS_OPEN, MS_ENDING, MS_RELEASE };	// ENDING: the running mission ends first; RELEASE: pad held back until the buttons are up

static int msState;
static int msCount;	// missions in the list
static int msSel;
static unsigned msPrev;
static int msRepeatDir;
static uint32 msRepeatAt;
static uint32 msLaunchAt;
static wchar msName[96];
static wchar msHint[120];
static wchar msNote[80];
static uint32 msNoteUntil;

static void
Note(const char *msg)
{
	AsciiToUnicode(msg, msNote);
	msNoteUntil = CTimer::GetTimeInMilliseconds() + 2500;
}

static void
MakeName(void)
{
	int idx = FIRST_MISSION + msSel;
	char script[16];
	PS3_MissionScriptName(idx, script);
	char buf[96];
	snprintf(buf, sizeof(buf), "%d: %s%s%s%s", idx, missionNames[msSel],
	         script[0] ? " [" : "", script, script[0] ? "]" : "");
	AsciiToUnicode(buf, msName);
	char hint[120];
	snprintf(hint, sizeof(hint), "Left/Right: mission   L1/R1: 10   X: start   O: close   (%d/%d)", msSel + 1, msCount);
	AsciiToUnicode(hint, msHint);
}

static void
Move(int d)
{
	msSel = ((msSel + d) % msCount + msCount) % msCount;
	MakeName();
}

// Why it can't open now, nil if it can
static const char *
Blocked(CPlayerPed *p)
{
	if (CCutsceneMgr::IsRunning() || CReplay::IsPlayingBack())
		return "Mission spawner: not now";
	if (CWorld::Players[CWorld::PlayerInFocus].m_WBState != WBSTATE_PLAYING || p->DyingOrDead())
		return "Mission spawner: not now";
	return nil;
}

static void
Consume(void)
{
	CPad::GetPad(0)->NewState.Clear();
}

static void
Launch(void)
{
	int idx = FIRST_MISSION + msSel;
	PS3_Logf("[missions] mission spawner: %d \"%s\"", idx, missionNames[msSel]);
	PS3_LaunchMission(idx);
}

// Off unless USRDIR/missionspawner.txt exists (looked at once, at the first frame)
static int msEnabled = -1;

static bool
Enabled(void)
{
	if (msEnabled < 0) {
		FILE *f = fopen(PS3_USRDIR "/missionspawner.txt", "rb");
		msEnabled = f != nil;
		if (f)
			fclose(f);
		PS3_Logf("[missions] mission spawner %s", msEnabled ? "on (missionspawner.txt)" : "off (no USRDIR/missionspawner.txt)");
	}
	return msEnabled;
}

// CPad::UpdatePads, after pad 0 is read
void
PS3_MissionSpawnerPad(void)
{
	if (FrontEndMenuManager.m_bMenuActive || !Enabled())
		return;
	CPlayerPed *p = FindPlayerPed();
	unsigned m = PS3_PadButtons();
	unsigned down = m & ~msPrev;
	msPrev = m;
	if (p == nil) {
		msState = MS_CLOSED;
		return;
	}

	switch (msState) {
	case MS_CLOSED:
		if ((m & B_L3) && (m & B_SELECT) && (down & (B_L3 | B_SELECT))) {
			const char *why = Blocked(p);
			if (why) {
				Note(why);
				msState = MS_RELEASE;
				Consume();
				return;
			}
			if (msCount == 0) {
				msCount = Min((int)ARRAY_SIZE(missionNames), CTheScripts::NumberOfMissionScripts - FIRST_MISSION);
				if (msCount <= 0) {
					msCount = 0;
					Note("Mission spawner: no missions in main.scm");
					msState = MS_RELEASE;
					Consume();
					return;
				}
				if (msSel >= msCount)
					msSel = 0;
			}
			msState = MS_OPEN;
			msNoteUntil = 0;
			msRepeatDir = 0;
			MakeName();
			Consume();
		}
		return;

	case MS_OPEN: {
		if (Blocked(p)) {	// busted, wasted, a cutscene started
			msState = MS_RELEASE;
			Consume();
			return;
		}
		uint32 now = CTimer::GetTimeInMilliseconds();
		if (down & B_CIRCLE) {
			msState = MS_RELEASE;
		} else if (down & B_CROSS) {
			if (CTheScripts::IsPlayerOnAMission()) {
				// the running one ends (its cleanup) and a second later the new one starts
				CTheScripts::EndMissionScripts();
				msLaunchAt = now + 1000;
				msState = MS_ENDING;
				Note("Mission spawner: ending the mission running...");
			} else {
				Launch();
				msState = MS_RELEASE;
			}
		} else {
			int dir = 0;
			if (down & B_LEFT) dir = -1;
			else if (down & B_RIGHT) dir = 1;
			else if (down & B_L1) dir = -10;
			else if (down & B_R1) dir = 10;
			if (dir) {
				Move(dir);
				msRepeatDir = dir;
				msRepeatAt = now + 400;
			} else if (msRepeatDir) {
				unsigned key = msRepeatDir == -1 ? B_LEFT : msRepeatDir == 1 ? B_RIGHT :
				               msRepeatDir < 0 ? B_L1 : B_R1;
				if (!(m & key))
					msRepeatDir = 0;
				else if ((int32)(now - msRepeatAt) >= 0) {
					Move(msRepeatDir);
					msRepeatAt = now + (msRepeatDir == 1 || msRepeatDir == -1 ? 80 : 200);
				}
			}
		}
		Consume();
		return;
	}

	case MS_ENDING:
		// the pad is the player's again; a cutscene the ending started plays out first
		if ((int32)(CTimer::GetTimeInMilliseconds() - msLaunchAt) >= 0 && !CCutsceneMgr::IsRunning()) {
			if (Blocked(p)) {
				Note("Mission spawner: not now");
			} else {
				msNoteUntil = 0;
				Launch();
			}
			msState = MS_RELEASE;
		}
		return;

	case MS_RELEASE:
		if (m & (B_CROSS | B_CIRCLE | B_L3 | B_SELECT))
			Consume();
		else
			msState = MS_CLOSED;
		return;
	}
}

// CHud::Draw, after the subtitles
void
PS3_MissionSpawnerDraw(void)
{
	bool note = CTimer::GetTimeInMilliseconds() < msNoteUntil;
	if (msState != MS_OPEN && !note)
		return;

	CFont::SetJustifyOff();
	CFont::SetBackgroundOff();
	CFont::SetCentreOn();
	CFont::SetPropOn();
	CFont::SetCentreSize(SCREEN_WIDTH - SCREEN_SCALE_X(60.0f));
	CFont::SetDropShadowPosition(2);
	CFont::SetDropColor(CRGBA(0, 0, 0, 255));
	CFont::SetFontStyle(FONT_LOCALE(FONT_STANDARD));
	if (msState == MS_OPEN) {
		CFont::SetColor(CRGBA(255, 255, 255, 255));
		CFont::SetScale(SCREEN_SCALE_X(0.75f), SCREEN_SCALE_Y(1.55f));
		CFont::PrintString(SCREEN_WIDTH / 2.0f, SCREEN_SCALE_FROM_BOTTOM(175.0f), msName);
	}
	CFont::SetColor(note ? CRGBA(255, 200, 80, 255) : CRGBA(200, 200, 200, 255));
	CFont::SetScale(SCREEN_SCALE_X(0.5f), SCREEN_SCALE_Y(1.05f));
	CFont::PrintString(SCREEN_WIDTH / 2.0f, SCREEN_SCALE_FROM_BOTTOM(145.0f), note ? msNote : msHint);
	CFont::SetDropShadowPosition(0);
	CFont::SetCentreOff();
}
