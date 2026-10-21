// ps3_autotest.cpp -- reLCS-PS3's automatic tests (USRDIR/autotest.txt).
//
// Put a file named autotest.txt in USRDIR (by FTP), load a save and leave
// the pad alone. Two kinds:
//
//   - the full test (any autotest.txt without "mode=check"): plays the game
//     by itself, missions, odd jobs, collectibles, save + load... with
//     "[autotest]" lines in the log. What it does is written above
//     FullFrame, further down.
//   - "mode=check": the data check below, "[check]" lines.
//
// When it ends autotest.txt is renamed to autotest.done.txt (the next boot
// doesn't run it again).
//
// The check, 10 seconds after the game is playing, goes through what can be
// missing or wrong in the LCS data and the port, a step per frame:
//
//   1. texts: every key the port's code asks for (TheText.Get), the menus'
//      (pages, rows and their option names), the map legend's for LCS's
//      blips, the zone names, the vehicles' names, the stats page's lines;
//      each one looked up in the GXT (main table), the port's own texts and
//      aliases. A missing one is shown in the game as "KEY missing".
//   2. HUD: the icon of every weapon (the weapon model's texture of the
//      same name, or the HUD's for the fist), the radar's sprites, the 64
//      radar tiles of the map.
//   3. models: every vehicle, ped and weapon of the IDEs, loaded one at a
//      time (and let go after, if nothing uses it); the textures they ask
//      for and their TXDs don't have (drawn white) are listed with the
//      model.
//   4. animations: every animation block (.ifp in anim.img) loaded.
//   5. audio: streamed files (radio, cutscenes) not found, empty samples.

#include "common.h"
#include "crossplatform.h"

#if PS3_STAGE != 1

#include <stdio.h>
#include <string.h>

#include "Pad.h"
#include "Timer.h"
#include "Text.h"
#include "Frontend.h"
#include "Hud.h"
#include "Radar.h"
#include "Zones.h"
#include "Stats.h"
#include "World.h"
#include "PlayerPed.h"
#include "ModelInfo.h"
#include "VehicleModelInfo.h"
#include "Streaming.h"
#include "TxdStore.h"
#include "AnimManager.h"
#include "WeaponInfo.h"
#include "CutsceneMgr.h"

#include "ps3_platform.h"
#include "src/ps3/rwps3.h"
#include "ps3_checkkeys.h"

extern int32 gRadarTxdIds[64];
const char *PS3_LegendKey(int32 sprite);	// Radar.cpp
void PS3_AudioCheck(void);			// sampman_ps3.cpp

static const char *const ps3RadarSpriteNames[] = {
	"",	// 0
	"radar_centre",	// 1
	"arrow",	// 2
	"radar_north",	// 3
	"radar_avery",	// 4
	"radar_biker",	// 5
	"radar_cortez",	// 6
	"radar_diaz",	// 7
	"radar_kent",	// 8
	"radar_lawyer",	// 9
	"radar_phil",	// 10
	"bikers",	// 11
	"boatyard",	// 12
	"club",	// 13
	"cubans",	// 14
	"filmstudio",	// 15
	"gun",	// 16
	"haitians",	// 17
	"hardware",	// 18
	"radar_save",	// 19
	"radar_strip",	// 20
	"icecream",	// 21
	"kcabs",	// 22
	"lovefist",	// 23
	"printworks",	// 24
	"property",	// 25
	"SunYard",	// 26
	"spray",	// 27
	"tshirt",	// 28
	"tommy",	// 29
	"phone",	// 30
	"radar_8Ball",	// 31
	"radar_Burner",	// 32
	"radar_Cipriani",	// 33
	"radar_Donald",	// 34
	"radar_Joseph",	// 35
	"radar_Leone",	// 36
	"radar_Maria",	// 37
	"radar_McRay",	// 38
	"radar_Toshiko",	// 39
	"radar_Vincenzo",	// 40
	"radar_Bomb",	// 41
	"radar_cShop",	// 42
	"radar_propertyR",	// 43
	"radar_flag",	// 44
	"radar_powerup",	// 45
	"mp_base",	// 46
	"radar_mp_checkpoint",	// 47
	"mp_player",	// 48
	"mp_objective",	// 49
	"mp_car",	// 50
	"mp_tank",	// 51
	"mp_carlockup",	// 52
	"mp_targetplayer",	// 53
	"radar_arrow",	// 54
	"radar_arrow_01",	// 55
	"radar_arrow_02",	// 56
	"radar_arrow_03",	// 57
	"radar_arrow_04",	// 58
	"radar_arrow_05",	// 59
	"radar_arrow_06",	// 60
	"radar_arrow_07",	// 61
	"radar_player_target",	// 62
};

enum { CK_OFF, CK_WAIT, CK_TEXT, CK_HUD, CK_TILES, CK_MODELS, CK_ANIMS, CK_AUDIO, CK_DONE };

static int ckState = -1;	// -1: autotest.txt not looked at yet
static uint32 ckPlayingSince;
static int ckIndex;
static int ckMissingTexts, ckMissingIcons, ckMissingSprites, ckMissingTiles, ckBadModels, ckModelTexMisses, ckBadAnims;
static int ckModelsLoaded, ckAlreadyLoaded;

// textures a model being loaded asks for that its TXD doesn't have (librw
// leaves them out: drawn white), logged with the model
static const char *ckCurModel;
static int ckCurMisses;

static void
CheckTexMiss(const char *name, const char *mask)
{
	if (ckCurModel == nil)
		return;	// the game streaming something on its own
	PS3_Logf("[check]   %s: texture \"%s\"%s%s%s not in its TXD (drawn white)", ckCurModel, name ? name : "",
	         mask && mask[0] ? " (mask \"" : "", mask && mask[0] ? mask : "", mask && mask[0] ? "\")" : "");
	ckCurMisses++;
}

static const char *
TextSource(int where)
{
	switch (where) {
	case 1: return "GXT";
	case 2: return "mission table";
	case 3: return "port";
	default: return "MISSING";
	}
}

// a key: logged when it has no text; returns 1 if missing
static int
CheckKey(const char *what, const char *key)
{
	if (key == nil || key[0] == '\0')
		return 0;
	int where = TheText.PS3_Lookup(key);
	if (where != 0)
		return 0;
	PS3_Logf("[check] text: %s \"%s\": no text (shows \"%s missing\")", what, key, key);
	ckMissingTexts++;
	return 1;
}

static void
CheckTexts(void)
{
	int n = 0, miss = 0;
	for (int i = 0; i < (int)ARRAY_SIZE(ps3CheckKeys); i++, n++)
		miss += CheckKey("code", ps3CheckKeys[i]);
	PS3_Logf("[check] text: %d keys of the code, %d missing", n, miss);

	n = miss = 0;
	for (int p = 0; p < MENUPAGES; p++) {
		CMenuScreenCustom &scr = aScreens[p];
		if (scr.m_ScreenName[0]) {
			n++;
			miss += CheckKey("menu page", scr.m_ScreenName);
		}
		for (int r = 0; r < NUM_MENUROWS; r++) {
			CMenuScreenCustom::CMenuEntry &e = scr.m_aEntries[r];
			if (e.m_EntryName[0] == '\0' && e.m_Action == 0)
				continue;
			char what[32];
			sprintf(what, "menu page %d row %d", p, r);
			if (e.m_EntryName[0]) {
				n++;
				miss += CheckKey(what, e.m_EntryName);
			}
			if (e.m_Action == MENUACTION_CFO_SELECT && e.m_CFOSelect && e.m_CFOSelect->rightTexts)
				for (int o = 0; o < e.m_CFOSelect->numRightTexts; o++) {
					n++;
					miss += CheckKey(what, e.m_CFOSelect->rightTexts[o]);
				}
		}
	}
	PS3_Logf("[check] text: %d keys of the menus, %d missing", n, miss);

	n = miss = 0;
	for (int s = RADAR_SPRITE_ENTITY_BLIP; s < RADAR_SPRITE_COUNT; s++) {
		const char *k = PS3_LegendKey(s);
		if (k) {
			n++;
			miss += CheckKey("map legend", k);
		}
	}
	miss += CheckKey("map", "MAP_YAH");
	miss += CheckKey("map", "MAP_LEG");
	PS3_Logf("[check] text: %d legend keys of LCS's blips, %d missing", n + 2, miss);

	n = miss = 0;
	for (int i = 0; i < CTheZones::PS3_NumInfoZones(); i++, n++)
		miss += CheckKey("zone (info)", CTheZones::GetInfoZone(i)->name);
	for (int i = 0; i < CTheZones::PS3_NumNavigationZones(); i++, n++)
		miss += CheckKey("zone (navigation)", CTheZones::GetNavigationZone(i)->name);
	PS3_Logf("[check] text: %d zone names, %d missing", n, miss);

	n = miss = 0;
	for (int i = 0; i < MODELINFOSIZE; i++) {
		CBaseModelInfo *mi = CModelInfo::GetModelInfo(i);
		if (mi == nil || mi->GetModelType() != MITYPE_VEHICLE)
			continue;
		char what[48];
		sprintf(what, "vehicle %d (%s) name", i, mi->GetModelName());
		n++;
		miss += CheckKey(what, ((CVehicleModelInfo*)mi)->m_gameName);
	}
	PS3_Logf("[check] text: %d vehicle names, %d missing", n, miss);

	// the stats page builds its lines with TheText.Get: a missing one is
	// logged by CText::Get as "[text] no text for"
	int rows = CStats::ConstructStatLine(99999);
	for (int i = 0; i < rows; i++)
		CStats::ConstructStatLine(i);
	PS3_Logf("[check] text: %d stats lines built (a missing key: a \"[text]\" line above)", rows);
}

