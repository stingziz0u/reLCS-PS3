#include "common.h"
#include "platform.h"

#include "Game.h"
#include "main.h"
#include "RwHelper.h"
#include "Accident.h"
#include "Antennas.h"
#include "Bridge.h"
#include "CarCtrl.h"
#include "CarGen.h"
#include "CdStream.h"
#include "Clock.h"
#include "Clouds.h"
#include "Collision.h"
#include "Console.h"
#include "Coronas.h"
#include "Cranes.h"
#include "Credits.h"
#include "CutsceneMgr.h"
#include "DMAudio.h"
#include "Darkel.h"
#include "Debug.h"
#include "EventList.h"
#include "Ferry.h"
#include "FileLoader.h"
#include "FileMgr.h"
#include "FinalMissionMusic.h"
#include "Fire.h"
#include "Fluff.h"
#include "Font.h"
#include "Frontend.h"
#include "frontendoption.h"
#include "GameLogic.h"
#include "Garages.h"
#include "GenericGameStorage.h"
#include "Glass.h"
#include "HandlingMgr.h"
#include "Heli.h"
#include "Hud.h"
#include "IniFile.h"
#include "Lights.h"
#include "MBlur.h"
#include "Messages.h"
#include "MemoryCard.h"
#include "Object.h"
#include "MemoryHeap.h"
#include "Pad.h"
#include "Particle.h"
#include "ParticleObject.h"
#include "PedRoutes.h"
#include "Phones.h"
#include "Pickups.h"
#include "Plane.h"
#include "PlayerSkin.h"
#include "Population.h"
#include "Radar.h"
#include "Record.h"
#include "References.h"
#include "Renderer.h"
#include "Replay.h"
#include "Restart.h"
#include "RoadBlocks.h"
#include "Rubbish.h"
#include "SceneEdit.h"
#include "Script.h"
#include "ScriptCommands.h"
#include "Shadows.h"
#include "Skidmarks.h"
#include "SetPieces.h"
#include "SpecialFX.h"
#include "Stats.h"
#include "Streaming.h"
#include "PlayerPed.h"
#include "ModelIndices.h"
#include "Camera.h"
#include "SurfaceTable.h"
#include "TempColModels.h"
#include "Timecycle.h"
#include "TrafficLights.h"
#include "Train.h"
#include "TxdStore.h"
#include "User.h"
#include "VisibilityPlugins.h"
#include "WaterCannon.h"
#include "WaterLevel.h"
#include "Weapon.h"
#include "WeaponEffects.h"
#include "Weather.h"
#include "World.h"
#include "ZoneCull.h"
#ifdef ENABLE_3DS_BOTTOM_RADAR
#include "BottomScreen3DS.h"
#endif

#include "Zones.h"
#include "Occlusion.h"
#include "debugmenu.h"
#include "Ropes.h"
#include "WindModifiers.h"
#include "WaterCreatures.h"
#include "postfx.h"
#include "custompipes.h"
#include "screendroplets.h"
#include "VarConsole.h"
#ifdef USE_TEXTURE_POOL
#include "TexturePools.h"
#endif

eLevelName CGame::currLevel;
int32 CGame::currArea;
bool CGame::bDemoMode = true;
bool CGame::nastyGame = true;
bool CGame::frenchGame;
bool CGame::germanGame;
bool CGame::noProstitutes;
bool CGame::playingIntro;
char CGame::aDatFile[32];
#ifdef MORE_LANGUAGES
bool CGame::russianGame = false;
bool CGame::japaneseGame = false;
#endif
#ifndef MASTER
CVector CGame::PlayerCoords;
bool8 CGame::VarUpdatePlayerCoords;
#endif

int gameTxdSlot;

#if defined(__PS3__) && !defined(EXTENDED_PIPELINES)
static rw::TexDictionary *ps3GenericTxd;
static rw::Texture *(*ps3DefaultFindCB)(const char *name);

static rw::Texture*
PS3_GenericFindCB(const char *name)
{
	rw::Texture *tex = ps3DefaultFindCB(name);
	if(tex == nil && ps3GenericTxd)
		tex = ps3GenericTxd->find(name);
	return tex;
}

static void
PS3_SetGenericTxdFallback(void)
{
	ps3GenericTxd = CTxdStore::GetSlot(gameTxdSlot)->texDict;
	if(ps3DefaultFindCB == nil)
		ps3DefaultFindCB = rw::Texture::findCB;
	rw::Texture::findCB = PS3_GenericFindCB;
}
#endif
/* The first frame after a save load rebuilds MAIN and performs two complete
 * streaming scene loads.  Defer Callahan's flattened-world restoration until
 * both are finished so neither step can recreate its initial IPL phase. */
static bool gRestoreCallahanAfterLoad;
static bool gRestoreCallahanBMState;
static bool gRestoreCallahanPMState;

#ifdef SECUROM
uint8 gameProcessPirateCheck = 0;
#endif

bool DoRWStuffStartOfFrame(int16 TopRed, int16 TopGreen, int16 TopBlue, int16 BottomRed, int16 BottomGreen, int16 BottomBlue, int16 Alpha);
void DoRWStuffEndOfFrame(void);
#ifdef _3DS
#include <3ds.h>
#ifdef _3DS
#include "../../../common/3ds/GameInstallation.h"
#endif
static void
Show3DSStartupError(const char *text, bool gameFont = true)
{
	wchar message[384];
	AsciiToUnicode(text, message);
	gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
	for(unsigned i = 0; i < 2; i++) {
		u16 width, height;
		u8 *buffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &width, &height);
		const unsigned bytes = unsigned(width) * height * 3;
		memset(buffer, 0, bytes);
		GSPGPU_FlushDataCache(buffer, bytes);
		gfxScreenSwapBuffers(GFX_BOTTOM, false);
	}
	if(!gameFont){
		gfxSet3D(false);
		consoleInit(GFX_TOP, NULL);
		for(const char *p=text; *p; ++p){
			if(strncmp(p,"~n~",3)==0){ putchar('\n'); p+=2; }
			else putchar(*p);
		}
	}
	while(aptMainLoop()) {
		hidScanInput();
		if(hidKeysDown() & KEY_B) break;
		if(gameFont && DoRWStuffStartOfFrame(0, 0, 0, 0, 0, 0, 255)) {
			CSprite2d::SetRecipNearClip();
			CSprite2d::InitPerFrame();
			CFont::InitPerFrame();
			DefinedState();
			CFont::SetAlphaFade(255.0f);
			CFont::SetCentreOff();
			CFont::SetRightJustifyOff();
			CFont::SetJustifyOff();
			CFont::SetPropOn();
			CFont::SetBackgroundOff();
			CFont::SetDropShadowPosition(0);
			CFont::SetScale(SCREEN_SCALE_X(0.52f), SCREEN_SCALE_Y(1.1f));
			CFont::SetWrapx(SCREEN_WIDTH - SCREEN_SCALE_X(26.0f));
			CFont::SetFontStyle(FONT_LOCALE(FONT_STANDARD));
			CFont::SetColor(CRGBA(175, 175, 175, 255));
			CFont::PrintString(SCREEN_SCALE_X(26.0f), SCREEN_SCALE_Y(28.0f), message);
			CFont::DrawFonts();
			RwCameraEndUpdate(Scene.camera);
			RsCameraShowRaster(Scene.camera);
		}
		gspWaitForVBlank();
	}
	svcExitProcess();
}

static void
Initialise3DSRenderState(void)
{
	if(!rw::c3d::initialiseMaterialState())
		Show3DSStartupError("Device not supported~n~Press B.");
}

