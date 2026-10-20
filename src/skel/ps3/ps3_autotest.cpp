// ps3_autotest.cpp -- reLCS-PS3's check (USRDIR/autotest.txt).
//
// Put a file named autotest.txt in USRDIR (by FTP, any content), load a save
// and leave the game alone (on foot, no mission running is best). 10 seconds
// after the game is playing, the check goes through what can be missing or
// wrong in the LCS data and the port, a step per frame, and writes "[check]"
// lines to the log:
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
//      model. The ones loaded already were checked when they loaded (the
//      "[tex]" lines).
//   4. animations: every animation block (.ifp in anim.img) loaded.
//   5. audio: streamed files (radio, cutscenes) not found, empty samples.
//
// At the end a "[check] done" line with the totals, and autotest.txt is
// renamed to autotest.done.txt (the next boot doesn't run it again). While
// it runs the game goes on as usual; opening the pause menu waits.

#include "common.h"

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

void
PS3_AutotestFrame(void)
{
	if (ckState == -1) {
		FILE *f = fopen(PS3_USRDIR "/autotest.txt", "rb");
		ckState = f ? CK_WAIT : CK_OFF;
		if (f) {
			fclose(f);
			PS3_Log("[check] autotest.txt: the check starts 10 s after the game is playing");
		}
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

#else
void PS3_AutotestFrame(void) {}
#endif

int  PS3_AutotestWantsLoad(void) { return 0; }
void PS3_AutotestLoaded(void) {}