static void
CheckHud(void)
{
	// weapon icons: from the weapon model's TXD (texture named like the
	// model), the HUD's sprite for the ones without a model (fist)
	for (int w = 0; w < WEAPONTYPE_TOTALWEAPONS; w++) {
		CWeaponInfo *info = CWeaponInfo::GetWeaponInfo((eWeaponType)w);
		if (info == nil)
			continue;
		const char *name = CWeaponInfo::PS3_WeaponName(w);
		if (info->m_nModelId <= 0) {
			bool ok = w < NUM_HUD_SPRITES && CHud::Sprites[w].m_pTexture != nil;
			PS3_Logf("[check] hud: weapon %d (%s), no model: HUD icon %s", w, name, ok ? "ok" : "MISSING");
			if (!ok) ckMissingIcons++;
			continue;
		}
		CBaseModelInfo *mi = CModelInfo::GetModelInfo(info->m_nModelId);
		if (mi == nil) {
			PS3_Logf("[check] hud: weapon %d (%s): model %d doesn't exist", w, name, info->m_nModelId);
			ckMissingIcons++;
			continue;
		}
		bool wasLoaded = CStreaming::HasModelLoaded(info->m_nModelId);
		if (!wasLoaded) {
			CStreaming::RequestModel(info->m_nModelId, STREAMFLAGS_DEPENDENCY);
			CStreaming::LoadAllRequestedModels(false);
		}
		TxdDef *td = mi->GetTxdSlot() >= 0 ? CTxdStore::GetSlot(mi->GetTxdSlot()) : nil;
		RwTexture *icon = td && td->texDict ? RwTexDictionaryFindNamedTexture(td->texDict, mi->GetModelName()) : nil;
		PS3_Logf("[check] hud: weapon %d (%s), model %d \"%s\": %s%s", w, name, info->m_nModelId, mi->GetModelName(),
		         !CStreaming::HasModelLoaded(info->m_nModelId) ? "model NOT LOADED, " : "",
		         icon ? "icon ok" : "icon MISSING (no texture of that name in its TXD)");
		if (!icon) ckMissingIcons++;
		if (!wasLoaded && mi->GetNumRefs() == 0 && CStreaming::CanRemoveModel(info->m_nModelId))
			CStreaming::RemoveModel(info->m_nModelId);
	}

	// the radar's sprites (VC's ones are still asked for: LCS doesn't use them)
	for (int s = 1; s < RADAR_SPRITE_COUNT && s < (int)ARRAY_SIZE(ps3RadarSpriteNames); s++) {
		if (CRadar::RadarSprites[s] == nil || CRadar::RadarSprites[s]->m_pTexture != nil)
			continue;
		bool lcs = PS3_LegendKey(s) != nil || s == RADAR_SPRITE_CENTRE || s == RADAR_SPRITE_MAP_HERE ||
		           s == RADAR_SPRITE_NORTH || s == RADAR_SPRITE_GUN || s == RADAR_SPRITE_SAVE ||
		           s == RADAR_SPRITE_SPRAY || s == RADAR_SPRITE_PHONE;
		PS3_Logf("[check] hud: radar sprite %d \"%s\" missing%s", s, ps3RadarSpriteNames[s],
		         lcs ? " (LCS uses it)" : " (Vice City's)");
		if (lcs) ckMissingSprites++;
	}
	// the HUD's own sprites
	int hudMissing = 0;
	for (int i = 0; i < NUM_HUD_SPRITES; i++)
		if (CHud::Sprites[i].m_pTexture == nil)
			hudMissing++;
	PS3_Logf("[check] hud: %d of %d HUD sprite slots without a texture (the weapon names are VC's: "
	         "LCS's weapons take their icon from the model, above)", hudMissing, NUM_HUD_SPRITES);
}

// one radar tile per frame
static bool
CheckTile(int i)
{
	int slot = gRadarTxdIds[i];
	if (slot < 0) {
		PS3_Logf("[check] map: radar tile %d has no TXD (radar%02d.txd)", i, i);
		ckMissingTiles++;
		return true;
	}
	bool was = CStreaming::HasTxdLoaded(slot);
	if (!was) {
		CStreaming::RequestTxd(slot, STREAMFLAGS_DEPENDENCY);
		CStreaming::LoadAllRequestedModels(false);
	}
	TxdDef *td = CTxdStore::GetSlot(slot);
	bool ok = CStreaming::HasTxdLoaded(slot) && td && td->texDict;
	if (!ok) {
		PS3_Logf("[check] map: radar tile %d (%s) didn't load", i, CTxdStore::GetTxdName(slot));
		ckMissingTiles++;
	}
	if (!was && CStreaming::CanRemoveTxd(slot))
		CStreaming::RemoveTxd(slot);
	return true;
}

// one model per frame; false when there are no more
static bool
CheckNextModel(void)
{
	for (; ckIndex < MODELINFOSIZE; ckIndex++) {
		CBaseModelInfo *mi = CModelInfo::GetModelInfo(ckIndex);
		if (mi == nil)
			continue;
		int type = mi->GetModelType();
		if (type != MITYPE_VEHICLE && type != MITYPE_PED && type != MITYPE_WEAPON)
			continue;
		if (CStreaming::ms_aInfoForModel[ckIndex].GetCdSize() == 0)
			continue;	// not in the .img (the special peds / cutscene slots)
		int id = ckIndex++;
		bool was = CStreaming::HasModelLoaded(id);
		static char cur[48];
		sprintf(cur, "%s %d \"%s\"", type == MITYPE_VEHICLE ? "vehicle" : type == MITYPE_PED ? "ped" : "weapon", id, mi->GetModelName());
		ckCurModel = cur;
		ckCurMisses = 0;
		if (!was) {
			CStreaming::RequestModel(id, STREAMFLAGS_DEPENDENCY);
			CStreaming::LoadAllRequestedModels(false);
		} else
			ckAlreadyLoaded++;	// checked when it loaded: the [tex] lines of the boot
		ckCurModel = nil;
		ckModelTexMisses += ckCurMisses;
		ckModelsLoaded++;
		if (!CStreaming::HasModelLoaded(id) || mi->GetRwObject() == nil) {
			PS3_Logf("[check] model: %s DIDN'T LOAD", cur);
			ckBadModels++;
		}
		if (!was && mi->GetNumRefs() == 0 && CStreaming::CanRemoveModel(id))
			CStreaming::RemoveModel(id);
		return true;
	}
	return false;
}

static bool
CheckNextAnim(void)
{
	for (; ckIndex < CAnimManager::PS3_NumAnimBlocks(); ckIndex++) {
		int id = ckIndex++;
		CAnimBlock *b = CAnimManager::GetAnimationBlock(id);
		bool was = b->isLoaded;
		if (!was) {
			CStreaming::RequestAnim(id, STREAMFLAGS_DEPENDENCY);
			CStreaming::LoadAllRequestedModels(false);
		}
		if (!b->isLoaded) {
			PS3_Logf("[check] anim: block %d \"%s\" (%d anims) DIDN'T LOAD", id, b->name, b->numAnims);
			ckBadAnims++;
		}
		if (!was && b->isLoaded && b->refCount == 0 && CStreaming::CanRemoveAnim(id))
			CStreaming::RemoveAnim(id);
		return true;
	}
	return false;
}

static void
CheckFrame(void)
{
	if (ckState == -1) {
		ckState = CK_WAIT;
		PS3_Log("[check] autotest.txt (mode=check): the check starts 10 s after the game is playing");
	}
	if (ckState == CK_OFF || ckState == CK_DONE)
		return;
	// not while a menu, a cutscene or a load is up
	if (FrontEndMenuManager.m_bMenuActive || CCutsceneMgr::IsRunning() || FindPlayerPed() == nil) {
		if (ckState == CK_WAIT)
			ckPlayingSince = 0;
		return;
	}
	uint32 now = CTimer::GetTimeInMillisecondsPauseMode();
	switch (ckState) {
	case CK_WAIT:
		if (ckPlayingSince == 0)
			ckPlayingSince = now ? now : 1;
		if (now - ckPlayingSince < 10000)
			return;
		PS3_LogMemTag("[check] start:");
		rw::ps3::texMissHook = CheckTexMiss;
		ckState = CK_TEXT;
		return;
	case CK_TEXT:
		CheckTexts();
		ckState = CK_HUD;
		return;
	case CK_HUD:
		CheckHud();
		ckState = CK_TILES;
		ckIndex = 0;
		return;
	case CK_TILES:
		CheckTile(ckIndex++);
		if (ckIndex >= 64) {
			PS3_Logf("[check] map: 64 radar tiles, %d missing", ckMissingTiles);
			ckState = CK_MODELS;
			ckIndex = 0;
		}
		return;
	case CK_MODELS:
		if (!CheckNextModel()) {
			PS3_Logf("[check] model: %d vehicles, peds and weapons (%d were loaded already), %d didn't load, %d textures missing",
			         ckModelsLoaded, ckAlreadyLoaded, ckBadModels, ckModelTexMisses);
			ckState = CK_ANIMS;
			ckIndex = 0;
		}
		return;
	case CK_ANIMS:
		if (!CheckNextAnim()) {
			PS3_Logf("[check] anim: %d animation blocks, %d didn't load", CAnimManager::PS3_NumAnimBlocks(), ckBadAnims);
			ckState = CK_AUDIO;
		}
		return;
	case CK_AUDIO:
		PS3_AudioCheck();
		rw::ps3::texMissHook = nil;
		PS3_LogMemTag("[check] end:");
		PS3_Logf("[check] done: texts missing %d, weapon icons missing %d, LCS radar sprites missing %d, "
		         "radar tiles missing %d, models that didn't load %d, model textures missing %d, anim blocks that didn't load %d",
		         ckMissingTexts, ckMissingIcons, ckMissingSprites, ckMissingTiles, ckBadModels, ckModelTexMisses, ckBadAnims);
		rename(PS3_USRDIR "/autotest.txt", PS3_USRDIR "/autotest.done.txt");
		ckState = CK_DONE;
		return;
	}
}

// ---- the full test (autotest.txt without "mode=check") ----------------------------
//
// What a player does, one step at a time, with a "[autotest]" line per step
// (result, time, fps, memory). Toni can't die (health kept up, bullet / fire /
// explosion / collision / melee proof) and can't be busted: the police ignore
// him, and the doors of any vehicle he is in are locked so nobody drags him
// out (the vehicle is kept from catching fire too). Everything is put back at
// the end. It changes the game's progress (packages, jumps, missions): use a
// save you don't mind, and don't save after.
//
//   1. save + load (a slot of its own, not one of the menu's)
//   2. the 16 outfits, one by one (the player model swap), and back
//   3. collectibles: the 100 hidden packages (Toni put on each one), the 20
//      rampages (started, then failed as if he had died: they stay to do),
//      the 26 unique jumps (a bike put flying over the take-off, then over
//      the landing: the script's own slow-motion and reward)
//   4. odd jobs started the player's way (vehicle + R3): taxi, paramedic,
//      vigilante, firefighter, trash
//   5. a Pay 'n' Spray (with a wanted level) and one of each other garage
//   6. a car, a bike and a boat at full throttle
//   7. every mission script of main.scm (story, odd jobs, races, RC, dirt
//      track, salesmen...), started as the mission switcher does, with Toni
//      put where the game starts it (the contact's marker, the odd job's
//      vehicle): "secs=N" seconds each (30), cutscenes skipped with X
//   8. save + load again
//
// Lines of autotest.txt (all optional):
//   mode=check     only the data check (texts, HUD, models, anims, audio)
//   secs=N         seconds per mission (5..600)
//   from=N / to=N  only missions N..M (the numbers of the "mission N" lines)
//   nomissions     steps 1-6 only
//   nocollect      no packages / rampages / jumps
//   resume=0       start from the beginning even if a run died before
// If the game dies, the next boot goes on after the step it died in
// (autotest.progress.txt).