static void
Initialise3DSInstallation(void)
{
	static bool checked = false;
	if(checked) return;
	if(!RegtaInstall::validate(RegtaInstall::LCS, [](){
		if(!aptMainLoop()) svcExitProcess();
	})) Show3DSStartupError(RegtaInstall::errorMessage());
	checked = true;
}
#endif
#ifdef PS2_MENU
void MessageScreen(char *msg)
{
	//TODO: stretch_screen
	
	CRect rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
	CRGBA color(255, 255, 255, 255);

	DoRWStuffStartOfFrame(50, 50, 50, 0, 0, 0, 255);
	
	CSprite2d::InitPerFrame();
	CFont::InitPerFrame();
	DefinedState();

	CSprite2d *splash = LoadSplash(NULL);
	splash->Draw(rect, color, color, color, color);
	splash->DrawRect(CRect(SCREEN_SCALE_X(20.0f), SCREEN_SCALE_Y(110.0f), SCREEN_SCALE_X(620.0f), SCREEN_SCALE_Y(300.0f)), CRGBA(50, 50, 50, 192));
	
	CFont::SetFontStyle(FONT_BANK);
	CFont::SetBackgroundOff();
	CFont::SetWrapx(SCREEN_SCALE_FROM_RIGHT(190.0f)); // 450.0f
	CFont::SetScale(SCREEN_SCALE_X(1.0f), SCREEN_SCALE_Y(1.0f));
	CFont::SetCentreOn();
	CFont::SetCentreSize(SCREEN_SCALE_X(450.0f));
	CFont::SetJustifyOff();
	CFont::SetColor(CRGBA(255, 255, 255, 255));
	CFont::SetDropColor(CRGBA(32, 32, 32, 255));
	CFont::SetDropShadowPosition(3);
	CFont::SetPropOn();
	CFont::PrintString(SCREEN_SCALE_X(320.0f), SCREEN_SCALE_Y(130.0f), TheText.Get(msg));
	CFont::DrawFonts();
	
	DoRWStuffEndOfFrame();
}
#endif

bool
CGame::InitialiseOnceBeforeRW(void)
{
	CFileMgr::Initialise();
	CdStreamInit(MAX_CDCHANNELS);
	debug("size of matrix %d\n", sizeof(CMatrix));
	debug("size of placeable %d\n", sizeof(CPlaceable));
	debug("size of entity %d\n", sizeof(CEntity));
	debug("size of building %d\n", sizeof(CBuilding));
	debug("size of dummy %d\n", sizeof(CDummy));
#ifdef EXTENDED_COLOURFILTER
	CPostFX::InitOnce();
#endif
#ifdef CUSTOM_FRONTEND_OPTIONS
	// Not needed here but may be needed in future
	// if (numCustomFrontendOptions == 0 && numCustomFrontendScreens == 0)
	CustomFrontendOptionsPopulate();
#endif
	return true;
}

#ifndef LIBRW
#ifdef PS2_MATFX
void ReplaceMatFxCallback();
#endif // PS2_MATFX
#ifdef PS2_ALPHA_TEST
void ReplaceAtomicPipeCallback();
#endif // PS2_ALPHA_TEST
#endif // !LIBRW

bool
CGame::InitialiseRenderWare(void)
{
	ValidateVersion();
#ifdef USE_TEXTURE_POOL
	_TexturePoolsInitialise();
#endif

	CTxdStore::Initialise();
	CVisibilityPlugins::Initialise();

#ifdef GTA_PS2
	RpSkySelectTrueTSClipper(TRUE);
	RpSkySelectTrueTLClipper(TRUE);

	// PS2ManagerApplyDirectionalLightingCB() uploads the GTA lights
	// directly without going through RpWorld and all that
	SetupPS2ManagerDefaultLightingCallback();
	PreAllocateRwObjects();
#endif

	/* Create camera */
	Scene.camera = CameraCreate(SCREEN_WIDTH, SCREEN_HEIGHT, TRUE);
	ASSERT(Scene.camera != nil);
	if (!Scene.camera)
	{
		return (false);
	}
	
	RwCameraSetFarClipPlane(Scene.camera, 2000.0f);
	RwCameraSetNearClipPlane(Scene.camera, 0.9f);
	
	CameraSize(Scene.camera, nil, DEFAULT_VIEWWINDOW, DEFAULT_ASPECT_RATIO);
	
	/* Create a world */
	RwBBox  bbox;
	
	bbox.sup.x = bbox.sup.y = bbox.sup.z = 10000.0f;
	bbox.inf.x = bbox.inf.y = bbox.inf.z = -10000.0f;

	Scene.world = RpWorldCreate(&bbox);
	ASSERT(Scene.world != nil);
	if (!Scene.world)
	{
		CameraDestroy(Scene.camera);
		Scene.camera = nil;
		return (false);
	}
	
	/* Add the camera to the world */
	RpWorldAddCamera(Scene.world, Scene.camera);
	LightsCreate(Scene.world);

	CreateDebugFont();

#ifdef LIBRW
#ifdef PS2_MATFX
	rw::MatFX::envMapApplyLight = true;
	rw::MatFX::envMapUseMatColor = true;
	rw::MatFX::envMapFlipU = true;
#else
	rw::MatFX::envMapApplyLight = false;
	rw::MatFX::envMapUseMatColor = false;
	rw::MatFX::envMapFlipU = false;
#endif
	rw::RGBA envcol = { 64, 64, 64, 255 };
	rw::MatFX::envMapColor = envcol;
#else
#ifdef PS2_MATFX
	ReplaceMatFxCallback();
#endif // PS2_MATFX
#ifdef PS2_ALPHA_TEST
	ReplaceAtomicPipeCallback();
#endif // PS2_ALPHA_TEST
#endif // LIBRW

	PUSH_MEMID(MEMID_TEXTURES);
#ifdef _3DS
	if(!RegtaInstall::present("models/fonts.txd")){
		RegtaInstall::failure("models/fonts.txd", "Missing or empty file");
		Show3DSStartupError(RegtaInstall::errorMessage(), false);
	}
#endif
	CFont::Initialise();
#ifdef _3DS
	Initialise3DSInstallation();
#endif
	CHud::Initialise();
	CPlayerSkin::Initialise();
	POP_MEMID();

#ifdef EXTENDED_PIPELINES
	CustomPipes::CustomPipeInit();	// need Scene.world for this
#endif
#ifdef SCREEN_DROPLETS
	ScreenDroplets::InitDraw();
#endif

	return (true);
}

void CGame::ShutdownRenderWare(void)
{
#if defined(RW_3DS) && defined(RESTORIES_3DS_BUILD)
	ShutdownPlayerVehicleReflection3DS();
#endif
#ifdef ENABLE_3DS_BOTTOM_RADAR
	ShutdownBottomScreen();
#endif
#ifdef SCREEN_DROPLETS
	ScreenDroplets::Shutdown();
#endif
#ifdef EXTENDED_PIPELINES
	CustomPipes::CustomPipeShutdown();
#endif

	DestroySplashScreen();
	CHud::Shutdown();
	CFont::Shutdown();
	
	for ( int32 i = 0; i < NUMPLAYERS; i++ )
		CWorld::Players[i].DeletePlayerSkin();

	CPlayerSkin::Shutdown();
	
	DestroyDebugFont();
	
	/* Destroy world */
	LightsDestroy(Scene.world);
	RpWorldRemoveCamera(Scene.world, Scene.camera);
	RpWorldDestroy(Scene.world);
	
	/* destroy camera */
	CameraDestroy(Scene.camera);
	
	Scene.world = nil;
	Scene.camera = nil;
	
	CVisibilityPlugins::Shutdown();
	
#ifdef USE_TEXTURE_POOL
	_TexturePoolsShutdown();
#endif
}

bool CGame::InitialiseOnceAfterRW(void)
{
	TheText.Load();
	CTimer::Initialise();
	gpTempColModels->Initialise();
	mod_HandlingManager.Initialise();
	CSurfaceTable::Initialise("DATA\\SURFACE.DAT");
	CPedStats::Initialise();
	CTimeCycle::Initialise();
#ifdef GTA_PS2
	LoadingScreen("Loading the Game", "Initialising audio", GetRandomSplashScreen());
#endif
	DMAudio.Initialise();

#ifndef GTA_PS2
#ifdef EXTERNAL_3D_SOUND
	if ( DMAudio.GetNum3DProvidersAvailable() == 0 )
		FrontEndMenuManager.m_nPrefsAudio3DProviderIndex = NO_AUDIO_PROVIDER;

	if ( FrontEndMenuManager.m_nPrefsAudio3DProviderIndex == AUDIO_PROVIDER_NOT_DETERMINED || FrontEndMenuManager.m_nPrefsAudio3DProviderIndex == -2 )
	{
		FrontEndMenuManager.m_PrefsSpeakers = 0;
		FrontEndMenuManager.m_nPrefsAudio3DProviderIndex = DMAudio.AutoDetect3DProviders();
	}

	DMAudio.SetCurrent3DProvider(FrontEndMenuManager.m_nPrefsAudio3DProviderIndex);
	DMAudio.SetSpeakerConfig(FrontEndMenuManager.m_PrefsSpeakers);
#endif
	DMAudio.SetDynamicAcousticModelingStatus(FrontEndMenuManager.m_PrefsDMA);
	DMAudio.SetMusicMasterVolume(FrontEndMenuManager.m_PrefsMusicVolume);
	DMAudio.SetEffectsMasterVolume(FrontEndMenuManager.m_PrefsSfxVolume);
	DMAudio.SetEffectsFadeVol(127);
	DMAudio.SetMusicFadeVol(127);
#endif
	return true;
}

void
CGame::FinalShutdown(void)
	{
	FinalMissionMusic::EndLCSFinale();
	CTxdStore::Shutdown();
	CPedStats::Shutdown();
	CdStreamShutdown();
}

bool CGame::Initialise(const char* datFile)
{
	ResetLoadingScreenBar();
	strcpy(aDatFile, datFile);

#ifdef GTA_PS2
	// TODO: upload VU0 collision code here
#endif

	CPools::Initialise();

	if(gMakeResources)
		CVehicleModelInfo::Load(nil);

#ifndef GTA_PS2
#ifdef PED_CAR_DENSITY_SLIDERS
	// Load density values from gta3.ini only if our reVC.ini have them 0.6f
	if (CIniFile::PedNumberMultiplier == 0.6f && CIniFile::CarNumberMultiplier == 0.6f)
#endif
		CIniFile::LoadIniFile();
#endif
#ifdef USE_TEXTURE_POOL
	_TexturePoolsUnknown(false);
#endif
	currLevel = LEVEL_INDUSTRIAL;
	currArea = AREA_MAIN_MAP;

	PUSH_MEMID(MEMID_TEXTURES);
	LoadingScreen("Loading the Game", "Loading generic textures", GetRandomSplashScreen());
	gameTxdSlot = CTxdStore::AddTxdSlot("generic");
	CTxdStore::Create(gameTxdSlot);
	CTxdStore::AddRef(gameTxdSlot);

#ifdef EXTENDED_PIPELINES
	// for generic fallback
	CustomPipes::SetTxdFindCallback();
#elif defined(__PS3__)
	// LCS cars take their shared textures (xv_*: windows, shattered glass,
	// engine, plates...) from GENERIC.TXD, not from their own TXD; on PC
	// that's CustomPipes' find callback, off here
	PS3_SetGenericTxdFallback();
#endif

	LoadingScreen("Loading the Game", "Loading particles", nil);
	int particleTxdSlot = CTxdStore::AddTxdSlot("particle");
	CTxdStore::LoadTxd(particleTxdSlot, "MODELS/PARTICLE.TXD");
	CTxdStore::AddRef(particleTxdSlot);
	CTxdStore::SetCurrentTxd(gameTxdSlot);
	LoadingScreen("Loading the Game", "Setup game variables", nil);
	POP_MEMID();

#ifdef GTA_PS2
	CDma::SyncChannel(0, true);
#endif

	CGameLogic::InitAtStartOfGame();
	CReferences::Init();
	TheCamera.Init();
	TheCamera.SetRwCamera(Scene.camera);
	CDebug::DebugInitTextBuffer();
	ThePaths.Init();
	ThePaths.AllocatePathFindInfoMem(4500);
	CScriptPaths::Init();
	CWeather::Init();
	CCullZones::Init();
	COcclusion::Init();
	CCollision::Init();
	CSetPieces::Init();
	CTheZones::Init();
	CUserDisplay::Init();
	CMessages::Init();
	CMessages::ClearAllMessagesDisplayedByGame();
	CRecordDataForGame::Init();
	CRestart::Initialise();

	PUSH_MEMID(MEMID_WORLD);
	CWorld::Initialise();
	POP_MEMID();

	PUSH_MEMID(MEMID_TEXTURES);
	CParticle::Initialise();
	POP_MEMID();

	PUSH_MEMID(MEMID_ANIMATION);
	CAnimManager::Initialise();
	CCutsceneMgr::Initialise();
	POP_MEMID();

	PUSH_MEMID(MEMID_CARS);
	CCarCtrl::Init();
	POP_MEMID();

	PUSH_MEMID(MEMID_DEF_MODELS);
	InitModelIndices();
	CModelInfo::Initialise();
	CPickups::Init();
	CTheCarGenerators::Init();

	CdStreamAddImage("MODELS\\GTA3.IMG");

//	CFileLoader::LoadLevel("DATA\\DEFAULT.DAT");
	CFileLoader::LoadLevel(datFile);

	LoadingScreen("Loading the Game", "Add Particles", nil);
	CWorld::AddParticles();
	CVehicleModelInfo::LoadVehicleColours();
	CVehicleModelInfo::LoadEnvironmentMaps();
	CTheZones::PostZoneCreation();
	POP_MEMID();

	LoadingScreen("Loading the Game", "Setup paths", nil);
	ThePaths.PreparePathData();
	for (int i = 0; i < NUMPLAYERS; i++)
		CWorld::Players[i].Clear();
	CWorld::Players[0].LoadPlayerSkin();
	TestModelIndices();

	LoadingScreen("Loading the Game", "Setup water", nil);
	CWaterLevel::Initialise("DATA\\WATER.DAT");
	TheConsole.Init();
	CDraw::SetFOV(120.0f);
	CDraw::ms_fLODDistance = 500.0f;

	LoadingScreen("Loading the Game", "Setup streaming", nil);
	CStreaming::LoadInitialVehicles();
	CStreaming::LoadInitialPeds();
	CStreaming::RequestBigBuildings(LEVEL_GENERIC);
	CStreaming::LoadAllRequestedModels(false);
	CStreaming::RemoveIslandsNotUsed(currLevel);
	printf("Streaming uses %dK of its memory", (int)(CStreaming::ms_memoryUsed / 1024)); // original modifier was %d

	LoadingScreen("Loading the Game", "Load animations", GetRandomSplashScreen());
	PUSH_MEMID(MEMID_ANIMATION);
	CAnimManager::LoadAnimFiles();
	POP_MEMID();

	CStreaming::LoadInitialWeapons();
	CStreaming::LoadAllRequestedModels(0);
	CPed::Initialise();
	CRouteNode::Initialise();
	CEventList::Initialise();
#ifdef SCREEN_DROPLETS
	ScreenDroplets::Initialise();
#endif
#ifdef _3DS
	LoadingScreen("Loading", nil, nil);
	Initialise3DSRenderState();
#else
	LoadingScreen("Loading the Game", "Find big buildings", nil);
#endif
	CRenderer::Init();

	LoadingScreen("Loading the Game", "Setup game variables", nil);
	CRadar::Initialise();
	CRadar::LoadTextures();
	CWeapon::InitialiseWeapons();

	LoadingScreen("Loading the Game", "Setup traffic lights", nil);
	CTrafficLights::ScanForLightsOnMap();
	CRoadBlocks::Init();

	LoadingScreen("Loading the Game", "Setup game variables", nil);
	CPopulation::Initialise();
	CWorld::PlayerInFocus = 0;
	CCoronas::Init();
	CShadows::Init();
	CWeaponEffects::Init();
	CSkidmarks::Init();
	CAntennas::Init();
	CGlass::Init();
	gPhoneInfo.Initialise();
#ifdef GTA_SCENE_EDIT
	CSceneEdit::Initialise();
#endif

	LoadingScreen("Loading the Game", "Load scripts", nil);
	PUSH_MEMID(MEMID_SCRIPT);
	CTheScripts::Init();
	CGangs::Initialise();
	POP_MEMID();

	LoadingScreen("Loading the Game", "Setup game variables", nil);
	CClock::Initialise(1000);
	CHeli::InitHelis();
	CCranes::InitCranes();
	CMovingThings::Init();
	CDarkel::Init();
	CStats::Init();
	CPacManPickups::Init();
	CRubbish::Init();
	CClouds::Init();
	CSpecialFX::Init();
	CRopes::Init();
	CWaterCannons::Init();
	CBridge::Init();
	CGarages::Init();

	LoadingScreen("Loading the Game", "Position dynamic objects", nil);
	LoadingScreen("Loading the Game", "Initialise vehicle paths", nil);

	CFerry::InitFerrys();
	CTrain::InitTrains();
	CPlane::InitPlanes();
	CCredits::Init();
	CRecordDataForChase::Init();
	CReplay::Init();

	LoadingScreen("Loading the Game", "Start script", nil);
#ifdef PS2_MENU
	if ( !TheMemoryCard.m_bWantToLoad )
#endif
	{
		CTheScripts::StartTestScript();
		CTheScripts::Process();
		TheCamera.Process();
	}

	LoadingScreen("Loading the Game", "Load scene", nil);
	CCollision::ms_collisionInMemory = currLevel;
	for (int i = 0; i < MAX_PADS; i++)
		CPad::GetPad(i)->Clear(true);
#ifdef USE_TEXTURE_POOL
	_TexturePoolsUnknown(true);
#endif

#ifndef MASTER
	PlayerCoords = FindPlayerCoors();
	VarConsole.Add("X PLAYER COORD", &PlayerCoords.x, 10.0f, -10000.0f, 10000.0f, true);
	VarConsole.Add("Y PLAYER COORD", &PlayerCoords.y, 10.0f, -10000.0f, 10000.0f, true);
	VarConsole.Add("Z PLAYER COORD", &PlayerCoords.z, 10.0f, -10000.0f, 10000.0f, true);
	VarConsole.Add("UPDATE PLAYER COORD", &VarUpdatePlayerCoords, true);
#endif


	DMAudio.SetStartingTrackPositions(TRUE);
	DMAudio.ChangeMusicMode(MUSICMODE_GAME);
	return true;
}