#include <stdarg.h>
#include <stdlib.h>
#include <strings.h>
#include <malloc.h>
#include <sys/systime.h>

#include "General.h"
#include "Clock.h"
#include "Weather.h"
#include "PlayerInfo.h"
#include "Wanted.h"
#include "Vehicle.h"
#include "Automobile.h"
#include "Bike.h"
#include "Boat.h"
#include "Pools.h"
#include "ModelIndices.h"
#include "Garages.h"
#include "Script.h"
#include "CarCtrl.h"
#include "Game.h"
#include "GenericGameStorage.h"
#include "PCSave.h"
#include "AudioManager.h"
#include "AnimBlendAssociation.h"
#include "Camera.h"
#include "Pickups.h"
#include "FileMgr.h"
#include "Darkel.h"
#include "WaterLevel.h"
#include "main.h"

namespace rw { namespace ps3 { void vramStats(uint32 *used, uint32 *total, uint32 *blocks); } }

#define AUTOTEST_FILE          PS3_USRDIR "/autotest.txt"
#define AUTOTEST_DONE_FILE     PS3_USRDIR "/autotest.done.txt"
#define AUTOTEST_PROGRESS_FILE PS3_USRDIR "/autotest.progress.txt"
#define AUTOTEST_SLOT          SLOT_COUNT	// a file of its own, not one of the 8 menu slots

// main.scm (LCS, ps3/tools/lcsdis.py): the globals the collectibles live in
#define G_PACKAGE_DONE     11	// +n, 100: 1 when collected
#define G_PACKAGE_PICKUP  562	// +n: the pickup's handle
#define G_RAMPAGE_DONE    163	// +n, 20
#define G_RAMPAGE_PICKUP 2605	// +n
#define G_JUMP_DONE       183	// +n, 26
#define G_JUMP_TAKEOFF    662	// x +n, y 688 +n, z 714 +n
#define G_JUMP_LANDING    818	// x +n, y 844 +n, z 870 +n
#define G_OUTFIT          533	// the outfit the player wears (0..15)
#define NUM_PACKAGES 100
#define NUM_RAMPAGES 20
#define NUM_JUMPS 26
#define NUM_OUTFITS 16

static int32 &G(int n) { return *(int32*)&CTheScripts::ScriptSpace[4 * n]; }
static float GF(int n) { return *(float*)&CTheScripts::ScriptSpace[4 * n]; }

// the outfits by G_OUTFIT (main.scm's MDLMAC)
static const char *const outfitNames[NUM_OUTFITS] = {
	"plr3", "plr", "plr5", "plr7", "plr4", "plr2", "plr6", "plr8",
	"plr9", "plr10", "plr11", "plr12", "plr13", "plr14", "plr16", "plr15",
};

// Where main.scm starts each mission (its LOAD_AND_LAUNCH_MISSION_INTERNAL
// and the checks before it): the contact's marker, the odd job's vehicle.
// LCS vehicle ids: 138 firetruck, 139 trash, 147 ambulance, 151 taxi,
// 157 police, 182 Topfun (RC), 202 Angel, 203 Pizza Boy, 204 Noodle Boy,
// 206 Faggio, 207 Freeway, 210 Sanchez
enum { MP_HERE, MP_FOOT, MP_IN, MP_NEAR };	// where he is / on foot at / in the vehicle at / the vehicle near
struct MissionPlace { int16 idx; const char *name; int16 model; int8 place; float x, y, z; };
static const MissionPlace missionPlaces[] = {
	{ 9, "VIC1", -1, MP_HERE, 0, 0, 0 },
	{ 10, "TAXIDR", 151, MP_HERE, 0, 0, 0 },
	{ 11, "AMBULAN", 147, MP_HERE, 0, 0, 0 },
	{ 12, "COPCAR", 157, MP_HERE, 0, 0, 0 },
	{ 13, "CRASH", 138, MP_NEAR, -83.9f, -431.8f, 15.3f },
	{ 14, "FIRETRU", 138, MP_HERE, 0, 0, 0 },
	{ 15, "TRASH2", 139, MP_HERE, 0, 0, 0 },
	{ 16, "RCBOMB1", 182, MP_IN, 1023.4f, -784.2f, 14.0f },
	{ 17, "RCRACE1", 182, MP_IN, 952.68f, -177.12f, 4.5f },
	{ 18, "RCRACE2", 182, MP_IN, 83.75f, -598.75f, 25.9f },
	{ 19, "RCRACE3", 182, MP_IN, -836.7f, -757.28f, 11.65f },
	{ 20, "PCJPLAY", 206, MP_NEAR, 448.6f, -735.4f, 16.2f },
	{ 21, "NOODLE", 204, MP_NEAR, 1038.1f, -665.9f, -100.0f },
	{ 22, "PIZZA", 203, MP_NEAR, 138.0f, -1264.6f, -100.0f },
	{ 23, "TR1", 210, MP_NEAR, 1328.6f, -531.2f, 42.7f },
	{ 24, "TT1", 210, MP_NEAR, 796.5f, 15.0f, 5.1f },
	{ 25, "TT2", 210, MP_NEAR, 796.5f, 15.0f, 5.1f },
	{ 26, "TT3", 210, MP_NEAR, 796.5f, 15.0f, 5.1f },
	{ 27, "TT4", 210, MP_NEAR, 796.5f, 15.0f, 5.1f },
	{ 28, "TT5", 210, MP_NEAR, 796.5f, 15.0f, 5.1f },
	{ 29, "TT6", 210, MP_NEAR, 796.5f, 15.0f, 5.1f },
	{ 30, "TT7", 210, MP_NEAR, 796.5f, 15.0f, 5.1f },
	{ 31, "TT8", 210, MP_NEAR, 796.5f, 15.0f, 5.1f },
	{ 32, "TT9", 210, MP_NEAR, 796.5f, 15.0f, 5.1f },
	{ 33, "TT10", 210, MP_NEAR, 796.5f, 15.0f, 5.1f },
	{ 34, "CARSALE", -1, MP_FOOT, 1218.55f, -121.74f, 14.0f },
	{ 35, "BIKESAL", -1, MP_FOOT, -89.4f, -477.13f, 15.13f },
	{ 36, "RAC1", -1, MP_FOOT, 1225.0f, -840.0f, 15.0f },
	{ 37, "RAC2", -1, MP_FOOT, -6.56f, -335.54f, 15.13f },
	{ 38, "RAC3", -1, MP_FOOT, -443.4f, -5.6f, 3.9f },
	{ 39, "BIK1", -1, MP_FOOT, 844.0f, -641.7f, 15.0f },
	{ 40, "BIK2", -1, MP_FOOT, 55.4f, -1592.99f, 25.13f },
	{ 41, "BIK3", -1, MP_FOOT, -997.3f, -40.6f, 39.6f },
	{ 42, "TP2", 206, MP_NEAR, 875.7f, -805.6f, 14.9f },
	{ 43, "TS1", 202, MP_NEAR, -666.8f, 54.1f, 17.7f },
	{ 44, "TP1", 207, MP_NEAR, 946.2f, -418.6f, 15.3f },
	{ 45, "KICKST", 210, MP_NEAR, 1162.4f, 0.0f, 1.0f },
	{ 46, "TOURIST", -1, MP_FOOT, -643.53f, -749.81f, 18.0f },
	{ 47, "TV", -1, MP_FOOT, 1584.93f, -1135.0f, 17.89f },
	{ 48, "VIC2", -1, MP_FOOT, 1127.22f, -1108.3f, -100.0f },
	{ 49, "VIC3", -1, MP_FOOT, 1127.22f, -1108.3f, -100.0f },
	{ 50, "VIC4", -1, MP_FOOT, 1127.22f, -1108.3f, -100.0f },
	{ 51, "VIC5", -1, MP_FOOT, 1127.22f, -1108.3f, -100.0f },
	{ 52, "VIC6", -1, MP_FOOT, 1127.22f, -1108.3f, -100.0f },
	{ 53, "VIC7", -1, MP_FOOT, 1127.22f, -1108.3f, -100.0f },
	{ 54, "SAL1", -1, MP_FOOT, 1455.7f, -187.3f, -100.0f },
	{ 55, "SAL2", -1, MP_FOOT, 1455.7f, -187.3f, -100.0f },
	{ 56, "SAL3", -1, MP_FOOT, 1455.7f, -187.3f, -100.0f },
	{ 57, "SAL4", -1, MP_FOOT, 1455.7f, -187.3f, -100.0f },
	{ 58, "SAL5", -1, MP_FOOT, 1455.7f, -187.3f, -100.0f },
	{ 59, "SAL6", -1, MP_FOOT, 1455.7f, -187.3f, -100.0f },
	{ 60, "SAL7", -1, MP_FOOT, 1455.7f, -187.3f, -100.0f },
	{ 61, "SAL8", -1, MP_FOOT, 1455.7f, -187.3f, -100.0f },
	{ 62, "JDREF", -1, MP_FOOT, 895.13f, -425.61f, 13.89f },
	{ 63, "JDT1", -1, MP_FOOT, 895.13f, -425.61f, 13.89f },
	{ 64, "JDT2", -1, MP_FOOT, 895.13f, -425.61f, 13.89f },
	{ 65, "JDT3", -1, MP_FOOT, 895.13f, -425.61f, 13.89f },
	{ 66, "JDT4", -1, MP_FOOT, 895.13f, -425.61f, 13.89f },
	{ 67, "JDT5", -1, MP_FOOT, 895.13f, -425.61f, 13.89f },
	{ 68, "JDT6", -1, MP_FOOT, 895.13f, -425.61f, 13.89f },
	{ 69, "JDT7", -1, MP_FOOT, 895.13f, -425.61f, 13.89f },
	{ 70, "JDT8", -1, MP_FOOT, 895.13f, -425.61f, 13.89f },
	{ 71, "MAC1", -1, MP_FOOT, 1219.8f, -319.7f, 27.4f },
	{ 72, "MAC2", -1, MP_FOOT, 1219.8f, -319.7f, 27.4f },
	{ 73, "MAC3", -1, MP_FOOT, 1219.8f, -319.7f, 27.4f },
	{ 74, "MAC4", -1, MP_FOOT, 1219.8f, -319.7f, 27.4f },
	{ 75, "MAC5", -1, MP_FOOT, 1219.8f, -319.7f, 27.4f },
	{ 76, "MAR1", -1, MP_FOOT, 1116.38f, -481.21f, -100.0f },
	{ 77, "MAR2", -1, MP_FOOT, 1116.38f, -481.21f, -100.0f },
	{ 78, "MAR3", -1, MP_FOOT, 1116.38f, -481.21f, -100.0f },
	{ 79, "MAR4", -1, MP_FOOT, 1116.38f, -481.21f, -100.0f },
	{ 80, "MAR5", -1, MP_FOOT, 1116.38f, -481.21f, -100.0f },
	{ 81, "SALS2", -1, MP_FOOT, 59.33f, -962.55f, -100.0f },
	{ 82, "SALS1", -1, MP_FOOT, 59.33f, -962.55f, -100.0f },
	{ 83, "SALS3", -1, MP_FOOT, 59.33f, -962.55f, -100.0f },
	{ 84, "SALS4", -1, MP_FOOT, 59.33f, -962.55f, -100.0f },
	{ 85, "SALS5", -1, MP_FOOT, 59.33f, -962.55f, -100.0f },
	{ 86, "SALS6", -1, MP_FOOT, 59.33f, -962.55f, -100.0f },
	{ 87, "RAYS1", -1, MP_FOOT, -174.85f, -314.94f, -100.0f },
	{ 88, "RAYS2", -1, MP_FOOT, -174.85f, -314.94f, -100.0f },
	{ 89, "RAYS3", -1, MP_FOOT, -174.85f, -314.94f, -100.0f },
	{ 90, "RAYS4", -1, MP_FOOT, -174.85f, -314.94f, -100.0f },
	{ 91, "RAYS5", -1, MP_FOOT, -174.85f, -314.94f, -100.0f },
	{ 92, "DONS1", -1, MP_FOOT, 220.19f, -1497.44f, -100.0f },
	{ 93, "DONS2", -1, MP_FOOT, 220.19f, -1497.44f, -100.0f },
	{ 94, "DONS3", -1, MP_FOOT, 220.19f, -1497.44f, -100.0f },
	{ 95, "DONS4", -1, MP_FOOT, 220.19f, -1497.44f, -100.0f },
	{ 96, "DONS5", -1, MP_FOOT, 220.19f, -1497.44f, -100.0f },
	{ 97, "DONS6", -1, MP_FOOT, 220.19f, -1497.44f, -100.0f },
	{ 98, "DONS7", -1, MP_FOOT, 220.19f, -1497.44f, -100.0f },
	{ 99, "NEDS1", -1, MP_FOOT, 18.07f, -1125.29f, 26.45f },
	{ 100, "NEDS2", -1, MP_FOOT, 18.07f, -1125.29f, 26.45f },
	{ 101, "NEDS3", -1, MP_FOOT, 18.07f, -1125.29f, 26.45f },
	{ 102, "NEDS4", -1, MP_FOOT, 18.07f, -1125.29f, 26.45f },
	{ 103, "SALH1", -1, MP_FOOT, -1256.58f, -44.95f, -100.0f },
	{ 104, "SALH2", -1, MP_FOOT, -1256.58f, -44.95f, -100.0f },
	{ 105, "SALH3", -1, MP_FOOT, -1256.58f, -44.95f, -100.0f },
	{ 106, "SALH4", -1, MP_FOOT, -1256.58f, -44.95f, -100.0f },
	{ 107, "SALH5", -1, MP_FOOT, -1256.58f, -44.95f, -100.0f },
	{ 108, "DONH1", -1, MP_FOOT, -1039.83f, -175.56f, -100.0f },
	{ 109, "DONH2", -1, MP_FOOT, -1039.83f, -175.56f, -100.0f },
	{ 110, "DONH3", -1, MP_FOOT, -1039.83f, -175.56f, -100.0f },
	{ 111, "DONH4", -1, MP_FOOT, -1039.83f, -175.56f, -100.0f },
	{ 112, "DONH5", -1, MP_FOOT, -1039.83f, -175.56f, -100.0f },
	{ 113, "DONH6", -1, MP_FOOT, -1039.83f, -175.56f, -100.0f },
	{ 114, "TOSH1", -1, MP_FOOT, 323.11f, -1360.75f, -100.0f },
	{ 115, "TOSH2", -1, MP_FOOT, 323.11f, -1360.75f, -100.0f },
	{ 116, "TOSH3", -1, MP_FOOT, 323.11f, -1360.75f, -100.0f },
	{ 117, "TOSH4", -1, MP_FOOT, 323.11f, -1360.75f, -100.0f },
};