bool CGame::ShutDown(void)
{
#ifdef USE_TEXTURE_POOL
	_TexturePoolsUnknown(false);
#endif
	CReplay::FinishPlayback();
	CReplay::EmptyReplayBuffer();
	CPlane::Shutdown();
	CTrain::Shutdown();
	CScriptPaths::Shutdown();
	//CWaterCreatures::RemoveAll();
	CSpecialFX::Shutdown();
	CGarages::Shutdown();
	CMovingThings::Shutdown();
	gPhoneInfo.Shutdown();
	CWeapon::ShutdownWeapons();
	CPedType::Shutdown();
	
	for (int32 i = 0; i < NUMPLAYERS; i++)
	{
		if ( CWorld::Players[i].m_pPed )
		{
			CWorld::Remove(CWorld::Players[i].m_pPed);
			delete CWorld::Players[i].m_pPed;
			CWorld::Players[i].m_pPed = nil;
		}
		
		CWorld::Players[i].Clear();
	}
	
	CRenderer::Shutdown();
	CWorld::ShutDown();
	DMAudio.DestroyAllGameCreatedEntities();
	CModelInfo::ShutDown();
	CAnimManager::Shutdown();
	CCutsceneMgr::Shutdown();
	CVehicleModelInfo::DeleteVehicleColourTextures();
	CVehicleModelInfo::ShutdownEnvironmentMaps();
	CRadar::Shutdown();
	CStreaming::Shutdown();
	CTxdStore::GameShutdown();
	CCollision::Shutdown();
	CWaterLevel::Shutdown();
	CRubbish::Shutdown();
	CClouds::Shutdown();
	CShadows::Shutdown();
	CCoronas::Shutdown();
	CSkidmarks::Shutdown();
	CWeaponEffects::Shutdown();
	CParticle::Shutdown();
	CPools::ShutDown();
	CHud::ReInitialise();
	CTxdStore::RemoveTxdSlot(gameTxdSlot);
	CMBlur::MotionBlurClose();
	CdStreamRemoveImages();
#ifdef USE_TEXTURE_POOL
	_TexturePoolsFinalShutdown();
#endif
	return true;
}

bool CGame::ReInitGameObjectVariables(bool load)
{
	CGameLogic::InitAtStartOfGame();
#ifdef PS2_MENU
	if ( !TheMemoryCard.m_bWantToLoad )
#endif
	{
		TheCamera.Init();
		TheCamera.SetRwCamera(Scene.camera);
	}
	CDebug::DebugInitTextBuffer();
	CWeather::Init();
	CUserDisplay::Init();
	CMessages::Init();
	CRestart::Initialise();
	CWorld::bDoingCarCollisions = false;
	CHud::ReInitialise();
	CRadar::Initialise();
	CCarCtrl::ReInit();
	CTimeCycle::Initialise();
	CDraw::SetFOV(120.0f);
	CDraw::ms_fLODDistance = 500.0f;
	CStreaming::RequestBigBuildings(LEVEL_GENERIC);
	CStreaming::RemoveIslandsNotUsed(LEVEL_INDUSTRIAL);
	CStreaming::RemoveIslandsNotUsed(LEVEL_COMMERCIAL);
	CStreaming::RemoveIslandsNotUsed(LEVEL_SUBURBAN);
	CStreaming::LoadAllRequestedModels(false);
	currArea = AREA_MAIN_MAP;
	CPed::Initialise();
	CEventList::Initialise();
#ifdef SCREEN_DROPLETS
	ScreenDroplets::Initialise();
#endif
	CWeapon::InitialiseWeapons();
	CPopulation::Initialise();
	
	for (int i = 0; i < NUMPLAYERS; i++)
		CWorld::Players[i].Clear();
	
	CWorld::PlayerInFocus = 0;
	CAntennas::Init();
	CGlass::Init();
	gPhoneInfo.Initialise();

	/* GenericLoad restores the native LCS timer, clock and absolute script wake
	 * times.  Initialising these after CTheScripts::Init(load) resets time to zero
	 * and leaves every restored controller asleep until the old timestamp is
	 * reached again.  Initialise the clocks first, then let the save overwrite
	 * them with its matching timeline. */
	CTimer::Initialise();
	CClock::Initialise(1000);

	/* The native LCS memory-card loader processes resident scripts before it
	 * returns.  A restarted 3DS game has already shut down C3dMarkers, so an
	 * interior script that creates a save/exit sphere would otherwise traverse
	 * the freed marker clump.  Recreate SpecialFX before the load-time script pass
	 * and do not initialise it a second time below. */
	if (load)
		CSpecialFX::Init();

	PUSH_MEMID(MEMID_SCRIPT);
	bool res = CTheScripts::Init(load);
	/* GenericLoad has already restored these native LCS blocks.  Initialising
	 * them here used to erase the freshly loaded state before gameplay resumed. */
	if (!load)
		CGangs::Initialise();
	POP_MEMID();

	if (!load)
		CTheCarGenerators::Init();
	CHeli::InitHelis();
	CMovingThings::Init();
	CDarkel::Init();
	if (!load)
		CStats::Init();
	/* GenericLoad has already restored the native LCS pickup block and its
	 * load-time script pass may have recreated safehouse interactions.  Clearing
	 * the pool here leaves their visuals or controller threads behind but removes
	 * the actual pickup interaction records. */
	if (!load)
		CPickups::Init();
	CPacManPickups::Init();
	if (!load)
		CGarages::Init();
	if (!load)
		CSpecialFX::Init();
	CRopes::Init();
	CWaterCannons::Init();
	CScriptPaths::Init();
	CParticle::ReloadConfig();
	CParticle::SetPixelData();

#ifdef PS2_MENU
	if ( !TheMemoryCard.m_bWantToLoad )
#else
	if ( !FrontEndMenuManager.m_bWantToLoad )
#endif
	{
		CCranes::InitCranes();
		CTheScripts::StartTestScript();
		CTheScripts::Process();
		TheCamera.Process();
		CFerry::InitFerrys();
		CTrain::InitTrains();
		CPlane::InitPlanes();
	}
	
	for (int32 i = 0; i < MAX_PADS; i++)
		CPad::GetPad(i)->Clear(true);

	return res;
}