enum StepType {
	ST_SAVELOAD,	// save here, load it back, check the position
	ST_OUTFIT,	// a = outfit (NUM_OUTFITS: back to the one he wore)
	ST_PACKAGE,	// a = package
	ST_RAMPAGE,	// a = rampage
	ST_JUMP,	// a = unique jump
	ST_ODDJOB,	// a = vehicle model: get in, R3, wait, R3
	ST_GARAGE,	// a = garage index
	ST_DRIVE,	// a = vehicle model, b = 1: on water; full throttle x seconds
	ST_MISSION,	// a = mission script index
	ST_MISSIONEND,	// the running mission fails and cleans up
	ST_SUMMARY,	// the collectibles' totals
};

struct Step {
	int type;
	int a, b;
	float x;
	char name[48];
};

#define MAX_STEPS 400
static Step steps[MAX_STEPS];
static int numSteps;

static int state;		// 0 not looked at, 1 waiting to start, 2 running, 3 done, 4 check mode
static int fromMission = 0, toMission = -1;
static float missionSecs = 30.0f;
static int resumeOff, noMissions, noCollect;
static int resumeFrom;		// the first step to run (after the one a previous run died in)
static int pulseCross, pressR3, pressCross;
static int cur, phase, stepNoted;
static uint64 phaseStartUs, stepStartUs, runStartUs, lastFrameUs;
static CVector startPos;
static float startHeading;
static int startOutfit;
static int wantLoad, waitingForLoad, loadFailed;
static CVector savedPos;
static int failures, skipped;
static CVehicle *testCar;
static int32 testCarHandle;
static CVector driveFrom;
static float driveMaxSpeed;
static int counterBefore;
static int texMissesBefore;
static CVector jumpTakeoff, jumpLanding, jumpDir;
static bool haveWater;
static CVector waterSpot;

// the vehicle Toni is in: its doors locked (no busts), as it was after
static CVehicle *lockedCar;
static int32 lockedHandle;
static eCarLock lockedPrev;

static uint32 sFrames, sSlow, sVerySlow;
static uint64 sWorstUs, sSumUs;

static uint64 nowUs(void) { return sysGetSystemTime(); }
static float phaseSecs(void) { return (nowUs() - phaseStartUs) / 1000000.0f; }
static void nextPhase(void) { phase++; phaseStartUs = nowUs(); }
static bool waited(float secs) { return phaseSecs() >= secs; }

static void addStep(int type, int a, int b, float x, const char *fmt, ...) __attribute__((format(printf, 5, 6)));
static void addStep(int type, int a, int b, float x, const char *fmt, ...)
{
	if (numSteps >= MAX_STEPS)
		return;
	Step &s = steps[numSteps++];
	s.type = type; s.a = a; s.b = b; s.x = x;
	va_list va;
	va_start(va, fmt);
	vsnprintf(s.name, sizeof(s.name), fmt, va);
	va_end(va);
}

static bool vehicleAlive(CVehicle *v, int32 handle)
{
	return v && CPools::GetVehiclePool()->GetAt(handle) == v;
}

// ---- Toni ---------------------------------------------------------------------

static void unlockCar(void)
{
	if (vehicleAlive(lockedCar, lockedHandle))
		lockedCar->m_nDoorLock = lockedPrev;
	lockedCar = nil;
}

static void keepPlayerSafe(void)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil)
		return;
	if (p->m_fHealth < 100.0f)
		p->m_fHealth = 100.0f;
	p->bBulletProof = p->bFireProof = p->bExplosionProof = p->bCollisionProof = p->bMeleeProof = true;
	if (p->m_pWanted)
		p->m_pWanted->m_bIgnoredByCops = true;	// nobody comes to bust him
	int32 &money = CWorld::Players[CWorld::PlayerInFocus].m_nMoney;
	money = Max(money, 20000);
	// in a vehicle: its doors locked (they can't drag him out), not on fire
	CVehicle *v = p->bInVehicle ? p->m_pMyVehicle : nil;
	if (v != lockedCar) {
		unlockCar();
		if (v) {
			lockedCar = v;
			lockedHandle = CPools::GetVehiclePool()->GetIndex(v);
			lockedPrev = v->m_nDoorLock;
		}
	}
	if (v) {
		v->m_nDoorLock = CARLOCK_LOCKED_PLAYER_INSIDE;
		if (v->m_fHealth < 500.0f)
			v->m_fHealth = 500.0f;
	}
}

static void releasePlayer(void)
{
	unlockCar();
	CPlayerPed *p = FindPlayerPed();
	if (p == nil)
		return;
	p->bBulletProof = p->bFireProof = p->bExplosionProof = p->bCollisionProof = p->bMeleeProof = false;
	if (p->m_pWanted)
		p->m_pWanted->m_bIgnoredByCops = false;
	p->bUsesCollision = true;
	p->bAffectedByGravity = true;
}

static int wantedLevel(void)
{
	CPlayerPed *p = FindPlayerPed();
	return p && p->m_pWanted ? p->m_pWanted->GetWantedLevel() : -1;
}

static void clearWanted(void)
{
	CPlayerPed *p = FindPlayerPed();
	if (p && p->m_pWanted && p->m_pWanted->GetWantedLevel() > 0)
		p->SetWantedLevel(0);
}

static void warpOutOfCar(CVector pos)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil)
		return;
	if (p->bInVehicle && p->m_pMyVehicle) {
		CVehicle *v = p->m_pMyVehicle;
		if (v == lockedCar)
			unlockCar();
		if (v->pDriver == p) {
			v->RemoveDriver();
			v->SetStatus(STATUS_ABANDONED);
			v->bEngineOn = false;
			v->AutoPilot.m_nCruiseSpeed = 0;
		} else
			v->RemovePassenger(p);
		p->bInVehicle = false;
		p->m_pMyVehicle = nil;
		p->SetPedState(PED_IDLE);
		p->bUsesCollision = true;
		p->SetMoveSpeed(0.0f, 0.0f, 0.0f);
		p->AddWeaponModel(CWeaponInfo::GetWeaponInfo(p->GetWeapon()->m_eWeaponType)->m_nModelId);
		p->RemoveInCarAnims();
		if (p->m_pVehicleAnim)
			p->m_pVehicleAnim->blendDelta = -1000.0f;
		p->m_pVehicleAnim = nil;
		p->SetMoveState(PEDMOVE_NONE);
		CAnimManager::BlendAnimation(p->GetClump(), p->m_animGroup, ANIM_STD_IDLE, 100.0f);
		p->RestartNonPartialAnims();
		AudioManager.PlayerJustLeftCar();
	}
	pos.z += p->GetDistanceFromCentreOfMassToBaseOfModel();
	p->Teleport(pos);
}

static void deleteTestCar(void)
{
	if (testCar == nil)
		return;
	if (vehicleAlive(testCar, testCarHandle)) {
		CPlayerPed *p = FindPlayerPed();
		if (p && p->bInVehicle && p->m_pMyVehicle == testCar)
			warpOutOfCar(testCar->GetPosition() + CVector(3.0f, 0.0f, 1.0f));
		if (lockedCar == testCar)
			lockedCar = nil;
		CWorld::Remove(testCar);
		CWorld::RemoveReferencesToDeletedObject(testCar);
		delete testCar;
	}
	testCar = nil;
}

// the ground under (x, y) once the place is loaded; false: water or nothing
static bool groundAt(float x, float y, float *z, float fromZ = 1000.0f)
{
	CStreaming::LoadScene(CVector(x, y, 20.0f));
	bool found = false;
	float gz = CWorld::FindGroundZFor3DCoord(x, y, fromZ, &found);
	if (!found || gz < -50.0f || gz > 500.0f)
		return false;
	*z = gz;
	return true;
}

static CVehicle *makeVehicle(int model, CVector pos, float heading)
{
	if (model <= 0 || CModelInfo::GetModelInfo(model) == nil)
		return nil;
	CStreaming::RequestModel(model, STREAMFLAGS_DEPENDENCY);
	CStreaming::LoadAllRequestedModels(false);
	if (!CStreaming::HasModelLoaded(model))
		return nil;
	CVehicle *car;
	if (CModelInfo::IsBoatModel(model))
		car = new CBoat(model, MISSION_VEHICLE);
	else if (CModelInfo::IsBikeModel(model))
		car = new CBike(model, MISSION_VEHICLE);
	else
		car = new CAutomobile(model, MISSION_VEHICLE);
	if (car == nil)
		return nil;	// the pool is full (POOL_NEW_NOEXCEPT)
	pos.z += car->GetDistanceFromCentreOfMassToBaseOfModel();
	car->SetPosition(pos);
	car->SetHeading(heading);
	CTheScripts::ClearSpaceForMissionEntity(pos, car);
	car->SetStatus(STATUS_ABANDONED);
	car->bIsLocked = false;
	car->m_nDoorLock = CARLOCK_UNLOCKED;
	if (!car->IsBoat())
		CCarCtrl::JoinCarWithRoadSystem(car);
	car->AutoPilot.m_nCarMission = MISSION_NONE;
	car->AutoPilot.m_nTempAction = TEMPACT_NONE;
	car->bEngineOn = false;
	car->m_nZoneLevel = CTheZones::GetLevelFromPosition(&pos);
	car->bHasBeenOwnedByPlayer = true;
	CWorld::Add(car);
	testCarHandle = CPools::GetVehiclePool()->GetIndex(car);
	return car;
}

static void warpIntoCar(CVehicle *car)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil || car == nil)
		return;
	p->SetObjective(OBJECTIVE_ENTER_CAR_AS_DRIVER, car);
	p->WarpPedIntoCar(car);
}

// ---- logging ------------------------------------------------------------------

static void statsReset(void)
{
	sFrames = sSlow = sVerySlow = 0;
	sWorstUs = sSumUs = 0;
}

static void statsFrame(void)
{
	uint64 n = nowUs();
	if (lastFrameUs) {
		uint64 dt = n - lastFrameUs;
		sFrames++;
		sSumUs += dt;
		if (dt > sWorstUs) sWorstUs = dt;
		if (dt > 20000) sSlow++;
		if (dt > 50000) sVerySlow++;
	}
	lastFrameUs = n;
}

static void logStep(const char *result)
{
	uint32 used = 0, total = 0, blocks = 0;
	rw::ps3::vramStats(&used, &total, &blocks);
	CVector pos = FindPlayerCoors();
	float fps = sSumUs ? sFrames * 1000000.0f / sSumUs : 0.0f;
	struct mallinfo mi = mallinfo();
	PS3_Logf("[autotest] %d/%d %-34s %s | %.1f s, %.1f fps, %u frames >20 ms, %u >50 ms, worst %.0f ms | "
	         "at (%.0f, %.0f, %.0f) | VRAM %u MB, streaming %.1f MB, heap %.2f MB, peds %d, cars %d, objects %d",
	         cur + 1, numSteps, steps[cur].name, result,
	         (nowUs() - stepStartUs) / 1000000.0f, fps, sSlow, sVerySlow, sWorstUs / 1000.0f,
	         pos.x, pos.y, pos.z,
	         used >> 20, CStreaming::ms_memoryUsed / 1048576.0f, mi.uordblks / 1048576.0,
	         CPools::GetPedPool()->GetNoOfUsedSpaces(), CPools::GetVehiclePool()->GetNoOfUsedSpaces(),
	         CPools::GetObjectPool()->GetNoOfUsedSpaces());
}

static void logScripts(const char *tag)
{
	char names[200] = "";
	int n = 0;
	for (CRunningScript *s = CTheScripts::pActiveScripts; s; s = s->GetNext()) {
		if (n++ < 16) {
			char nm[9];
			memcpy(nm, s->m_abScriptName, 8);
			nm[8] = '\0';
			strncat(names, " ", sizeof(names) - strlen(names) - 1);
			strncat(names, nm, sizeof(names) - strlen(names) - 1);
		}
	}
	PS3_Logf("[autotest]     %s: on a mission %d, wanted %d, money %d, scripts %d:%s", tag,
	         CTheScripts::IsPlayerOnAMission(), wantedLevel(),
	         CWorld::Players[CWorld::PlayerInFocus].m_nMoney, n, names);
}

// SCRIPT_NAME (0x03A9 + 8 chars) near the start of mission i, from main.scm
static void missionScriptName(int i, char *out)
{
	out[0] = '\0';
	if (gScriptsFile == -1 || i < 0 || i >= CTheScripts::NumberOfMissionScripts)
		return;
	uint8 head[300];
	CFileMgr::Seek(gScriptsFile, CTheScripts::MultiScriptArray[i] + 8, 0);
	int n = CFileMgr::Read(gScriptsFile, (char*)head, sizeof(head));
	for (int k = 0; k + 10 <= n; k++)
		if (head[k] == 0xA9 && head[k + 1] == 0x03 && head[k + 2] >= 'A' && head[k + 2] <= 'Z') {
			memcpy(out, head + k + 2, 8);
			out[8] = '\0';
			return;
		}
}

// ---- the plan -----------------------------------------------------------------

static const MissionPlace *findPlace(int idx)
{
	for (int i = 0; i < (int)ARRAY_SIZE(missionPlaces); i++)
		if (missionPlaces[i].idx == idx)
			return &missionPlaces[i];
	return nil;
}

static void addGarageSteps(void)
{
	bool doneType[64] = { false };
	for (uint32 i = 0; i < CGarages::NumGarages; i++) {
		CGarage &g = CGarages::aGarages[i];
		const char *what = nil;
		switch (g.m_eGarageType) {
		case GARAGE_RESPRAY: what = "Pay 'n' Spray"; break;
		case GARAGE_BOMBSHOP1: case GARAGE_BOMBSHOP2: case GARAGE_BOMBSHOP3: what = "bomb shop"; break;
		case GARAGE_COLLECTCARS_1: case GARAGE_COLLECTCARS_2: case GARAGE_COLLECTCARS_3: what = "car list garage"; break;
		case GARAGE_COLLECTSPECIFICCARS: what = "specific cars garage"; break;
		case GARAGE_CRUSHER: what = "crusher"; break;
		}
		if (what == nil || doneType[g.m_eGarageType & 63])
			continue;
		doneType[g.m_eGarageType & 63] = true;
		addStep(ST_GARAGE, i, 0, 0, "garage %u: %s", i, what);
	}
}

static void buildPlan(void)
{
	numSteps = 0;
	addStep(ST_SAVELOAD, 0, 0, 0, "save + load");
	for (int i = 0; i < NUM_OUTFITS; i++)
		addStep(ST_OUTFIT, i, 0, 0, "outfit %d \"%s\"", i, outfitNames[i]);
	addStep(ST_OUTFIT, NUM_OUTFITS, 0, 0, "outfit: back to the one he wore");
	if (!noCollect) {
		for (int i = 0; i < NUM_PACKAGES; i++)
			addStep(ST_PACKAGE, i, 0, 0, "hidden package %d", i + 1);
		for (int i = 0; i < NUM_RAMPAGES; i++)
			addStep(ST_RAMPAGE, i, 0, 0, "rampage %d", i + 1);
		for (int i = 0; i < NUM_JUMPS; i++)
			addStep(ST_JUMP, i, 0, 0, "unique jump %d", i + 1);
		addStep(ST_SUMMARY, 0, 0, 0, "collectibles: totals");
	}
	addStep(ST_ODDJOB, MI_TAXI, 0, 20.0f, "odd job (R3): taxi");
	addStep(ST_ODDJOB, MI_AMBULAN, 0, 20.0f, "odd job (R3): paramedic");
	addStep(ST_ODDJOB, MI_POLICE, 0, 20.0f, "odd job (R3): vigilante");
	addStep(ST_ODDJOB, MI_FIRETRUCK, 0, 20.0f, "odd job (R3): firefighter");
	addStep(ST_ODDJOB, MI_TRASH, 0, 20.0f, "odd job (R3): trash");
	addGarageSteps();
	addStep(ST_DRIVE, MI_BANSHEE, 0, 8.0f, "car, full throttle");
	addStep(ST_DRIVE, MI_PCJ600, 0, 8.0f, "bike, full throttle");
	addStep(ST_DRIVE, MI_SPEEDER, 1, 10.0f, "boat, full throttle");
	if (!noMissions) {
		int n = CTheScripts::NumberOfMissionScripts;
		int hi = toMission >= 0 && toMission < n - 1 ? toMission : n - 1;
		for (int i = fromMission; i <= hi && numSteps < MAX_STEPS - 4; i++)
			addStep(ST_MISSION, i, 0, missionSecs, "mission %d of %d", i, n - 1);
		addStep(ST_MISSIONEND, 0, 0, 0, "end the last mission");
	}
	addStep(ST_SAVELOAD, 0, 0, 0, "save + load at the end");
	PS3_Logf("[autotest] plan: %d steps (missions %s, %.0f s each; collectibles %s)", numSteps,
	         noMissions ? "off" : "on", missionSecs, noCollect ? "off" : "on");
}

// ---- the steps ------------------------------------------------------------------