void CGame::ReloadIPLs(void)
{
	// Empty and unused
}

void CGame::ShutDownForRestart(void)
{
	FinalMissionMusic::EndLCSFinale();
#ifdef USE_TEXTURE_POOL
	_TexturePoolsUnknown(false);
#endif
	CReplay::FinishPlayback();
	CReplay::EmptyReplayBuffer();
	DMAudio.DestroyAllGameCreatedEntities();
	CMovingThings::Shutdown();
	
	for (int i = 0; i < NUMPLAYERS; i++)
		CWorld::Players[i].Clear();

	CGarages::SetAllDoorsBackToOriginalHeight();
	CTheScripts::UndoBuildingSwaps();
	CTheScripts::UndoEntityInvisibilitySettings();
	CWorld::ClearForRestart();
	CGameLogic::ClearShortCut();
	CTimer::Shutdown();
	CStreaming::ReInit();
	CRadar::RemoveRadarSections();
	FrontEndMenuManager.UnloadTextures();
	CParticleObject::RemoveAllExpireableParticleObjects();
	//CWaterCreatures::RemoveAll(); 
	CSetPieces::Init();
	CPedType::Shutdown();
	CSpecialFX::Shutdown();
}


void CGame::InitialiseWhenRestarting(void)
{
	CRect rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
	CRGBA color(255, 255, 255, 255);
	
	CTimer::Initialise();
	CSprite2d::SetRecipNearClip();

	if (b_FoundRecentSavedGameWantToLoad || FrontEndMenuManager.m_bWantToLoad)
	{
		LoadSplash("splash1");
#ifndef XBOX_MESSAGE_SCREEN
		if (FrontEndMenuManager.m_bWantToLoad)
			FrontEndMenuManager.MessageScreen("FELD_WR", true);
#endif
	}

	b_FoundRecentSavedGameWantToLoad = false;
	
	TheCamera.Init();
	
	if ( FrontEndMenuManager.m_bWantToLoad == true )
	{
#ifdef XBOX_MESSAGE_SCREEN
		FrontEndMenuManager.SetDialogTimer(1000);
		DoRWStuffStartOfFrame(0, 0, 0, 0, 0, 0, 0);
		CSprite2d::InitPerFrame();
		CFont::InitPerFrame();
		FrontEndMenuManager.DrawOverlays();
		DoRWStuffEndOfFrame();
#endif
		RestoreForStartLoad();
	}
	
	bool bLoadSuccessful = ReInitGameObjectVariables(FrontEndMenuManager.m_bWantToLoad);
	
	if ( FrontEndMenuManager.m_bWantToLoad == true )
	{
		InitRadioStationPositionList();
		if ( bLoadSuccessful == true )
		{
			DMAudio.ResetTimers(CTimer::GetTimeInMilliseconds());
			CFerry::InitFerrys();
			SyncLCSFortStauntonWorld();
			/* LCS records that these one-shot world changes have happened, but not
			 * the C++ ferry table or bridge state themselves.  Reapply the effects
			 * after every load; this is equally required for native ARM saves and
			 * PS2-converted progress. */
			/* Keep the existing ferry/lift-bridge script contract untouched.  Its
			 * recovery is known-good on native LCS saves; Callahan is restored below
			 * independently because its flattened cWorldStream IPL is absent. */
			const uint32 ferryEnableIp = 83954;
			const uint32 bridgeEnableIp = 54931;
			const uint16 ferryCommand = CTheScripts::ScriptSpace[ferryEnableIp] |
				CTheScripts::ScriptSpace[ferryEnableIp + 1] << 8;
			const uint16 bridgeCommand = CTheScripts::ScriptSpace[bridgeEnableIp] |
				CTheScripts::ScriptSpace[bridgeEnableIp + 1] << 8;
			if(ferryCommand == COMMAND_ENABLE_FERRY_PATH &&
			   bridgeCommand == COMMAND_SET_BRIDGE_STATE) {
				const bool ferryUnlocked = *CTheScripts::GetPointerToScriptVariable(4516) != 0;
				CFerry::RestorePathAfterLoad(ferryUnlocked);

				/* The two Callahan construction barriers and the lift-bridge blocker
				 * are mission objects.  Converted saves keep the target object pool,
				 * so remove any still-live blocker whose native progress flag says it
				 * was already cleared.  Pool generation handles make stale refs safe. */
				const int32 openFlags[] = { 4492, 4496, 4496, 4488 };
				const int32 objectHandles[] = { 4952, 4956, 4960, 5116 };
				for(uint32 i = 0; i < ARRAY_SIZE(openFlags); i++) {
					if(*CTheScripts::GetPointerToScriptVariable(openFlags[i]) == 0)
						continue;
					int32 *handle = CTheScripts::GetPointerToScriptVariable(objectHandles[i]);
					CObject *object = CPools::GetObjectPool()->GetAt(*handle);
					if(object && object->ObjectCreatedBy == MISSION_OBJECT) {
						CWorld::Remove(object);
						CWorld::RemoveReferencesToDeletedObject(object);
						delete object;
					}
					*handle = 0;
				}

				if(*CTheScripts::GetPointerToScriptVariable(4488) != 0) {
					OpenLCSLiftBridgeRoadblock();
					CBridge::ForceBridgeState(STATE_LIFT_PART_MOVING_DOWN);
					ThePaths.SwitchRoadsOffInArea(-69.1f, -46.8f, -648.0f,
						-614.0f, 39.0f, 50.0f, false);
				}
			} else {
				debug("LCS infrastructure script contract mismatch; recovery skipped\n");
			}
			/* Capture these before MAIN's first reconstructed frame.  The final
			 * world phase is applied after both post-load LoadScene calls below. */
			gRestoreCallahanBMState =
				*CTheScripts::GetPointerToScriptVariable(4496) != 0;
			gRestoreCallahanPMState =
				*CTheScripts::GetPointerToScriptVariable(4492) != 0;
			gRestoreCallahanAfterLoad = true;
			CTrain::InitTrains();
			CPlane::InitPlanes();
		}
		else
		{
			for ( int32 i = 0; i < 50; i++ )
			{
				HandleExit();
				FrontEndMenuManager.MessageScreen("FED_LFL", true); // Loading save game has failed. The game will restart now. 
			}
			
			TheCamera.SetFadeColour(0, 0, 0);
			ShutDownForRestart();
			CTimer::Stop();
			CTimer::Initialise();
			//FrontEndMenuManager.m_bWantToLoad = false;
			ReInitGameObjectVariables(false);
			currLevel = LEVEL_GENERIC;
			CCollision::SortOutCollisionAfterLoad();
		}
#ifdef XBOX_MESSAGE_SCREEN
		FrontEndMenuManager.ProcessDialogTimer();
#endif
	}
	
	FrontEndMenuManager.m_bWantToLoad = true;
	CTimer::Update();
	
	DMAudio.ChangeMusicMode(MUSICMODE_GAME);
#ifdef USE_TEXTURE_POOL
	_TexturePoolsUnknown(true);
#endif
}

void CGame::Process(void) 
{
	if (FrontEndMenuManager.m_bWantToLoad) {
		/* Match reStories PC and the native LCS memory-card flow: saved running
		 * scripts are not resumed because their entity handles are transient.  MAIN
		 * rebuilds the controller graph from restored globals after all pools load. */
		CTheScripts::StartTestScript();
		CTheScripts::Process();
		TheCamera.Process();
		CStreaming::LoadScene(TheCamera.GetPosition());
		//CGame::GenerateTempPedAtStartOfNetworkGame();
		if (!CStreaming::HasModelLoaded(MI_PLAYER)){
			CStreaming::RequestSpecialModel(MI_PLAYER, "player", STREAMFLAGS_DEPENDENCY | STREAMFLAGS_DONT_REMOVE);
			CStreaming::LoadAllRequestedModels(false);
		}
		TheCamera.Process();
		CStreaming::LoadScene(TheCamera.GetPosition());
		if(gRestoreCallahanAfterLoad) {
			ApplyLCSWorldBuildingSwap(2, gRestoreCallahanBMState);
			ApplyLCSWorldBuildingSwap(4, gRestoreCallahanPMState);
			gRestoreCallahanAfterLoad = false;
		}
		FrontEndMenuManager.m_bWantToLoad = false;
	}
	PS3_GAME_STEP("CPad::UpdatePads");
	CPad::UpdatePads();
#ifdef USE_CUSTOM_ALLOCATOR
	ProcessTidyUpMemory();
#endif
#ifdef DEBUGMENU
	DebugMenuProcess();
#endif
	PS3_GAME_STEP("CCutsceneMgr::Update");
	/* Cutscene teardown and update both restore player state.  A restarted save
	 * can legitimately have no player until the main script recreates one. */
	if(FindPlayerPed())
		CCutsceneMgr::Update();

	PS3_GAME_STEP("FrontEndMenuManager.Process");
	if (!CCutsceneMgr::IsCutsceneProcessing() && !CTimer::GetIsCodePaused())
		FrontEndMenuManager.Process();

	PS3_GAME_STEP("CTheZones::Update");
	CTheZones::Update();
#ifdef SECUROM
	if (CTimer::GetTimeInMilliseconds() >= (35 * 60 * 1000) && gameProcessPirateCheck == 0){
		// if game not pirated
		// gameProcessPirateCheck = 1;
		// else
		gameProcessPirateCheck = 2;
	}
#endif
	//uint32 startTime = CTimer::GetCurrentTimeInCycles() / CTimer::GetCyclesPerMillisecond();
#ifdef _3DS
	uint64 profileStreamingStart = rw::c3d::profileTimerStart();
#endif
	CStreaming::Update();
#ifdef _3DS
	rw::c3d::profileRecordStreaming(profileStreamingStart);
#endif
	//uint32 processTime = CTimer::GetCurrentTimeInCycles() / CTimer::GetCyclesPerMillisecond() - startTime;
	CWindModifiers::Number = 0;
	if (!CTimer::GetIsPaused())
	{
#ifndef MASTER
		if (VarUpdatePlayerCoords) {
			CPlayerPed *player = FindPlayerPed();
			if(player) {
				player->Teleport(PlayerCoords);
				VarUpdatePlayerCoords = false;
			}
		}
#endif
		PS3_GAME_STEP("CSprite2d::SetRecipNearClip");
		CSprite2d::SetRecipNearClip();
		PS3_GAME_STEP("CSprite2d::InitPerFrame");
		CSprite2d::InitPerFrame();
		PS3_GAME_STEP("CFont::InitPerFrame");
		CFont::InitPerFrame();
		PS3_GAME_STEP("CRecordDataForGame::SaveOrRetrieveDataForThisFrame");
		CRecordDataForGame::SaveOrRetrieveDataForThisFrame();
		PS3_GAME_STEP("CRecordDataForChase::SaveOrRetrieveDataForThisFrame");
		CRecordDataForChase::SaveOrRetrieveDataForThisFrame();
		PS3_GAME_STEP("CPad::DoCheats");
		if(FindPlayerPed())
			CPad::DoCheats();
		PS3_GAME_STEP("CClock::Update");
		CClock::Update();
		PS3_GAME_STEP("CWeather::Update");
		CWeather::Update();
#ifdef __PS3__
		{
			extern float PS3_profRain;
			extern int PS3_profWeather;
			PS3_profRain = CWeather::Rain;
			PS3_profWeather = CWeather::NewWeatherType;
		}
#endif

		PUSH_MEMID(MEMID_SCRIPT);
		PS3_GAME_STEP("CTheScripts::Process");
		CTheScripts::Process();
		POP_MEMID();
		/* CLEAR_CUTSCENE is a script command, so repair an occupied player's
		 * vehicle state only after the complete script pass can no longer overwrite
		 * it with the cutscene/on-foot pose. */
		CCutsceneMgr::RestorePlayerVehicleAfterCutscene();
#if defined(_3DS) || defined(__PS3__)	// reLCS-PS3: a game fix of the 3DS port
		/* Save restoration can leave the player pool empty for a few normal
		 * frames while the restarted main script waits for streamed models.
		 * Timers, streaming and scripts above must keep advancing, but every
		 * world subsystem below assumes that Tony already exists. */
		if(FindPlayerPed() == nil)
			return;
#endif

		PS3_GAME_STEP("CCollision::Update");
		CCollision::Update();
		PS3_GAME_STEP("CScriptPaths::Update");
		CScriptPaths::Update();
		CFerry::UpdateFerrys();
		PS3_GAME_STEP("CTrain::UpdateTrains");
		CTrain::UpdateTrains();
		PS3_GAME_STEP("CPlane::UpdatePlanes");
		CPlane::UpdatePlanes();
		PS3_GAME_STEP("CHeli::UpdateHelis");
		CHeli::UpdateHelis();
		PS3_GAME_STEP("CDarkel::Update");
		CDarkel::Update();
		PS3_GAME_STEP("CSkidmarks::Update");
		CSkidmarks::Update();
		PS3_GAME_STEP("CAntennas::Update");
		CAntennas::Update();
		PS3_GAME_STEP("CGlass::Update");
		CGlass::Update();
#ifdef GTA_SCENE_EDIT
		PS3_GAME_STEP("CSceneEdit::Update");
		CSceneEdit::Update();
#endif
		PS3_GAME_STEP("CSetPieces::Update");
		CSetPieces::Update();
		PS3_GAME_STEP("CEventList::Update");
		CEventList::Update();
		PS3_GAME_STEP("CParticle::Update");
		CParticle::Update();
		PS3_GAME_STEP("gFireManager.Update");
		gFireManager.Update();
		//if (processTime >= 2) {
		//	CPopulation::Update(false);
		//} else {
		//	uint32 startTime = CTimer::GetCurrentTimeInCycles() / CTimer::GetCyclesPerMillisecond();
			CPopulation::Update(true);
		//	processTime = CTimer::GetCurrentTimeInCycles() / CTimer::GetCyclesPerMillisecond() - startTime;
		//}
		CWeapon::UpdateWeapons();
		PS3_GAME_STEP("CTheCarGenerators::Process");
		if (!CCutsceneMgr::IsRunning())
			CTheCarGenerators::Process();
		PS3_GAME_STEP("CCranes::UpdateCranes");
		if (!CReplay::IsPlayingBack())
			CCranes::UpdateCranes();
		PS3_GAME_STEP("CClouds::Update");
		CClouds::Update();
		//CMovingThings::Update(); // TODO
		CWaterCannons::Update();
		PS3_GAME_STEP("CUserDisplay::Process");
		CUserDisplay::Process();
		PS3_GAME_STEP("CReplay::Update");
		CReplay::Update();

		PUSH_MEMID(MEMID_WORLD);
		PS3_GAME_STEP("CWorld::Process");
		CWorld::Process();
		POP_MEMID();

		PS3_GAME_STEP("gAccidentManager.Update");
		gAccidentManager.Update();
		PS3_GAME_STEP("CPacManPickups::Update");
		CPacManPickups::Update();
		PS3_GAME_STEP("CPickups::Update");
		CPickups::Update();
		PS3_GAME_STEP("CGarages::Update");
		CGarages::Update();
		PS3_GAME_STEP("CRubbish::Update");
		CRubbish::Update();
		PS3_GAME_STEP("CSpecialFX::Update");
		CSpecialFX::Update();
		PS3_GAME_STEP("CRopes::Update");
		CRopes::Update();
		PS3_GAME_STEP("CTimeCycle::Update");
		CTimeCycle::Update();
		PS3_GAME_STEP("TheCamera.Process");
		if (CReplay::ShouldStandardCameraBeProcessed())
			TheCamera.Process();
		PS3_GAME_STEP("CCullZones::Update");
		CCullZones::Update();
		PS3_GAME_STEP("CGameLogic::Update");
		if (!CReplay::IsPlayingBack())
			CGameLogic::Update();
		PS3_GAME_STEP("CBridge::Update");
		CBridge::Update();
		PS3_GAME_STEP("CCoronas::DoSunAndMoon");
		CCoronas::DoSunAndMoon();
		PS3_GAME_STEP("CCoronas::Update");
		CCoronas::Update();
		PS3_GAME_STEP("CShadows::UpdateStaticShadows");
		CShadows::UpdateStaticShadows();
		PS3_GAME_STEP("CShadows::UpdatePermanentShadows");
		CShadows::UpdatePermanentShadows();
		PS3_GAME_STEP("gPhoneInfo.Update");
		gPhoneInfo.Update();
		if (!CReplay::IsPlayingBack())
		{
			PUSH_MEMID(MEMID_CARS);
			//if (processTime < 2)
				CCarCtrl::GenerateRandomCars();
			PS3_GAME_STEP("CRoadBlocks::GenerateRoadBlocks");
			CRoadBlocks::GenerateRoadBlocks();
			PS3_GAME_STEP("CCarCtrl::RemoveDistantCars");
			CCarCtrl::RemoveDistantCars();
			PS3_GAME_STEP("CCarCtrl::RemoveCarsIfThePoolGetsFull");
			CCarCtrl::RemoveCarsIfThePoolGetsFull();
			POP_MEMID();
		}
	}
	FinalMissionMusic::Update();
#ifdef GTA_PS2
	PS3_GAME_STEP("CMemCheck::DoTest");
	CMemCheck::DoTest();
#endif
#ifdef __PS3__
	PS3_ProfStep(PS3_PROF_GAME, nil);
#endif
}