static void startStep(void)
{
	phase = 0;
	stepNoted = 0;
	phaseStartUs = stepStartUs = nowUs();
	statsReset();
	PS3_CRUMB("autotest step", cur + 1);
	// if the game dies in this step, the next boot goes on after it
	FILE *pf = fopen(AUTOTEST_PROGRESS_FILE, "wb");
	if (pf) {
		fprintf(pf, "%d %s\n", cur, steps[cur].name);
		fclose(pf);
	}
}

static void finishStep(const char *result)
{
	logStep(result);
	if (strncmp(result, "FAIL", 4) == 0)
		failures++;
	else if (strncmp(result, "skipped", 7) == 0)
		skipped++;
	cur++;
	if (cur < numSteps)
		startStep();
}

static void runSaveLoad(Step &s)
{
	(void)s;
	CPlayerPed *p = FindPlayerPed();
	switch (phase) {
	case 0:
		if (p == nil) { finishStep("FAIL no player"); return; }
		deleteTestCar();
		if (p->bInVehicle)
			warpOutOfCar(p->GetPosition());
		savedPos = p->GetPosition();
		nextPhase();
		break;
	case 1:
		if (!waited(1.0f))
			break;
		PcSaveHelper.SaveSlot(AUTOTEST_SLOT);
		if (PcSaveHelper.nErrorCode != SAVESTATUS_SUCCESSFUL) {
			PS3_Logf("[autotest]     save failed, error %d", PcSaveHelper.nErrorCode);
			finishStep("FAIL save");
			return;
		}
		PS3_Logf("[autotest]     saved at (%.1f, %.1f, %.1f)", savedPos.x, savedPos.y, savedPos.z);
		nextPhase();
		break;
	case 2:
		if (!waited(1.0f))
			break;
		wantLoad = 1;	// ps3.cpp loads it between two frames
		waitingForLoad = 1;
		loadFailed = 0;
		nextPhase();
		break;
	case 3:
		if (waitingForLoad)
			break;
		if (loadFailed) { finishStep("FAIL the save doesn't check out (slot shown as corrupt)"); return; }
		if (!waited(4.0f))
			break;
		p = FindPlayerPed();
		if (p == nil) { finishStep("FAIL no player after the load"); return; }
		{
			float d = (p->GetPosition() - savedPos).Magnitude();
			PS3_Logf("[autotest]     loaded: Toni at (%.1f, %.1f, %.1f), %.1f m from where it was saved, model %d, health %.0f",
			         p->GetPosition().x, p->GetPosition().y, p->GetPosition().z, d, p->GetModelIndex(), p->m_fHealth);
			finishStep(d < 10.0f ? "ok" : "FAIL position differs");
		}
		break;
	}
}

static void texMissCount(const char *name, const char *mask)
{
	(void)mask;
	PS3_Logf("[autotest]     texture \"%s\" missing (drawn white)", name ? name : "");
}

static void runOutfit(Step &s)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil) { finishStep("FAIL no player"); return; }
	switch (phase) {
	case 0: {
		if (p->bInVehicle)
			warpOutOfCar(p->GetPosition());
		int o = s.a < NUM_OUTFITS ? s.a : startOutfit;
		if (o < 0 || o >= NUM_OUTFITS)
			o = 0;
		texMissesBefore = rw::ps3::texMissTotal;
		rw::ps3::texMissHook = texMissCount;
		p->Undress(outfitNames[o]);
		CStreaming::LoadAllRequestedModels(false);
		p->Dress();
		rw::ps3::texMissHook = nil;
		G(G_OUTFIT) = o;
		nextPhase();
		break;
	}
	case 1:
		if (waited(2.0f)) {
			int misses = rw::ps3::texMissTotal - texMissesBefore;
			bool ok = p->GetClump() != nil && misses == 0;
			PS3_Logf("[autotest]     model %d, clump %s, %d textures missing", p->GetModelIndex(),
			         p->GetClump() ? "ok" : "NONE", misses);
			finishStep(ok ? "ok" : "FAIL (see above)");
		}
		break;
	}
}

static void runPackage(Step &s)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil) { finishStep("FAIL no player"); return; }
	int n = s.a;
	switch (phase) {
	case 0: {
		deleteTestCar();
		if (G(G_PACKAGE_DONE + n) != 0) { finishStep("ok (collected already)"); return; }
		int idx = G(G_PACKAGE_PICKUP + n) ? CPickups::GetActualPickupIndex(G(G_PACKAGE_PICKUP + n)) : -1;
		if (idx < 0 || CPickups::aPickUps[idx].m_eType == PICKUP_NONE || CPickups::aPickUps[idx].m_bRemoved) {
			finishStep("FAIL no pickup for it");
			return;
		}
		CVector pos = CPickups::aPickUps[idx].m_vecPos;
		counterBefore = CWorld::Players[CWorld::PlayerInFocus].m_nCollectedPackages;
		CStreaming::LoadScene(pos);
		warpOutOfCar(pos);
		PS3_Logf("[autotest]     at (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
		nextPhase();
		break;
	}
	case 1:
		if (G(G_PACKAGE_DONE + n) != 0) {
			PS3_Logf("[autotest]     collected: packages %d -> %d", counterBefore,
			         CWorld::Players[CWorld::PlayerInFocus].m_nCollectedPackages);
			nextPhase();
		} else if (waited(5.0f))
			finishStep("FAIL not collected in 5 s");
		break;
	case 2:
		if (waited(1.0f))	// the message and the reward
			finishStep("ok");
		break;
	}
}

static void runRampage(Step &s)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil) { finishStep("FAIL no player"); return; }
	int n = s.a;
	switch (phase) {
	case 0: {
		deleteTestCar();
		if (G(G_RAMPAGE_DONE + n) != 0) { finishStep("ok (passed already)"); return; }
		int idx = G(G_RAMPAGE_PICKUP + n) ? CPickups::GetActualPickupIndex(G(G_RAMPAGE_PICKUP + n)) : -1;
		if (idx < 0 || CPickups::aPickUps[idx].m_eType == PICKUP_NONE || CPickups::aPickUps[idx].m_bRemoved) {
			finishStep("skipped: no pickup for it now");
			return;
		}
		CVector pos = CPickups::aPickUps[idx].m_vecPos;
		CStreaming::LoadScene(pos);
		warpOutOfCar(pos);
		PS3_Logf("[autotest]     at (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
		nextPhase();
		break;
	}
	case 1:
		if (CDarkel::FrenzyOnGoing()) {
			PS3_Logf("[autotest]     rampage on (status %d)", CDarkel::ReadStatus());
			nextPhase();
		} else if (waited(8.0f)) {
			logScripts("no rampage");
			finishStep("FAIL it didn't start in 8 s");
		}
		break;
	case 2:
		if (waited(5.0f)) {
			CDarkel::ResetOnPlayerDeath();	// failed, as if he had died: it stays to do
			nextPhase();
		}
		break;
	case 3:
		if (waited(4.0f)) {
			int idx = CPickups::GetActualPickupIndex(G(G_RAMPAGE_PICKUP + n));
			bool back = idx >= 0 && CPickups::aPickUps[idx].m_eType != PICKUP_NONE && !CPickups::aPickUps[idx].m_bRemoved;
			PS3_Logf("[autotest]     after it failed: frenzy status %d, its pickup %s", CDarkel::ReadStatus(), back ? "back" : "NOT back yet");
			finishStep("ok");
		}
		break;
	}
}

static void runJump(Step &s)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil) { finishStep("FAIL no player"); return; }
	int n = s.a;
	switch (phase) {
	case 0: {
		deleteTestCar();
		if (G(G_JUMP_DONE + n) != 0) { finishStep("ok (done already)"); return; }
		jumpTakeoff = CVector(GF(G_JUMP_TAKEOFF + n), GF(G_JUMP_TAKEOFF + 26 + n), GF(G_JUMP_TAKEOFF + 52 + n));
		jumpLanding = CVector(GF(G_JUMP_LANDING + n), GF(G_JUMP_LANDING + 26 + n), GF(G_JUMP_LANDING + 52 + n));
		if (jumpTakeoff.x == 0.0f && jumpTakeoff.y == 0.0f) { finishStep("skipped: not set"); return; }
		jumpDir = jumpLanding - jumpTakeoff;
		jumpDir.z = 0.0f;
		jumpDir.Normalise();
		// a boat jump: the take-off is on the water
		float wl;
		bool water = CWaterLevel::GetWaterLevel(jumpTakeoff.x, jumpTakeoff.y, jumpTakeoff.z, &wl, true) &&
		             jumpTakeoff.z - wl < 3.0f;
		CStreaming::LoadScene(jumpTakeoff);
		warpOutOfCar(jumpTakeoff + CVector(0.0f, 0.0f, 3.0f));
		float heading = CGeneral::GetATanOfXY(jumpDir.x, jumpDir.y) - HALFPI;
		testCar = makeVehicle(water ? MI_SPEEDER : MI_PCJ600, jumpTakeoff + CVector(0.0f, 0.0f, 2.5f), heading);
		if (testCar == nil) { finishStep("FAIL no vehicle"); return; }
		warpIntoCar(testCar);
		PS3_Logf("[autotest]     take-off (%.0f, %.0f, %.0f), landing (%.0f, %.0f, %.0f)%s",
		         jumpTakeoff.x, jumpTakeoff.y, jumpTakeoff.z, jumpLanding.x, jumpLanding.y, jumpLanding.z, water ? ", by boat" : "");
		nextPhase();
		break;
	}
	case 1:
		// over the take-off, in the air, at speed, until the script sees it
		// (its slow motion)
		if (!vehicleAlive(testCar, testCarHandle)) { testCar = nil; finishStep("FAIL the vehicle is gone"); return; }
		if (CTimer::GetTimeScale() < 0.9f) {
			nextPhase();
			break;
		}
		if (waited(5.0f)) {
			logScripts("jump not seen");
			deleteTestCar();
			finishStep("FAIL the script didn't see the take-off");
			break;
		}
		if (phaseSecs() > 0.5f) {	// the vehicle settled in first
			testCar->Teleport(jumpTakeoff + CVector(0.0f, 0.0f, 2.5f));
			testCar->SetHeading(CGeneral::GetATanOfXY(jumpDir.x, jumpDir.y) - HALFPI);
			testCar->SetMoveSpeed(jumpDir * 0.6f);	// 30 m/s
			testCar->SetTurnSpeed(0.0f, 0.0f, 0.0f);
		}
		break;
	case 2:
		// over the landing, still in the air: then it comes down
		if (!vehicleAlive(testCar, testCarHandle)) { testCar = nil; finishStep("FAIL the vehicle is gone"); return; }
		testCar->Teleport(jumpLanding + CVector(0.0f, 0.0f, 2.0f));
		testCar->SetHeading(CGeneral::GetATanOfXY(jumpDir.x, jumpDir.y) - HALFPI);
		testCar->SetMoveSpeed(jumpDir * 0.1f);
		testCar->SetTurnSpeed(0.0f, 0.0f, 0.0f);
		nextPhase();
		break;
	case 3:
		if (G(G_JUMP_DONE + n) != 0 || waited(10.0f)) {
			bool done = G(G_JUMP_DONE + n) != 0;
			PS3_Logf("[autotest]     jump %s, time scale %.2f, money %d", done ? "DONE" : "not done",
			         CTimer::GetTimeScale(), CWorld::Players[CWorld::PlayerInFocus].m_nMoney);
			nextPhase();
		}
		break;
	case 4:
		if (waited(2.5f) && CTimer::GetTimeScale() > 0.9f) {	// the camera back
			deleteTestCar();
			finishStep(G(G_JUMP_DONE + n) != 0 ? "ok" : "FAIL not counted");
		} else if (waited(8.0f)) {
			deleteTestCar();
			finishStep("FAIL still in slow motion");
		}
		break;
	}
}