#ifdef USE_CUSTOM_ALLOCATOR

// TODO(MIAMI)

int32 gNumMemMoved;

bool
MoveMem(void** ptr)
{
	if (*ptr) {
		gNumMemMoved++;
		void* newPtr = gMainHeap.MoveMemory(*ptr);
		if (*ptr != newPtr) {
			*ptr = newPtr;
			return true;
		}
	}
	return false;
}

// Some convenience structs
struct SkyDataPrefix
{
	uint32 pktSize1;
	uint32 data;	// pointer to data as read from TXD
	uint32 pktSize2;
	uint32 unused;
};

struct DMAGIFUpload
{
	uint32 tag1_qwc, tag1_addr;	// dmaref
	uint32 nop1, vif_direct1;

	uint32 giftag[4];
	uint32 gs_bitbltbuf[4];

	uint32 tag2_qwc, tag2_addr;	// dmaref
	uint32 nop2, vif_direct2;
};

// This is very scary. it depends on the exact memory layout of the DMA chains and whatnot
RwTexture*
MoveTextureMemoryCB(RwTexture* texture, void* pData)
{
#ifdef GTA_PS2
	bool* pRet = (bool*)pData;
	RwRaster* raster = RwTextureGetRaster(texture);
	_SkyRasterExt* rasterExt = RASTEREXTFROMRASTER(raster);
	if (raster->originalPixels == nil ||	// the raw data
		raster->cpPixels == raster->originalPixels ||	// old format, can't handle it
		rasterExt->dmaRefCount != 0 && rasterExt->dmaClrCount != 0)
		return texture;

	// this is the allocated pointer we will move
	SkyDataPrefix* prefix = (SkyDataPrefix*)raster->originalPixels;
	DMAGIFUpload* uploads = (DMAGIFUpload*)(prefix + 1);

	// We have 4qw for each upload,
	// i.e. for each buffer width of mip levels,
	// and the palette if there is one.
	// NB: this code does NOT support mipmaps!
	// so we assume two uploads (pixels and palette)
	//
	// each upload looks like this:
	//    (DMAcnt; NOP; VIF DIRECT(2))
	//     giftag (1, A+D)
	//      GS_BITBLTBUF
	//    (DMAref->pixel data; NOP; VIF DIRECT(5))
	// the DMArefs are what we have to adjust
	uintptr dataDiff, upload1Diff, upload2Diff, pixelDiff, paletteDiff;
	dataDiff = prefix->data - (uintptr)raster->originalPixels;
	upload1Diff = uploads[0].tag2_addr - (uintptr)raster->originalPixels;
	if (raster->palette)
		upload2Diff = uploads[1].tag2_addr - (uintptr)raster->originalPixels;
	pixelDiff = (uintptr)raster->cpPixels - (uintptr)raster->originalPixels;
	if (raster->palette)
		paletteDiff = (uintptr)raster->palette - (uintptr)raster->originalPixels;
	uint8* newptr = (uint8*)gMainHeap.MoveMemory(raster->originalPixels);
	if (newptr != raster->originalPixels) {
		// adjust everything
		prefix->data = (uintptr)newptr + dataDiff;
		uploads[0].tag2_addr = (uintptr)newptr + upload1Diff;
		if (raster->palette)
			uploads[1].tag2_addr = (uintptr)newptr + upload2Diff;
		raster->originalPixels = newptr;
		raster->cpPixels = newptr + pixelDiff;
		if (raster->palette)
			raster->palette = newptr + paletteDiff;

		if (pRet) {
			*pRet = true;
			return nil;
		}
	}
#else
	// nothing to do here really, everything should be in videomemory
#endif
	return texture;
}

bool
MoveAtomicMemory(RpAtomic* atomic, bool onlyOne)
{
	RpGeometry* geo = RpAtomicGetGeometry(atomic);

#if THIS_IS_COMPATIBLE_WITH_GTA3_RW31
	if (MoveMem((void**)&geo->triangles) && onlyOne)
		return true;
	if (MoveMem((void**)&geo->matList.materials) && onlyOne)
		return true;
	if (MoveMem((void**)&geo->preLitLum) && onlyOne)
		return true;
	if (MoveMem((void**)&geo->texCoords[0]) && onlyOne)
		return true;
	if (MoveMem((void**)&geo->texCoords[1]) && onlyOne)
		return true;

	// verts and normals of morph target are allocated together
	int vertDiff;
	if (geo->morphTarget->normals)
		vertDiff = geo->morphTarget->normals - geo->morphTarget->verts;
	if (MoveMem((void**)&geo->morphTarget->verts)) {
		if (geo->morphTarget->normals)
			geo->morphTarget->normals = geo->morphTarget->verts + vertDiff;
		if (onlyOne)
			return true;
	}

	RpMeshHeader* oldmesh = geo->mesh;
	if (MoveMem((void**)&geo->mesh)) {
		// index pointers are allocated together with meshes,
		// have to relocate those too
		RpMesh* mesh = (RpMesh*)(geo->mesh + 1);
		uintptr reloc = (uintptr)geo->mesh - (uintptr)oldmesh;
		for (int i = 0; i < geo->mesh->numMeshes; i++)
			mesh[i].indices = (RxVertexIndex*)((uintptr)mesh[i].indices + reloc);
		if (onlyOne)
			return true;
	}
#else
	// we could do something in librw here
#endif
	return false;
}