static void runSummary(void)
{
	int packs = 0, ramps = 0, jumps = 0;
	for (int i = 0; i < NUM_PACKAGES; i++) packs += G(G_PACKAGE_DONE + i) != 0;
	for (int i = 0; i < NUM_RAMPAGES; i++) ramps += G(G_RAMPAGE_DONE + i) != 0;
	for (int i = 0; i < NUM_JUMPS; i++) jumps += G(G_JUMP_DONE + i) != 0;
	PS3_Logf("[autotest]     packages %d of %d (player info %d), rampages passed %d of %d, unique jumps %d of %d, money %d",
	         packs, NUM_PACKAGES, CWorld::Players[CWorld::PlayerInFocus].m_nCollectedPackages, ramps, NUM_RAMPAGES,
	         jumps, NUM_JUMPS, CWorld::Players[CWorld::PlayerInFocus].m_nMoney);
	finishStep("ok");
}

static void runOddJob(Step &s)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil) { finishStep("FAIL no player"); return; }
	switch (phase) {
	case 0: {
		deleteTestCar();
		if (p->bInVehicle)
			warpOutOfCar(p->GetPosition());
		CVector pos = p->GetPosition() + CVector(4.0f, 0.0f, 0.0f);
		float gz;
		if (groundAt(pos.x, pos.y, &gz, pos.z + 10.0f))
			pos.z = gz;
		testCar = makeVehicle(s.a, pos, p->m_fRotationCur);
		if (testCar == nil) { finishStep("FAIL no vehicle"); return; }
		warpIntoCar(testCar);
		nextPhase();
		break;
	}
	case 1:
		if (waited(2.0f)) {
			pressR3 = 6;
			nextPhase();
		}
		break;
	case 2:
		if (!stepNoted && waited(4.0f)) {
			stepNoted = 1;
			logScripts("4 s after R3");
			counterBefore = CTheScripts::IsPlayerOnAMission();
		}
		if (CCutsceneMgr::IsRunning() && pulseCross == 0)
			pulseCross = 20;
		if (waited(s.x)) {
			logScripts("before the second R3");
			pressR3 = 6;	// cancel it, as a player would
			nextPhase();
		}
		break;
	case 3:
		if (waited(4.0f)) {
			if (CTheScripts::IsPlayerOnAMission())
				CTheScripts::EndMissionScripts();
			deleteTestCar();
			finishStep(counterBefore ? "ok" : "FAIL R3 started nothing");
		}
		break;
	}
}

static void runGarage(Step &s)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil) { finishStep("FAIL no player"); return; }
	CGarage &g = CGarages::aGarages[s.a];
	switch (phase) {
	case 0: {
		deleteTestCar();
		if (p->bInVehicle)
			warpOutOfCar(p->GetPosition());
		float cx = g.GetGarageCenterX(), cy = g.GetGarageCenterY();
		float gz;
		if (!groundAt(cx, cy, &gz, g.m_fInfZ + 3.0f))
			gz = g.m_fInfZ;
		testCar = makeVehicle(MI_STALLION, CVector(cx, cy, gz), 0.0f);
		if (testCar == nil) { finishStep("FAIL no car"); return; }
		warpIntoCar(testCar);
		if (g.m_eGarageType == GARAGE_RESPRAY)
			p->SetWantedLevel(2);
		PS3_Logf("[autotest]     garage type %d state %d at (%.0f, %.0f, %.0f)", g.m_eGarageType, g.m_eGarageState, cx, cy, gz);
		nextPhase();
		break;
	}
	case 1:
		if (waited(12.0f)) {
			PS3_Logf("[autotest]     after 12 s: garage state %d, wanted %d, respray %d, money %d",
			         g.m_eGarageState, wantedLevel(), g.m_bResprayHappened, CWorld::Players[CWorld::PlayerInFocus].m_nMoney);
			bool resprayOk = g.m_eGarageType != GARAGE_RESPRAY || wantedLevel() == 0;
			deleteTestCar();
			clearWanted();
			finishStep(resprayOk ? "ok" : "FAIL the wanted level stayed");
		}
		break;
	}
}

// water for the boat: the nearest place the water level covers, on a 100 m
// grid, not one already found to have land over it
#define MAX_NOT_WATER 12
static CVector notWater[MAX_NOT_WATER];
static int numNotWater;

static bool findWater(CVector from, CVector *out)
{
	float best = 1.0e9f;
	for (float x = -2400.0f; x <= 2400.0f; x += 100.0f)
		for (float y = -2400.0f; y <= 2400.0f; y += 100.0f) {
			float wl;
			if (!CWaterLevel::GetWaterLevel(x, y, 0.0f, &wl, true))
				continue;
			bool bad = false;
			for (int i = 0; i < numNotWater; i++)
				if ((CVector2D(x, y) - CVector2D(notWater[i].x, notWater[i].y)).Magnitude() < 150.0f)
					bad = true;
			if (bad)
				continue;
			float d = (CVector2D(x, y) - CVector2D(from.x, from.y)).Magnitude();
			if (d > 60.0f && d < best) {
				best = d;
				*out = CVector(x, y, wl);
			}
		}
	return best < 1.0e9f;
}

static void runDrive(Step &s)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil) { finishStep("FAIL no player"); return; }
	switch (phase) {
	case 0: {
		deleteTestCar();
		if (p->bInVehicle)
			warpOutOfCar(p->GetPosition());
		CVector pos;
		if (s.b == 1) {
			if (!haveWater)
				haveWater = findWater(startPos, &waterSpot);
			if (!haveWater) { finishStep("skipped: no water found"); return; }
			pos = waterSpot;
			CStreaming::LoadScene(pos);
			float gz;
			if (groundAt(pos.x, pos.y, &gz, pos.z + 50.0f) && gz > pos.z) {
				// land there after all: the next one
				haveWater = false;
				if (numNotWater < MAX_NOT_WATER) {
					notWater[numNotWater++] = pos;
					break;
				}
				finishStep("skipped: no open water found");
				return;
			}
			PS3_Logf("[autotest]     water at (%.0f, %.0f, %.1f)", pos.x, pos.y, pos.z);
		} else {
			pos = p->GetPosition() + CVector(6.0f, 0.0f, 0.0f);
			float gz;
			if (!groundAt(pos.x, pos.y, &gz, pos.z + 10.0f)) { finishStep("skipped: no ground here"); return; }
			pos.z = gz + 0.5f;
		}
		testCar = makeVehicle(s.a, pos, p->m_fRotationCur);
		if (testCar == nil) { finishStep("FAIL no vehicle"); return; }
		warpIntoCar(testCar);
		driveFrom = testCar->GetPosition();
		driveMaxSpeed = 0.0f;
		nextPhase();
		break;
	}
	case 1:
		if (!vehicleAlive(testCar, testCarHandle)) { testCar = nil; pressCross = 0; finishStep("FAIL the vehicle is gone"); return; }
		if (phaseSecs() > 1.0f)
			pressCross = 1;
		driveMaxSpeed = Max(driveMaxSpeed, testCar->GetMoveSpeed().Magnitude() * 50.0f);
		if (waited(s.x > 0.0f ? s.x : 8.0f)) {
			pressCross = 0;
			float dist = (testCar->GetPosition() - driveFrom).Magnitude();
			PS3_Logf("[autotest]     model %d: %.0f m from the start, top speed %.1f m/s, health %.0f",
			         s.a, dist, driveMaxSpeed, testCar->m_fHealth);
			deleteTestCar();
			finishStep(driveMaxSpeed > 5.0f ? "ok" : "FAIL it didn't move");
		}
		break;
	}
}

static const char *missionRunning(char *buf)
{
	for (CRunningScript *sc = CTheScripts::pActiveScripts; sc; sc = sc->GetNext())
		if (sc->m_bIsMissionScript) {
			memcpy(buf, sc->m_abScriptName, 8);
			buf[8] = '\0';
			return buf;
		}
	strcpy(buf, "(none)");
	return buf;
}