bool
MoveColModelMemory(CColModel& colModel, bool onlyOne)
{
#if GTA_VERSION >= GTA3_PS2_160
	// hm...should probably only do this if ownsCollisionVolumes
	// but it doesn't exist on PS2...
	if (!colModel.ownsCollisionVolumes)
		return false;
#endif

	if (MoveMem((void**)&colModel.spheres) && onlyOne)
		return true;
	if (MoveMem((void**)&colModel.lines) && onlyOne)
		return true;
	if (MoveMem((void**)&colModel.boxes) && onlyOne)
		return true;
	if (MoveMem((void**)&colModel.vertices) && onlyOne)
		return true;
	if (MoveMem((void**)&colModel.triangles) && onlyOne)
		return true;
	if (MoveMem((void**)&colModel.trianglePlanes) && onlyOne)
		return true;
	return false;
}

RpAtomic*
MoveAtomicMemoryCB(RpAtomic* atomic, void* pData)
{
	bool* pRet = (bool*)pData;
	if (pRet == nil)
		MoveAtomicMemory(atomic, false);
	else if (MoveAtomicMemory(atomic, true)) {
		*pRet = true;
		return nil;
	}
	return atomic;
}

bool
TidyUpModelInfo(CBaseModelInfo* modelInfo, bool onlyone)
{
	if (modelInfo->GetColModel() && modelInfo->DoesOwnColModel())
		if (MoveColModelMemory(*modelInfo->GetColModel(), onlyone))
			return true;

	RwObject* rwobj = modelInfo->GetRwObject();
	if (RwObjectGetType(rwobj) == rpATOMIC)
		if (MoveAtomicMemory((RpAtomic*)rwobj, onlyone))
			return true;
	if (RwObjectGetType(rwobj) == rpCLUMP) {
		bool ret = false;
		if (onlyone)
			RpClumpForAllAtomics((RpClump*)rwobj, MoveAtomicMemoryCB, &ret);
		else
			RpClumpForAllAtomics((RpClump*)rwobj, MoveAtomicMemoryCB, nil);
		if (ret)
			return true;
	}

	if (modelInfo->GetModelType() == MITYPE_PED && ((CPedModelInfo*)modelInfo)->m_hitColModel)
		if (MoveColModelMemory(*((CPedModelInfo*)modelInfo)->m_hitColModel, onlyone))
			return true;

	return false;
}
#endif


void CGame::DrasticTidyUpMemory(bool flushDraw)
{
#ifdef USE_CUSTOM_ALLOCATOR
	bool removedCol = false;

	TidyUpMemory(true, flushDraw);

	if (gMainHeap.GetLargestFreeBlock() < 200000 && !playingIntro) {
		CStreaming::RemoveIslandsNotUsed(LEVEL_INDUSTRIAL);
		CStreaming::RemoveIslandsNotUsed(LEVEL_COMMERCIAL);
		CStreaming::RemoveIslandsNotUsed(LEVEL_SUBURBAN);
		TidyUpMemory(true, flushDraw);
	}

	if (gMainHeap.GetLargestFreeBlock() < 200000 && !playingIntro) {
		CModelInfo::RemoveColModelsFromOtherLevels(LEVEL_GENERIC);
		TidyUpMemory(true, flushDraw);
		removedCol = true;
	}

	if (gMainHeap.GetLargestFreeBlock() < 200000 && !playingIntro) {
		CStreaming::RemoveBigBuildings(LEVEL_INDUSTRIAL);
		CStreaming::RemoveBigBuildings(LEVEL_COMMERCIAL);
		CStreaming::RemoveBigBuildings(LEVEL_SUBURBAN);
		TidyUpMemory(true, flushDraw);
	}

	if (removedCol) {
		// different on PS2
		CFileLoader::LoadCollisionFromDatFile(CCollision::ms_collisionInMemory);
	}

	if (!playingIntro)
		CStreaming::RequestBigBuildings(currLevel);

	CStreaming::LoadAllRequestedModels(true);
#endif
}

void CGame::TidyUpMemory(bool moveTextures, bool flushDraw)
{
#ifdef USE_CUSTOM_ALLOCATOR
	printf("Largest free block before tidy %d\n", gMainHeap.GetLargestFreeBlock());

	if (moveTextures) {
		if (flushDraw) {
#ifdef GTA_PS2
			for (int i = 0; i < sweMaxFlips + 1; i++) {
#else
			for (int i = 0; i < 5; i++) {	// probably more than needed
#endif
				RwCameraBeginUpdate(Scene.camera);
				RwCameraEndUpdate(Scene.camera);
				RwCameraShowRaster(Scene.camera, nil, 0);
			}
			}
		int fontSlot = CTxdStore::FindTxdSlot("fonts");

		for (int i = 0; i < TXDSTORESIZE; i++) {
			if (i == fontSlot ||
				CTxdStore::GetSlot(i) == nil)
				continue;
			RwTexDictionary* txd = CTxdStore::GetSlot(i)->texDict;
			if (txd)
				RwTexDictionaryForAllTextures(txd, MoveTextureMemoryCB, nil);
		}
		}

	// animations
	for (int i = 0; i < NUMANIMATIONS; i++) {
		CAnimBlendHierarchy* anim = CAnimManager::GetAnimation(i);
		if (anim == nil)
			continue;	// cannot happen
		anim->MoveMemory();
	}

	// model info
	for (int i = 0; i < MODELINFOSIZE; i++) {
		CBaseModelInfo* mi = CModelInfo::GetModelInfo(i);
		if (mi == nil)
			continue;
		TidyUpModelInfo(mi, false);
	}

	printf("Largest free block after tidy %d\n", gMainHeap.GetLargestFreeBlock());
#endif
	}

void CGame::ProcessTidyUpMemory(void)
{
#ifdef USE_CUSTOM_ALLOCATOR
	static int32 modelIndex = 0;
	static int32 animIndex = 0;
	static int32 txdIndex = 0;
	bool txdReturn = false;
	RwTexDictionary* txd = nil;
	gNumMemMoved = 0;

	// model infos
	for (int numCleanedUp = 0; numCleanedUp < 10; numCleanedUp++) {
		CBaseModelInfo* mi;
		do {
			mi = CModelInfo::GetModelInfo(modelIndex);
			modelIndex++;
			if (modelIndex >= MODELINFOSIZE)
				modelIndex = 0;
		} while (mi == nil);

		if (TidyUpModelInfo(mi, true))
			return;
	}

	// tex dicts
	for (int numCleanedUp = 0; numCleanedUp < 3; numCleanedUp++) {
		if (gNumMemMoved > 80)
			break;

		do {
#ifdef FIX_BUGS
			txd = nil;
#endif
			if (CTxdStore::GetSlot(txdIndex))
				txd = CTxdStore::GetSlot(txdIndex)->texDict;
			txdIndex++;
			if (txdIndex >= TXDSTORESIZE)
				txdIndex = 0;
		} while (txd == nil);

		RwTexDictionaryForAllTextures(txd, MoveTextureMemoryCB, &txdReturn);
		if (txdReturn)
			return;
		}

	// animations
	CAnimBlendHierarchy* anim;
	do {
		anim = CAnimManager::GetAnimation(animIndex);
		animIndex++;
		if (animIndex >= NUMANIMATIONS)
			animIndex = 0;
	} while (anim == nil);	// always != nil
	anim->MoveMemory(true);
#endif
}

void
CGame::InitAfterFocusLoss()
{
	FrontEndMenuManager.m_nPrefsAudio3DProviderIndex = FrontEndMenuManager.m_lastWorking3DAudioProvider;
	DMAudio.SetCurrent3DProvider(FrontEndMenuManager.m_lastWorking3DAudioProvider);

	if (!FrontEndMenuManager.m_bGameNotLoaded && !FrontEndMenuManager.m_bMenuActive)
		FrontEndMenuManager.m_bStartUpFrontEndRequested = true;
}

bool
CGame::CanSeeWaterFromCurrArea(void)
{
	return currArea == AREA_MAIN_MAP || currArea == AREA_MANSION
		|| currArea == AREA_HOTEL;
}

bool
CGame::CanSeeOutSideFromCurrArea(void)
{
	return currArea == AREA_MAIN_MAP || currArea == AREA_MALL ||
		currArea == AREA_MANSION || currArea == AREA_HOTEL;
}