static void runMission(Step &s)
{
	CPlayerPed *p = FindPlayerPed();
	if (p == nil) { finishStep("FAIL no player"); return; }
	char name[16];
	switch (phase) {
	case 0:
		// the one before ends first (its cleanup), then Toni goes there
		deleteTestCar();
		if (CTheScripts::IsPlayerOnAMission() || CCutsceneMgr::IsRunning())
			CTheScripts::EndMissionScripts();
		nextPhase();
		break;
	case 1: {
		if (!waited(1.0f))
			break;
		missionScriptName(s.a, name);
		// INIT*: the new game's setup (every pickup, car generator, jump
		// again: the pools fill up); without a name: the player's
		// creation (outfits) and the world's state
		const MissionPlace *mp = findPlace(s.a);
		if ((s.a < 9 && mp == nil) || name[0] == '\0' || strncmp(name, "INIT", 4) == 0) {
			static char msg[64];
			snprintf(msg, sizeof(msg), "skipped: \"%s\" sets up the game", name);
			finishStep(msg);
			return;
		}
		if (mp && strcmp(mp->name, name) != 0) {
			PS3_Logf("[autotest]     mission %d is \"%s\", not \"%s\" (another main.scm): started where Toni is", s.a, name, mp->name);
			mp = nil;
		}
		if (p->bInVehicle)
			warpOutOfCar(p->GetPosition() + CVector(0.0f, 0.0f, 1.0f));
		CVector pos = p->GetPosition();
		if (mp && mp->place != MP_HERE) {
			pos = CVector(mp->x, mp->y, mp->z);
			if (mp->place == MP_NEAR) {
				// next to the place the job checks he has left
				static const float off[4][2] = { { 12, 0 }, { -12, 0 }, { 0, 12 }, { 0, -12 } };
				for (int i = 0; i < 4; i++) {
					float gz;
					if (groundAt(mp->x + off[i][0], mp->y + off[i][1], &gz, mp->z > -50.0f ? mp->z + 10.0f : 1000.0f)) {
						pos = CVector(mp->x + off[i][0], mp->y + off[i][1], gz);
						break;
					}
				}
			} else {
				float gz;
				if (groundAt(pos.x, pos.y, &gz, mp->z > -50.0f ? mp->z + 5.0f : 1000.0f))
					pos.z = gz;
			}
			CStreaming::LoadScene(pos);
			warpOutOfCar(pos + CVector(0.0f, 0.0f, 0.5f));
		}
		if (mp && mp->model > 0) {
			CVector vpos = mp->place == MP_HERE ? pos + CVector(4.0f, 0.0f, 0.0f) : pos;
			float gz;
			if (groundAt(vpos.x, vpos.y, &gz, vpos.z + 10.0f))
				vpos.z = gz;
			testCar = makeVehicle(mp->model, vpos, p->m_fRotationCur);
			if (testCar)
				warpIntoCar(testCar);
		}
		PS3_Logf("[autotest]     mission %d is \"%s\"%s%s", s.a, name,
		         mp && mp->place != MP_HERE ? ", Toni put where it starts" : "",
		         testCar ? ", in its vehicle" : "");
		CTheScripts::SwitchToMission(s.a);
		testCar = nil;	// the mission's now (its cleanup may delete it)
		nextPhase();
		break;
	}
	case 2:
		if (CCutsceneMgr::IsRunning() && phaseSecs() > 2.0f && pulseCross == 0)
			pulseCross = 20;
		if (!stepNoted && phaseSecs() > 3.0f && !CTheScripts::IsPlayerOnAMission() && (stepNoted = 1))
			PS3_Logf("[autotest]     not on a mission any more after 3 s");
		if (waited(s.x)) {
			if (CCutsceneMgr::IsRunning())
				nextPhase();	// let it end first
			else
				phase = 4;
		}
		break;
	case 3:
		if (CCutsceneMgr::IsRunning() && pulseCross == 0)
			pulseCross = 20;
		if (!CCutsceneMgr::IsRunning() || waited(20.0f))
			phase = 4;
		break;
	case 4: {
		bool on = CTheScripts::IsPlayerOnAMission();
		PS3_Logf("[autotest]     script %s, on a mission %d, cutscene %d, wanted %d, %s",
		         missionRunning(name), on, CCutsceneMgr::IsRunning(), wantedLevel(),
		         p->bInVehicle ? "in a vehicle" : "on foot");
		finishStep(CCutsceneMgr::IsRunning() ? "ok (a cutscene still running)" :
		           on ? "ok" : "ok (ended by itself: see the lines above)");
		break;
	}
	}
}

static void runMissionEnd(void)
{
	switch (phase) {
	case 0:
		if (CCutsceneMgr::IsRunning()) {
			if (pulseCross == 0) pulseCross = 20;
			if (!waited(20.0f)) break;
		}
		CTheScripts::EndMissionScripts();
		nextPhase();
		break;
	case 1:
		if (waited(5.0f)) {
			logScripts("after the end");
			finishStep(CTheScripts::IsPlayerOnAMission() ? "FAIL still on a mission" : "ok");
		}
		break;
	}
}

// ---- start / end ----------------------------------------------------------------

static void finishRun(void)
{
	deleteTestCar();
	pressCross = 0;
	if (CTheScripts::IsPlayerOnAMission())
		CTheScripts::EndMissionScripts();
	releasePlayer();
	clearWanted();
	CPlayerPed *p = FindPlayerPed();
	if (p) {
		float gz;
		if (groundAt(startPos.x, startPos.y, &gz, startPos.z + 5.0f))
			startPos.z = gz + 0.5f;
		warpOutOfCar(startPos);
		p->m_fRotationCur = p->m_fRotationDest = startHeading;
	}
	PS3_Logf("[autotest] ==== DONE in %.1f min: %d steps, %d failures, %d skipped ====",
	         (nowUs() - runStartUs) / 60000000.0f, numSteps, failures, skipped);
	PS3_LogMemTag("[autotest]    ");
	rename(AUTOTEST_FILE, AUTOTEST_DONE_FILE);
	remove(AUTOTEST_PROGRESS_FILE);
	state = 3;
}

// 0: no autotest.txt, 1: the full test, 2: mode=check
static int readConfig(void)
{
	FILE *f = fopen(AUTOTEST_FILE, "rb");
	if (f == nil)
		return 0;
	char line[128];
	int check = 0;
	while (fgets(line, sizeof(line), f)) {
		int v;
		if (strncmp(line, "mode=check", 10) == 0)
			check = 1;
		if (sscanf(line, "from=%d", &v) == 1 && v >= 0)
			fromMission = v;
		if (sscanf(line, "to=%d", &v) == 1 && v >= 0)
			toMission = v;
		if (sscanf(line, "secs=%d", &v) == 1 && v >= 5 && v <= 600)
			missionSecs = (float)v;
		if (strncmp(line, "resume=0", 8) == 0)
			resumeOff = 1;
		if (strncmp(line, "nomissions", 10) == 0)
			noMissions = 1;
		if (strncmp(line, "nocollect", 9) == 0)
			noCollect = 1;
	}
	fclose(f);
	if (check)
		return 2;
	if (!resumeOff) {
		FILE *pf = fopen(AUTOTEST_PROGRESS_FILE, "rb");
		if (pf) {
			int died = -1;
			if (fscanf(pf, "%d", &died) == 1 && died >= 0) {
				char rest[64] = "";
				if (fgets(rest, sizeof(rest), pf))
					rest[strcspn(rest, "\r\n")] = '\0';
				PS3_Logf("[autotest] the previous run died in step %d (%s): going on after it", died + 1, rest);
				resumeFrom = died + 1;
			}
			fclose(pf);
		}
	}
	return 1;
}

static void FullFrame(void);

void
PS3_AutotestFrame(void)
{
	// a save calls CPad::FixPadsAfterSave -> UpdatePads -> here again
	static int inside;
	if (inside)
		return;
	inside = 1;
	if (state == 0) {
		int c = readConfig();
		state = c == 0 ? 3 : c == 2 ? 4 : 1;
		if (state == 1)
			PS3_Log("[autotest] autotest.txt found: the full test starts once a game is running (outdoors, no mission)");
	}
	if (state == 4)
		CheckFrame();
	else if (state == 1 || state == 2)
		FullFrame();
	inside = 0;
}

static void
FullFrame(void)
{
	if (gGameState != GS_PLAYING_GAME || FrontEndMenuManager.m_bGameNotLoaded)
		return;

	if (state == 1) {
		static uint64 countdownUs;
		static int shown = -1;
		CPlayerPed *p = FindPlayerPed();
		if (p == nil || FrontEndMenuManager.m_bMenuActive || CCutsceneMgr::IsRunning() ||
		    CTimer::GetTimeInMilliseconds() < 8000) {
			countdownUs = 0;
			return;
		}
		if (CTheScripts::IsPlayerOnAMission() || CGame::IsInInterior()) {
			static int warned;
			if (!warned++)
				PS3_Log("[autotest] waiting: a mission is running or Toni is indoors");
			countdownUs = 0;
			return;
		}
		if (countdownUs == 0) {
			countdownUs = nowUs();
			shown = -1;
		}
		int left = 10 - (int)((nowUs() - countdownUs) / 1000000);
		if (left > 0) {
			if (left != shown) {
				char a[48];
				wchar msg[48];
				snprintf(a, sizeof(a), "AUTOTEST STARTS IN %d S", left);
				AsciiToUnicode(a, msg);
				CHud::SetHelpMessage(msg, true);
				shown = left;
			}
			return;
		}
		{
			wchar none[1] = { 0 };
			CHud::SetHelpMessage(none, true);
		}
		startPos = p->GetPosition();
		startHeading = p->m_fRotationCur;
		startOutfit = G(G_OUTFIT);
		runStartUs = nowUs();
		failures = skipped = 0;
		haveWater = false;
		buildPlan();
		PS3_LogMemTag("[autotest] start:");
		state = 2;
		cur = resumeFrom < numSteps ? resumeFrom : numSteps;
		if (cur < numSteps)
			startStep();
	}

	if (waitingForLoad || FrontEndMenuManager.m_bMenuActive) {
		lastFrameUs = 0;	// the time in the menu isn't a frame
		return;
	}

	statsFrame();
	keepPlayerSafe();

	if (cur >= numSteps) {
		finishRun();
		return;
	}

	Step &s = steps[cur];
	switch (s.type) {
	case ST_SAVELOAD: runSaveLoad(s); break;
	case ST_OUTFIT: runOutfit(s); break;
	case ST_PACKAGE: runPackage(s); break;
	case ST_RAMPAGE: runRampage(s); break;
	case ST_JUMP: runJump(s); break;
	case ST_SUMMARY: runSummary(); break;
	case ST_ODDJOB: runOddJob(s); break;
	case ST_GARAGE: runGarage(s); break;
	case ST_DRIVE: runDrive(s); break;
	case ST_MISSION: runMission(s); break;
	case ST_MISSIONEND: runMissionEnd(); break;
	}

	// the buttons, over what the real pad says
	CPad *pad = CPad::GetPad(0);
	if (pressR3 > 0) {
		pad->NewState.RightShock = pressR3 > 3 ? 255 : 0;
		pressR3--;
	}
	if (pressCross)
		pad->NewState.Cross = 255;
	if (pulseCross > 0) {
		// on 3 frames, off the rest: a fresh press each time
		pad->NewState.Cross = pulseCross > 17 ? 255 : 0;
		pulseCross--;
	}
}

// From ps3.cpp, between frames: a load the test asked for
int
PS3_AutotestWantsLoad(void)
{
	if (!wantLoad)
		return 0;
	wantLoad = 0;
	testCar = nil;	// the load throws all the vehicles away
	lockedCar = nil;
	if (!CheckSlotDataValid(AUTOTEST_SLOT)) {
		PS3_Logf("[autotest]     the save doesn't check out (error %d)", PcSaveHelper.nErrorCode);
		waitingForLoad = 0;
		loadFailed = 1;
		return 0;
	}
	FrontEndMenuManager.m_bWantToRestart = true;
	FrontEndMenuManager.m_bWantToLoad = true;
	b_FoundRecentSavedGameWantToLoad = true;
	return 1;
}

// From ps3.cpp, after CGame::InitialiseWhenRestarting
void
PS3_AutotestLoaded(void)
{
	if (waitingForLoad) {
		waitingForLoad = 0;
		phaseStartUs = nowUs();
		lastFrameUs = 0;
	}
}

#else
void PS3_AutotestFrame(void) {}
int  PS3_AutotestWantsLoad(void) { return 0; }
void PS3_AutotestLoaded(void) {}
#endif
