#define WITHD3D
#include "common.h"
#ifdef __PS3__
#include "ps3_platform.h"	// PS3_Logf
#endif

#include "main.h"
#include "General.h"
#include "Lights.h"
#include "ModelInfo.h"
#include "Treadable.h"
#include "Ped.h"
#include "Vehicle.h"
#include "Boat.h"
#include "Heli.h"
#include "Bike.h"
#include "Object.h"
#include "PathFind.h"
#include "Collision.h"
#include "VisibilityPlugins.h"
#include "Clock.h"
#include "Weather.h"
#include "World.h"
#include "Camera.h"
#include "ModelIndices.h"
#include "Streaming.h"
#include "Shadows.h"
#include "Coronas.h"
#include "PointLights.h"
#include "Occlusion.h"
#include "Renderer.h"
#include "custompipes.h"
#include "Frontend.h"
#include "Ferry.h"
#include "Plane.h"
#include "WaterLevel.h"
#include "Pools.h"	// patch 91: NewShad* tracker
#include "Hud.h"	// patch 93: the decal test mode on screen
#include "Font.h"	// AsciiToUnicode
#include "ps3_glfw.h"	// GLFW_GAMEPAD_BUTTON_* (PS3_PadButtons)
#include "Pad.h"
#include "src/ps3/rwps3.h"	// patch 93: rw::ps3::decalMode, decalStats
#ifdef __PS3__
#include <stdlib.h>	// qsort, bsearch (patch 28)
#include "Timecycle.h"
#endif

// maybe some day...
//#define GTA_WORLDSTREAM

bool gbShowPedRoadGroups;
bool gbShowCarRoadGroups;
bool gbShowCollisionPolys;
bool gbShowCollisionPolysReflections;
bool gbShowCollisionPolysNoShadows;
bool gbShowCollisionLines;
bool gbBigWhiteDebugLightSwitchedOn;

bool gbDontRenderBuildings;
bool gbDontRenderBigBuildings;
bool gbDontRenderPeds;
bool gbDontRenderObjects;
bool gbDontRenderVehicles;

bool gbRenderDebugEnvMap;

// unused
bool gbLighting;

// unused
int16 TestCloseThings;
int16 TestBigThings;

struct EntityInfo
{
	CEntity *ent;
	float sort;
};

CLinkList<EntityInfo> gSortedVehiclesAndPeds;

int32 CRenderer::ms_nNoOfVisibleEntities;
CEntity *CRenderer::ms_aVisibleEntityPtrs[NUMVISIBLEENTITIES];
CEntity *CRenderer::ms_aInVisibleEntityPtrs[NUMINVISIBLEENTITIES];
int32 CRenderer::ms_nNoOfInVisibleEntities;
int32 CRenderer::ms_nNoOfVisibleVehicles;
CEntity *CRenderer::ms_aVisibleVehiclePtrs[NUMVISIBLEENTITIES];
#ifdef NEW_RENDERER
int32 CRenderer::ms_nNoOfVisibleBuildings;
CEntity *CRenderer::ms_aVisibleBuildingPtrs[NUMVISIBLEENTITIES];
#endif

CVector CRenderer::ms_vecCameraPosition;
CVehicle *CRenderer::m_pFirstPersonVehicle;
bool CRenderer::m_loadingPriority;
float CRenderer::ms_lodDistScale = 1.2f;

void
CRenderer::Init(void)
{
	gSortedVehiclesAndPeds.Init(40);
	if(gMakeResources)
		SortBIGBuildings();
}

void
CRenderer::Shutdown(void)
{
	gSortedVehiclesAndPeds.Shutdown();
}

void
CRenderer::PreRender(void)
{
	int i;
	CLink<CVisibilityPlugins::AlphaObjectInfo> *node;

#ifdef __PS3__
	// patch 86: what PreRender spends its time on (2-4 ms a frame with the
	// burning cars of The Trouble with Triads, 0.5-1 otherwise), by type,
	// in "[prof] scene"
	static const char *ps3PreTag[] = { "prerender: nothing", "prerender: buildings", "prerender: vehicles",
	                                   "prerender: peds", "prerender: objects", "prerender: dummies" };
#define PS3_PRERENDER(e) do { CEntity *pe_ = (e); unsigned long long t0_ = PS3_Ticks(); pe_->PreRender(); \
		PS3_ProfAdd(PS3_PROF_SCENE, ps3PreTag[pe_->GetType() < 6 ? pe_->GetType() : 0], PS3_Ticks() - t0_); } while(0)
#else
#define PS3_PRERENDER(e) (e)->PreRender()
#endif
	for(i = 0; i < ms_nNoOfVisibleEntities; i++){
		PS3_CRUMB("PreRender visible: model|type<<16", ms_aVisibleEntityPtrs[i]->GetModelIndex() | ms_aVisibleEntityPtrs[i]->GetType()<<16);
		PS3_PRERENDER(ms_aVisibleEntityPtrs[i]);
	}

	for(i = 0; i < ms_nNoOfVisibleVehicles; i++)
		PS3_PRERENDER(ms_aVisibleVehiclePtrs[i]);
#ifdef NEW_RENDERER
	if(gbNewRenderer){
		// How is this done with cWorldStream?
		for(i = 0; i < ms_nNoOfVisibleBuildings; i++)
			ms_aVisibleBuildingPtrs[i]->PreRender();
		for(node = CVisibilityPlugins::m_alphaBuildingList.head.next;
		    node != &CVisibilityPlugins::m_alphaBuildingList.tail;
		    node = node->next)
			((CEntity*)node->item.entity)->PreRender();
	}
#endif

	for (i = 0; i < ms_nNoOfInVisibleEntities; i++) {
#ifdef SQUEEZE_PERFORMANCE
		if (ms_aInVisibleEntityPtrs[i]->IsVehicle() && ((CVehicle*)ms_aInVisibleEntityPtrs[i])->IsHeli())
#endif
		PS3_CRUMB("PreRender invisible: model|type<<16", ms_aInVisibleEntityPtrs[i]->GetModelIndex() | ms_aInVisibleEntityPtrs[i]->GetType()<<16);
		PS3_PRERENDER(ms_aInVisibleEntityPtrs[i]);
	}

	for(node = CVisibilityPlugins::m_alphaEntityList.head.next;
	    node != &CVisibilityPlugins::m_alphaEntityList.tail;
	    node = node->next){
		PS3_CRUMB("PreRender alpha: model|type<<16", ((CEntity*)node->item.entity)->GetModelIndex() | ((CEntity*)node->item.entity)->GetType()<<16);
		PS3_PRERENDER((CEntity*)node->item.entity);
	}
#undef PS3_PRERENDER

	PS3_CRUMB("CHeli::SpecialHeliPreRender", 0);
#ifdef __PS3__
	PS3_PROF_CALL(PS3_PROF_SCENE, "prerender: SpecialHeliPreRender", CHeli::SpecialHeliPreRender());
#else
	CHeli::SpecialHeliPreRender();
#endif
	PS3_CRUMB("CShadows::RenderExtraPlayerShadows", 0);
#ifdef __PS3__
	PS3_PROF_CALL(PS3_PROF_SCENE, "prerender: RenderExtraPlayerShadows", CShadows::RenderExtraPlayerShadows());
#else
	CShadows::RenderExtraPlayerShadows();
#endif
}

#ifdef __PS3__
void PS3_BigBuildingLogEntity(const char *what, CEntity *ent, int level, CPtrNode *node, unsigned where);	// World.cpp
#endif
#ifdef __PS3__
// patch 22: the entity being drawn, for the crash dump (Ped.cpp); nil out of RenderScene
CEntity *PS3_renderEntity;
#define PS3_RENDER_ENTITY(tag, e) (PS3_renderEntity = (e), PS3_CRUMB(tag, (e)->GetModelIndex() | (e)->GetType()<<16))
#else
#define PS3_RENDER_ENTITY(tag, e)
#endif

#ifdef __PS3__
// ---- patch 28: LCS's LOD buildings (pop-in) -------------------------------------
//
// A LOD building (big building) hides itself near the camera when its
// detailed model ("non-LOD", GetRelatedModel) is loaded and faded in. That
// says nothing about this detailed instance: it can be outside the sector
// scan (LCS draws some detailed buildings farther than LOD_DISTANCE), off
// its own range, or not scanned yet, and then neither is drawn: a hole that
// pops when the camera moves. Flying makes it worse (the LOD multiplier
// grows with height). REGTA-3DSPort-Complete found the same and pairs each
// LOD with the detailed instances the sector scan actually accepted this
// frame; this is that idea without the 3DS's draw budgets: the LOD hides
// only when a detailed instance of its model, at its place, was drawn.
// USRDIR/nolodpairs.txt: the game's rule, to compare.
bool gPS3LodPairs = true;
bool gPS3BoundLod = true;	// patch 125: USRDIR/noboundlod.txt turns it off
static int16 ps3LodHigh[MODELINFOSIZE];	// LOD model -> its detailed model, -1
static uint8 ps3HasLod[MODELINFOSIZE];	// detailed model with a LOD model
static bool ps3LodPairsBuilt;
static uint32 ps3VisFrame = 1;
struct PS3VisibleHigh {
	uint32 frame;
	int32 n;
	CEntity *ent[4];
};
static PS3VisibleHigh ps3VisHigh[MODELINFOSIZE];
// patch 38: every detailed instance with a LOD model drawn this frame. A LOD
// often stands for a whole block of detailed models while it names only one
// (GetRelatedModel): with that one off screen and its neighbours drawn,
// patch 28 kept the LOD over them (Harwood's road, Ren's video: the textures
// fighting; gone with nolodpairs.txt). Now any of them drawn inside the
// LOD's bounds lets it hide.
// patch 124: every building accepted, not only the models some LOD names,
// and the frame before as well. LODcustroadpm1 / pm2 stand for a whole stretch
// of Staunton's roads (FS_custroadA*, com_roadkb*, com_cust_roads*) none of
// which is the LOD's own model, and the LOD is decided before the sector scan
// accepts them: patch 38 found nothing inside and kept the LOD over the drawn
// roads (Ren's dark wedges, the subway stairs: patch 123's log, all at one
// distance from those LODs). Centres, not pointers: the frame before's
// entities may be gone
static CVector ps3DrawnCentre[2][1024];
static int32 ps3NumDrawn[2];
static uint32 ps3DrawnFrame[2];
static int32 ps3NumDrawnHigh;	// the [lod] reports

static void
PS3_NoteDrawn(CEntity *ent)
{
	int k = ps3VisFrame & 1;
	if(ps3DrawnFrame[k] != ps3VisFrame){
		ps3DrawnFrame[k] = ps3VisFrame;
		ps3NumDrawn[k] = 0;
	}
	if(ps3NumDrawn[k] < 1024)
		ps3DrawnCentre[k][ps3NumDrawn[k]++] = ent->GetBoundCentre();
}

// patch 127: noted where the detailed buildings are really drawn
// (CEntity::Render), whatever list brought them there. Patch 124 noted them
// in SetupEntityVisibility, past the early returns some take (bDontStream,
// draw last...): LODcustroadA1 (the wedge's LOD, its bounds holding the
// wedge) found nothing inside and stayed over FS_custroadA1 ("kept, detailed
// one not drawn" 19-20 a frame; mode 18 without LODs: the wedge gone, Ren)
void
PS3_NoteDrawnBuilding(CEntity *e)
{
	PS3_NoteDrawn(e);
}

// a detailed instance drawn (this frame so far, or the frame before) inside this LOD's bounds
static bool
PS3_HighDrawnInside(CEntity *lod)
{
	CVector c = lod->GetBoundCentre();
	float r = lod->GetBoundRadius();
	for(int k = 0; k < 2; k++){
		uint32 f = ps3DrawnFrame[k];
		if(f != ps3VisFrame && f + 1 != ps3VisFrame)
			continue;
		for(int32 i = 0; i < ps3NumDrawn[k]; i++)
			if((ps3DrawnCentre[k][i] - c).MagnitudeSqr() < r*r)
				return true;
	}
	return false;
}

struct PS3ModelPtr { CBaseModelInfo *mi; int32 id; };
static int
PS3_CmpModelPtr(const void *a, const void *b)
{
	uintptr pa = (uintptr)((const PS3ModelPtr*)a)->mi, pb = (uintptr)((const PS3ModelPtr*)b)->mi;
	return pa < pb ? -1 : pa > pb ? 1 : 0;
}

// once, after the IDEs (the first frame drawn): which LOD goes with which model
static void
PS3_BuildLodPairs(void)
{
	static PS3ModelPtr ptrs[MODELINFOSIZE];
	int32 n = 0, pairs = 0;
	ps3LodPairsBuilt = true;
	for(int32 i = 0; i < MODELINFOSIZE; i++){
		ps3LodHigh[i] = -1;
		ps3HasLod[i] = 0;
		CBaseModelInfo *mi = CModelInfo::GetModelInfo(i);
		if(mi && mi->IsSimple()){
			ptrs[n].mi = mi;
			ptrs[n].id = i;
			n++;
		}
	}
	qsort(ptrs, n, sizeof(ptrs[0]), PS3_CmpModelPtr);
	for(int32 i = 0; i < n; i++){
		CSimpleModelInfo *low = (CSimpleModelInfo*)ptrs[i].mi;
		if(!low->m_isBigBuilding || low->GetRelatedModel() == nil)
			continue;
		PS3ModelPtr key = { low->GetRelatedModel(), 0 };
		PS3ModelPtr *high = (PS3ModelPtr*)bsearch(&key, ptrs, n, sizeof(ptrs[0]), PS3_CmpModelPtr);
		if(high == nil || ((CSimpleModelInfo*)high->mi)->m_isBigBuilding)
			continue;
		ps3LodHigh[ptrs[i].id] = high->id;
		ps3HasLod[high->id] = 1;
		pairs++;
	}
	PS3_Logf("[lod] %d LOD models paired with their detailed model (%s)", pairs,
	         gPS3LodPairs ? "a LOD hides only for a detailed instance drawn at its place" : "OFF: nolodpairs.txt");
}

// the sector scan accepted this detailed instance (faded in): it is drawn
static void
PS3_MarkHighDrawn(CEntity *ent)
{
	int32 id = ent->GetModelIndex();
	if(ent->IsBuilding() && !ent->bIsBIGBuilding)
		PS3_NoteDrawn(ent);	// patch 124
	if(id < 0 || id >= MODELINFOSIZE || !ps3HasLod[id])
		return;
	ps3NumDrawnHigh++;
	PS3VisibleHigh &v = ps3VisHigh[id];
	if(v.frame != ps3VisFrame){
		v.frame = ps3VisFrame;
		v.n = 0;
	}
	if(v.n < 4)
		v.ent[v.n] = ent;
	v.n++;
}

// What the LOD buildings did on the last frame, for "[lod]" every 10 s
// (patch 78: and why the ones that hid for their detailed model did)
enum {
	PS3BB_VISIBLE, PS3BB_FADING, PS3BB_DRAWLAST, PS3BB_HIGH_DRAWN, PS3BB_KEPT,
	PS3BB_OFFSCREEN, PS3BB_OCCLUDED, PS3BB_HIDDEN, PS3BB_TOOFAR, PS3BB_STREAM,
	PS3BB_TIME, PS3BB_AREA, PS3BB_PLANE,
	PS3HI_VISIBLE, PS3HI_FADING, PS3HI_OFFSCREEN, PS3HI_OCCLUDED, PS3HI_NOTLOADED,
	PS3BB_HIDE_UNPAIRED, PS3BB_HIDE_MANY,
	PS3BB_NUM
};
static int32 ps3BB[2][PS3BB_NUM];	// [0] this frame, [1] the last one
#define PS3_BB(r) (ps3BB[0][r]++)

// may this LOD hide? (its detailed model is loaded and faded in)
static bool
PS3_LodMayHide(CEntity *lod)
{
	if(!gPS3LodPairs || !ps3LodPairsBuilt)
		return true;
	int32 id = lod->GetModelIndex();
	if(id < 0 || id >= MODELINFOSIZE || ps3LodHigh[id] < 0){
		ps3BB[0][PS3BB_HIDE_UNPAIRED]++;	// patch 78
		return true;	// not paired: the game's rule
	}
	PS3VisibleHigh &v = ps3VisHigh[ps3LodHigh[id]];
	if(v.frame != ps3VisFrame || v.n == 0)
		return PS3_HighDrawnInside(lod);
	if(v.n > 4){
		ps3BB[0][PS3BB_HIDE_MANY]++;	// patch 78
		return true;	// many instances of the model: as the game
	}
	for(int32 i = 0; i < v.n; i++){
		CEntity *e = v.ent[i];
		// LOD and detailed instance often have different pivots: their
		// bounds touch, or they stand within 2 m of each other
		CVector d = e->GetPosition() - lod->GetPosition();
		if(d.x*d.x + d.y*d.y < 4.0f || lod->GetIsTouching(e))
			return true;
	}
	return PS3_HighDrawnInside(lod);
}

bool gPS3NoOcclusion;	// USRDIR/noocclusion.txt (Occlusion.cpp)

// ---- patch 78: buildings black in some weathers (diagnostic) ----------------------
//
// Ren: in some weathers some buildings go completely black; his rain video
// has a flat dark shape where a block of buildings later pops in. What a
// building is drawn with depends on the weather only through the time cycle
// (ambient, far clip, fog), so "[weather]" says what the time cycle gives
// right now, and "[dark]" names the buildings drawn in the last 10 s that
// can only come out black: prelight (nearly) black, no prelight and no
// normals or lighting, or a textured mesh without its texture.

enum Visbility
{
	VIS_INVISIBLE,
	VIS_VISIBLE,
	VIS_OFFSCREEN,
	VIS_STREAMME
};

// ---- patch 91/92: the road shadows of Staunton that come and go -------------
//
// "[shad]" says, per frame, what happened to the shadow buildings (model
// name with "shad" in it: NewShad*, pm_treeshad_*, ...) within 80 m of the
// camera: drawn, not scanned at all (their sector outside the scan), off
// screen, occluded (COcclusion), invisible, not loaded, or dropped because
// the sorted alpha list was full (patch 92). Patch 91 looked for NewShad*
// only, and none of those was near: Ren's are pm_treeshad_* ([dark]).
#define PS3_MAX_SHAD 512
static CEntity *ps3Shad[PS3_MAX_SHAD];
static int32 ps3NumShad = -1;	// -1: not looked for yet
static uint8 ps3IsShadModel[MODELINFOSIZE];
static uint32 ps3ShadFrame[PS3_MAX_SHAD];
static uint8 ps3ShadWhy[PS3_MAX_SHAD];
enum { PS3SH_DRAWN, PS3SH_NOTSCANNED, PS3SH_OFFSCREEN, PS3SH_OCCLUDED, PS3SH_INVISIBLE, PS3SH_NOTLOADED, PS3SH_ALPHALOST, PS3SH_NUM };
extern int32 gPS3AlphaBldUsed, gPS3AlphaBldPeak, gPS3AlphaBldLost, gPS3AlphaEntUsed, gPS3AlphaEntPeak, gPS3AlphaEntLost;	// VisibilityPlugins.cpp
extern CEntity *gPS3AlphaLastLost;
static uint32 ps3AlphaFrames, ps3AlphaFullFrames, ps3AlphaLastBldLost;

static bool
PS3_IsShadName(const char *n)
{
	for(; *n; n++)
		if(strncasecmp(n, "shad", 4) == 0)
			return true;
	return false;
}
static uint32 ps3ShadCount[PS3SH_NUM], ps3ShadFrames;

static void
PS3_ShadFind(void)
{
	ps3NumShad = 0;
	for(int32 i = 0; i < MODELINFOSIZE; i++){
		CBaseModelInfo *mi = CModelInfo::GetModelInfo(i);
		ps3IsShadModel[i] = mi && PS3_IsShadName(mi->GetModelName());
	}
	CBuildingPool *pool = CPools::GetBuildingPool();
	for(int32 i = 0; pool && i < pool->GetSize() && ps3NumShad < PS3_MAX_SHAD; i++){
		CBuilding *b = pool->GetSlot(i);
		if(b && ps3IsShadModel[b->GetModelIndex()])
			ps3Shad[ps3NumShad++] = b;
	}
	int32 nm = 0;
	for(int32 i = 0; i < MODELINFOSIZE; i++)
		nm += ps3IsShadModel[i];
	PS3_Logf("[shad] %d shadow buildings (%d models with \"shad\" in the name)", ps3NumShad, nm);
}

// patch 93: CEntity::Render marks the decal draws with it
bool
PS3_IsShadModel(int32 id)
{
	return ps3NumShad > 0 && id >= 0 && id < MODELINFOSIZE && ps3IsShadModel[id];
}
uint32 gPS3ShadRendered;	// shadow entities through CEntity::Render (Entity.cpp)
static uint32 ps3ShadRenderedSum, ps3ShadNearSum, ps3ShadVisSum, ps3ShadSumFrames;
static CEntity *ps3ShadInfoDone[24];
static int32 ps3NumShadInfo;
static float ps3ShadNearestDist;
static CEntity *ps3ShadNearest;

// once per shadow entity that comes near: what it is and how high above the road
static void
PS3_ShadInfo(CEntity *e)
{
	for(int32 i = 0; i < ps3NumShadInfo; i++)
		if(ps3ShadInfoDone[i] == e)
			return;
	if(ps3NumShadInfo >= 24 || e->m_rwObject == nil || RwObjectGetType(e->m_rwObject) != rpATOMIC)
		return;
	ps3ShadInfoDone[ps3NumShadInfo++] = e;
	CSimpleModelInfo *mi = (CSimpleModelInfo*)CModelInfo::GetModelInfo(e->GetModelIndex());
	RpAtomic *a = (RpAtomic*)e->m_rwObject;
	RpGeometry *g = RpAtomicGetGeometry(a);
	float minZ = 1.0e9f, maxZ = -1.0e9f;
	int32 minA = 256, maxA = -1;
	if(g && g->morphTargets && g->morphTargets[0].vertices){
		const CMatrix &m = e->GetMatrix();
		for(int32 v = 0; v < g->numVertices; v++){
			rw::V3d *p = &g->morphTargets[0].vertices[v];
			CVector w = m * CVector(p->x, p->y, p->z);
			minZ = Min(minZ, w.z);
			maxZ = Max(maxZ, w.z);
		}
		if(g->colors)
			for(int32 v = 0; v < g->numVertices; v++){
				minA = Min(minA, (int32)g->colors[v].alpha);
				maxA = Max(maxA, (int32)g->colors[v].alpha);
			}
	}
	CColPoint cp;
	CEntity *hit = nil;
	CVector top = e->GetPosition();
	top.z = maxZ + 3.0f;
	float ground = -1000.0f;
	const char *hitName = "nothing";
	if(CWorld::ProcessVerticalLine(top, minZ - 10.0f, cp, hit, true, false, false, false, true, false, nil)){
		ground = cp.point.z;
		if(hit)
			hitName = CModelInfo::GetModelInfo(hit->GetModelIndex())->GetModelName();
	}
	int32 meshes = 0;
	char tex[96] = "";
	if(g){
		meshes = g->matList.numMaterials;
		for(int32 k = 0; k < meshes && k < 3; k++){
			rw::Material *mat = g->matList.materials[k];
			char t[40];
			snprintf(t, sizeof(t), "%s%s(a%d)", k ? "," : "", mat->texture ? mat->texture->name : "none", mat->color.alpha);
			strncat(tex, t, sizeof(tex) - strlen(tex) - 1);
		}
	}
	PS3_Logf("[shadm] %s at %.1f %.1f %.1f: z %.2f..%.2f, ground under it %.2f (%s), so %.1f cm above; %d verts, %d materials %s, vertex alpha %d..%d; "
	         "flags drawLast %d noZwrite %d additive %d noFade %d ignoreLight %d bigBuilding %d, entity drawLast %d, lod dist %.0f",
	         mi->GetModelName(), e->GetPosition().x, e->GetPosition().y, e->GetPosition().z, minZ, maxZ, ground, hitName,
	         (minZ - ground)*100.0f, g ? g->numVertices : 0, meshes, tex, minA, maxA,
	         (int)mi->m_drawLast, (int)mi->m_noZwrite, (int)mi->m_additive, (int)mi->m_noFade, (int)mi->m_ignoreLight, (int)mi->m_isBigBuilding,
	         (int)e->bDrawLast, mi->GetLargestLodDistance());
}

// after SetupEntityVisibility in the sector scans
static void
PS3_ShadNoteVis(CEntity *ent, int vis)
{
	if(ps3NumShad <= 0 || !ps3IsShadModel[ent->GetModelIndex()])
		return;
	for(int32 i = 0; i < ps3NumShad; i++)
		if(ps3Shad[i] == ent){
			ps3ShadFrame[i] = ps3VisFrame;
			if(gPS3AlphaLastLost == ent){
				ps3ShadWhy[i] = PS3SH_ALPHALOST;
				gPS3AlphaLastLost = nil;
			}else if(vis == VIS_VISIBLE)
				ps3ShadWhy[i] = PS3SH_DRAWN;
			else if(vis == VIS_STREAMME)
				ps3ShadWhy[i] = PS3SH_NOTLOADED;
			else if(vis == VIS_OFFSCREEN)
				// VIS_OFFSCREEN is also what a fading entity returns (drawn from the sorted list)
				ps3ShadWhy[i] = !ent->GetIsOnScreen() ? PS3SH_OFFSCREEN :
				                ent->IsEntityOccluded() ? PS3SH_OCCLUDED : PS3SH_DRAWN;
			else
				ps3ShadWhy[i] = ent->m_rwObject == nil ? PS3SH_NOTLOADED : PS3SH_INVISIBLE;
			return;
		}
}

// the frame that just ended (ClearForFrame)
static void
PS3_ShadEndFrame(void)
{
	if(ps3NumShad < 0){
		if(CPools::GetBuildingPool() && CPools::GetBuildingPool()->GetNoOfUsedSpaces() > 0)
			PS3_ShadFind();	// patch 93: also without logs (the test modes need it)
		return;
	}
	if(!PS3_logsOn){
		gPS3ShadRendered = 0;
		return;
	}
	ps3AlphaFrames++;
	if(gPS3AlphaBldLost != ps3AlphaLastBldLost)
		ps3AlphaFullFrames++;
	ps3AlphaLastBldLost = gPS3AlphaBldLost;
	CVector cam = TheCamera.GetPosition();
	bool any = false;
	uint32 nearN = 0, visN = 0;
	ps3ShadNearest = nil;
	ps3ShadNearestDist = 1.0e9f;
	for(int32 i = 0; i < ps3NumShad; i++){
		float d = (ps3Shad[i]->GetPosition() - cam).Magnitude2D();
		if(d > 80.0f)
			continue;
		any = true;
		nearN++;
		int why = ps3ShadFrame[i] == ps3VisFrame ? ps3ShadWhy[i] : PS3SH_NOTSCANNED;
		if(why == PS3SH_DRAWN) visN++;
		ps3ShadCount[why]++;
		if(d < ps3ShadNearestDist){
			ps3ShadNearestDist = d;
			ps3ShadNearest = ps3Shad[i];
		}
		PS3_ShadInfo(ps3Shad[i]);
	}
	if(any)
		ps3ShadFrames++;
	ps3ShadNearSum += nearN;
	ps3ShadVisSum += visN;
	ps3ShadRenderedSum += gPS3ShadRendered;
	ps3ShadSumFrames++;
	gPS3ShadRendered = 0;
}

static void PS3_FarReport(void);	// patch 93, below
static void PS3_DecalSnapshot(const char *why);

static void
PS3_ShadReport(void)
{
	if(ps3AlphaFrames){
		PS3_Logf("[alpha] sorted lists in %u frames: buildings peak %d of %d, full in %u frames, %.1f dropped per frame | others peak %d of %d, %.1f dropped per frame",
		         ps3AlphaFrames, gPS3AlphaBldPeak, NUMALPHAENTITYLIST*4, ps3AlphaFullFrames, gPS3AlphaBldLost/(float)ps3AlphaFrames,
		         gPS3AlphaEntPeak, NUMALPHAENTITYLIST*3, gPS3AlphaEntLost/(float)ps3AlphaFrames);
		gPS3AlphaBldPeak = gPS3AlphaEntPeak = gPS3AlphaBldLost = gPS3AlphaEntLost = 0;
		ps3AlphaLastBldLost = 0;
		ps3AlphaFrames = ps3AlphaFullFrames = 0;
	}
	PS3_FarReport();
	if(ps3ShadSumFrames && ps3ShadNearSum)
		PS3_DecalSnapshot("every 10 s");
	if(ps3ShadFrames == 0)
		return;
	float f = (float)ps3ShadFrames;
	PS3_Logf("[shad] shadow buildings within 80 m, per frame (%u frames): drawn %.1f, not scanned %.1f, off screen %.1f, occluded %.1f, invisible %.1f, not loaded %.1f, alpha list full %.1f",
	         ps3ShadFrames, ps3ShadCount[PS3SH_DRAWN]/f, ps3ShadCount[PS3SH_NOTSCANNED]/f, ps3ShadCount[PS3SH_OFFSCREEN]/f,
	         ps3ShadCount[PS3SH_OCCLUDED]/f, ps3ShadCount[PS3SH_INVISIBLE]/f, ps3ShadCount[PS3SH_NOTLOADED]/f, ps3ShadCount[PS3SH_ALPHALOST]/f);
	memset(ps3ShadCount, 0, sizeof(ps3ShadCount));
	ps3ShadFrames = 0;
}

// ---- patch 95: LODs (big buildings) are kept until the fog hides them --------
// [far]: with the fog starting behind the camera, LODs ended at their draw
// distance with 40-80% fog (LODebarrier_gaz2 at 545 m, LODroad_se_12 at
// 1090 m, the tunnel LODs at 422 m): the city far away appeared and went
// in steps. Their draw distance is stretched until the fog there is 97%
// (at most x3), the same scale for the cut, the streaming request and the
// fade. USRDIR/nolodfog.txt turns it off.
static int ps3LodFogOn = -1;

float
PS3_LodFogScale(CSimpleModelInfo *mi)
{
	if(ps3LodFogOn < 0){
		FILE *f = fopen(PS3_USRDIR "/nolodfog.txt", "rb");
		ps3LodFogOn = f == nil;
		if(f)
			fclose(f);
		PS3_Logf("[far] LODs kept until the fog hides them: %s", ps3LodFogOn ? "on (nolodfog.txt turns it off)" : "off (nolodfog.txt)");
	}
	if(!ps3LodFogOn || Scene.camera == nil || mi == nil || !mi->m_isBigBuilding)
		return 1.0f;
	float fogStart = Scene.camera->fogPlane, farClip = Scene.camera->farPlane;
	float reach = fogStart + 0.97f*(farClip - fogStart);	// 97% fog there
	float lodEnd = mi->GetLodDistance(0) + FADE_DISTANCE;
	if(lodEnd <= 0.0f || reach <= lodEnd)
		return 1.0f;
	return Min(reach/lodEnd, 3.0f);
}

// ---- patch 94: "[near]", every building model drawn within 60 m, once: its
// materials (texture, material alpha, texture alpha). The road shadows of
// Staunton aren't pm_treeshad_* (that one was behind the camera): this
// says what they are.
static uint8 ps3NearDone[MODELINFOSIZE];
static int32 ps3NumNear;

void
PS3_NearInventory(CEntity *e)
{
	if(!PS3_logsOn || ps3NumNear >= 400 || !e->IsBuilding())
		return;
	int32 id = e->GetModelIndex();
	if(id < 0 || id >= MODELINFOSIZE || ps3NearDone[id])
		return;
	if(e->m_rwObject == nil || RwObjectGetType(e->m_rwObject) != rpATOMIC)
		return;
	if((e->GetPosition() - TheCamera.GetPosition()).MagnitudeSqr2D() > 60.0f*60.0f)
		return;
	ps3NearDone[id] = 1;
	ps3NumNear++;
	RpGeometry *g = RpAtomicGetGeometry((RpAtomic*)e->m_rwObject);
	if(g == nil)
		return;
	char buf[400] = "";
	for(int32 k = 0; k < g->matList.numMaterials && k < 8; k++){
		rw::Material *mat = g->matList.materials[k];
		char t[64];
		bool ta = mat->texture && mat->texture->raster && GETPS3RASTEREXT(mat->texture->raster)->hasAlpha;
		snprintf(t, sizeof(t), "%s%s(a%d%s)", k ? ", " : "", mat->texture ? mat->texture->name : "none", mat->color.alpha, ta ? " tex-alpha" : "");
		strncat(buf, t, sizeof(buf) - strlen(buf) - 1);
	}
	CVector p = e->GetPosition();
	CSimpleModelInfo *mi = (CSimpleModelInfo*)CModelInfo::GetModelInfo(id);
	// patch 95: the average prelight of each mesh (a shadow overlay is dark)
	if(g->colors && g->meshHeader && mi->m_wetRoadReflection){
		char mb[400] = "";
		rw::Mesh *mesh = g->meshHeader->getMeshes();
		for(uint32 k = 0; k < g->meshHeader->numMeshes && k < 8; k++, mesh++){
			uint32 r = 0, gg = 0, b = 0, al = 0, n = 0;
			for(uint32 j = 0; j < mesh->numIndices; j++){
				rw::RGBA &c = g->colors[mesh->indices[j]];
				r += c.red; gg += c.green; b += c.blue; al += c.alpha; n++;
			}
			if(n == 0) n = 1;
			char t[64];
			snprintf(t, sizeof(t), "%s%s %u/%u/%u a%u", k ? ", " : "",
			         mesh->material && mesh->material->texture ? mesh->material->texture->name : "none", r/n, gg/n, b/n, al/n);
			strncat(mb, t, sizeof(mb) - strlen(mb) - 1);
		}
		PS3_Logf("[near]   %s meshes, average prelight: %s", mi->GetModelName(), mb);
	}
	PS3_Logf("[near] %s at %.0f %.0f %.1f, %d m away: %d materials: %s%s%s%s", mi->GetModelName(), p.x, p.y, p.z,
	         (int)(e->GetPosition() - TheCamera.GetPosition()).Magnitude2D(), g->matList.numMaterials, buf,
	         mi->m_wetRoadReflection ? " | road" : "", mi->m_drawLast ? " | drawLast" : "", mi->m_noZwrite ? " | noZwrite" : "");
}

// ---- patch 93: "[far]", big buildings (LODs) cut at their draw distance where
// the fog doesn't hide them yet: what pops in and out far away
static uint32 ps3FarCut, ps3FarCutFrames;
static float ps3FarWorstFog = 2.0f, ps3FarWorstDist, ps3FarWorstLod;
static int32 ps3FarWorstModel = -1;
static uint32 ps3FarLastFrame;
// patch 94: by model, the most cut ones
struct PS3FarModel { int32 model; uint32 count; float minFog, dist; };
static PS3FarModel ps3FarModels[48];
static int32 ps3NumFarModels;

static void
PS3_FarNoteCut(CEntity *ent, float dist)
{
	if(!PS3_logsOn || Scene.camera == nil || !ent->GetIsOnScreen())
		return;
	if(strncasecmp(CModelInfo::GetModelInfo(ent->GetModelIndex())->GetModelName(), "IslandLOD", 9) == 0)
		return;	// patch 95: the other islands' LODs, deleted on purpose for this level
	float fogStart = Scene.camera->fogPlane, farClip = Scene.camera->farPlane;
	float fog = farClip > fogStart ? (dist - fogStart)/(farClip - fogStart) : 1.0f;	// 1: all fog
	if(fog >= 0.9f)
		return;
	ps3FarCut++;
	{
		int32 id = ent->GetModelIndex(), k;
		for(k = 0; k < ps3NumFarModels; k++)
			if(ps3FarModels[k].model == id)
				break;
		if(k == ps3NumFarModels && k < 48){
			ps3NumFarModels++;
			ps3FarModels[k].model = id;
			ps3FarModels[k].count = 0;
			ps3FarModels[k].minFog = 2.0f;
		}
		if(k < 48){
			ps3FarModels[k].count++;
			if(fog < ps3FarModels[k].minFog){
				ps3FarModels[k].minFog = fog;
				ps3FarModels[k].dist = dist;
			}
		}
	}
	if(ps3FarLastFrame != ps3VisFrame){
		ps3FarLastFrame = ps3VisFrame;
		ps3FarCutFrames++;
	}
	if(fog < ps3FarWorstFog){
		ps3FarWorstFog = fog;
		ps3FarWorstDist = dist;
		ps3FarWorstModel = ent->GetModelIndex();
		ps3FarWorstLod = ((CSimpleModelInfo*)CModelInfo::GetModelInfo(ent->GetModelIndex()))->GetLargestLodDistance();
	}
}

static void
PS3_FarReport(void)
{
	if(ps3FarCut == 0 || Scene.camera == nil)
		return;
	PS3_Logf("[far] LODs cut on screen before the fog covers them: %u in 10 s (%u frames); worst %s at %.0f m (its draw distance %.0f), %.0f%% fog there; far clip %.0f, fog from %.0f, weather %d -> %d (%.2f)",
	         ps3FarCut, ps3FarCutFrames, ps3FarWorstModel >= 0 ? CModelInfo::GetModelInfo(ps3FarWorstModel)->GetModelName() : "-",
	         ps3FarWorstDist, ps3FarWorstLod, ps3FarWorstFog*100.0f, Scene.camera->farPlane, Scene.camera->fogPlane,
	         CWeather::OldWeatherType, CWeather::NewWeatherType, CWeather::InterpolationValue);
	// the 6 most cut, with their first LOD distance (the cut is there)
	for(int32 n = 0; n < 6; n++){
		int32 best = -1;
		for(int32 k = 0; k < ps3NumFarModels; k++)
			if(ps3FarModels[k].count && (best < 0 || ps3FarModels[k].count > ps3FarModels[best].count))
				best = k;
		if(best < 0)
			break;
		PS3FarModel &f = ps3FarModels[best];
		CSimpleModelInfo *mi = (CSimpleModelInfo*)CModelInfo::GetModelInfo(f.model);
		PS3_Logf("[far]   %s x%u: cut from %.0f m (first LOD distance %.0f, largest %.0f, LOD x%.2f), %.0f%% fog at %.0f m",
		         mi->GetModelName(), f.count, mi->GetLodDistance(0) + FADE_DISTANCE,
		         mi->GetLodDistance(0), mi->GetLargestLodDistance(), TheCamera.LODDistMultiplier,
		         f.minFog*100.0f, f.dist);
		f.count = 0;
	}
	ps3NumFarModels = 0;
	ps3FarCut = ps3FarCutFrames = 0;
	ps3FarWorstFog = 2.0f;
	ps3FarWorstModel = -1;
}

// ---- patch 96: LODs drawn near the camera (their detailed model wasn't) ------
struct PS3NearLod { int32 model; uint32 frames; float dist; };
static PS3NearLod ps3NearLods[16];
static int32 ps3NumNearLods;

static void
PS3_NoteNearLod(CEntity *ent, float dist)
{
	if(!PS3_logsOn || dist > 80.0f)
		return;
	int32 id = ent->GetModelIndex(), k;
	for(k = 0; k < ps3NumNearLods; k++)
		if(ps3NearLods[k].model == id)
			break;
	if(k == ps3NumNearLods){
		if(k == 16)
			return;
		ps3NumNearLods++;
		ps3NearLods[k].model = id;
		ps3NearLods[k].frames = 0;
		ps3NearLods[k].dist = dist;
	}
	ps3NearLods[k].frames++;
	ps3NearLods[k].dist = Min(ps3NearLods[k].dist, dist);
}

static void
PS3_NearLodReport(void)
{
	if(ps3NumNearLods == 0){
		PS3_Logf("[snap]   no LOD drawn within 80 m");
		return;
	}
	char buf[600] = "";
	for(int32 k = 0; k < ps3NumNearLods; k++){
		CSimpleModelInfo *mi = (CSimpleModelInfo*)CModelInfo::GetModelInfo(ps3NearLods[k].model);
		CSimpleModelInfo *hi = mi->GetRelatedModel();
		char t[96];
		snprintf(t, sizeof(t), "%s%s x%u from %.0f m (detailed %s%s)", k ? ", " : "", mi->GetModelName(),
		         ps3NearLods[k].frames, ps3NearLods[k].dist, hi ? hi->GetModelName() : "none",
		         hi ? (hi->GetRwObject() ? " loaded" : " NOT loaded") : "");
		strncat(buf, t, sizeof(buf) - strlen(buf) - 1);
	}
	PS3_Logf("[snap]   LODs drawn within 80 m: %s", buf);
	ps3NumNearLods = 0;
}

// ---- patch 93: the decal test modes (R3 + Select) and the snapshot ---------------
static const char *const ps3DecalModeName[] = {
	"0 normal", "1 depth bias", "2 no depth test", "3 hide ground overlays", "4 hide shad",
	"5 opaque (no blend)", "6 hide every 2nd+ mesh", "7 hide near LODs",
	"8 hide cars/peds shadows", "9 hide pools and pole shadows", "10 hide player vehicle lights",
	"11 hide skidmarks, rubbish, glass", "12 hide coronas, lights, particles, fx",
	"13 hide the aimed model", "14 hide the rest at the aimed point",
	"15 hide the aimed mesh",
	"16 free camera (the view frozen)",
	"17 log what is drawn, loaded, removed",
	"18 hide every LOD (big building)"
};
#define PS3_NUMTESTMODES 19
static uint32 ps3DecalPrev;
static wchar ps3DecalMsg[96];

static void
PS3_DecalSnapshot(const char *why)
{
	using namespace rw::ps3;
	float f = ps3ShadSumFrames ? (float)ps3ShadSumFrames : 1.0f;
	CVector cam = TheCamera.GetPosition();
	CVector fwd = TheCamera.GetForward();
	PS3_Logf("[snap] %s | mode %s, %u frames: shadow buildings within 80 m %.1f, visible %.1f, rendered %.1f per frame | camera %.1f %.1f %.1f looking %.2f %.2f %.2f, near %.2f far %.0f fog from %.0f",
	         why, ps3DecalModeName[decalMode], ps3ShadSumFrames, ps3ShadNearSum/f, ps3ShadVisSum/f, ps3ShadRenderedSum/f,
	         cam.x, cam.y, cam.z, fwd.x, fwd.y, fwd.z,
	         Scene.camera ? Scene.camera->nearPlane : 0.0f, Scene.camera ? Scene.camera->farPlane : 0.0f, Scene.camera ? Scene.camera->fogPlane : 0.0f);
	if(ps3ShadNearest){
		CVector p = ps3ShadNearest->GetPosition();
		PS3_Logf("[snap]   nearest: %s at %.1f m (%.1f %.1f %.1f)",
		         CModelInfo::GetModelInfo(ps3ShadNearest->GetModelIndex())->GetModelName(), ps3ShadNearestDist, p.x, p.y, p.z);
	}
	DecalStats &d = decalStats;
	if(d.meshes)
		PS3_Logf("[snap]   drawn with (of %u meshes): textured %u (texture with alpha %u, first %s), vertex alpha %u, blend %u, z test %u, z write %u, fog %u, GS alpha test %u, no culling %u | last: blend %d/%d, alpha test %d ref %d, GS ref %d, material alpha %d",
		         d.meshes, d.textured, d.texAlpha, d.tex[0] ? d.tex : "-", d.vertexAlpha, d.blendOn, d.ztestOn, d.zwriteOn, d.fogOn, d.gsAlphaTest, d.cullNone,
		         d.srcBlend, d.dstBlend, d.alphaFunc, d.alphaRef, d.gsAlphaRef, d.matAlpha);
	else
		PS3_Logf("[snap]   no shadow mesh reached the RSX");
	PS3_NearLodReport();
	{
		extern void PS3_ShadowSnapshot(void);	// Shadows.cpp (patch 109)
		PS3_ShadowSnapshot();
	}
	memset(&decalStats, 0, sizeof(decalStats));
	ps3ShadNearSum = ps3ShadVisSum = ps3ShadRenderedSum = ps3ShadSumFrames = 0;
}

// ---- patch 110: the "broken shadow" is none of CShadows (modes 8, 9). Modes 13
// and 14 aim at the screen centre: 13 hides the model the camera ray hits
// (the HUD names it), 14 hides every other model whose bounds hold that point
// (overlays without collision). The [aim] log lists them all, with the
// prelight of the hit triangle.
static CEntity *ps3AimEnt;
static CEntity *ps3AimOthers[16];
static int ps3AimNumOthers;
static uint32 ps3AimFrame;
static wchar ps3AimMsg[200];

int
PS3_TestMode(void)
{
	return PS3_logsOn ? rw::ps3::decalMode : 0;
}

bool
PS3_AimHidden(CEntity *e)
{
	int m = rw::ps3::decalMode;
	if(!PS3_logsOn || (m != 13 && m != 14 && m != 16) || e == nil)
		return false;
	if(m == 13)
		return e == ps3AimEnt;
	if(m == 16){
		extern CEntity *ps3FreeHideEnt;
		return e == ps3FreeHideEnt;
	}
	for(int i = 0; i < ps3AimNumOthers; i++)
		if(ps3AimOthers[i] == e)
			return true;
	return false;
}

static const char *
PS3_AimFlags(CEntity *e)
{
	static char f[96];
	f[0] = 0;
	CBaseModelInfo *bmi = CModelInfo::GetModelInfo(e->GetModelIndex());
	if(bmi && (bmi->GetModelType() == MITYPE_SIMPLE || bmi->GetModelType() == MITYPE_TIME)){
		CSimpleModelInfo *mi = (CSimpleModelInfo*)bmi;
		if(mi->m_drawLast) strcat(f, " drawLast");
		if(mi->m_additive) strcat(f, " additive");
		if(mi->m_noZwrite) strcat(f, " noZwrite");
		if(mi->m_wetRoadReflection) strcat(f, " road");
		if(mi->GetLodDistance(0) > 300.0f) strcat(f, " big");
	}
	if(e->bIsBIGBuilding) strcat(f, " BIG");
	if(e->m_rwObject == nil) strcat(f, " no-rw");
	if(!e->bIsVisible) strcat(f, " invisible");
	if(!e->bUsesCollision) strcat(f, " no-col");
	return f;
}

// the triangle of e under the world point p (2D in model space), its prelight
static void
PS3_AimTriangle(CEntity *e, const CVector &p)
{
	if(e->m_rwObject == nil || RwObjectGetType(e->m_rwObject) != rpATOMIC)
		return;
	RpGeometry *g = RpAtomicGetGeometry((RpAtomic*)e->m_rwObject);
	if(g == nil || g->triangles == nil || g->numTriangles == 0 || g->morphTargets == nil || g->morphTargets[0].vertices == nil){
		PS3_Logf("[aim]   (no triangles kept for %s)", CModelInfo::GetModelInfo(e->GetModelIndex())->GetModelName());
		return;
	}
	CVector lp = Invert(e->GetMatrix()) * p;
	rw::V3d *v = g->morphTargets[0].vertices;
	int found = 0;
	for(int32 t = 0; t < g->numTriangles && found < 3; t++){
		rw::Triangle *tr = &g->triangles[t];
		rw::V3d a = v[tr->v[0]], b = v[tr->v[1]], c = v[tr->v[2]];
		float d = (b.y - c.y)*(a.x - c.x) + (c.x - b.x)*(a.y - c.y);
		if(d > -1e-6f && d < 1e-6f)
			continue;	// vertical
		float l0 = ((b.y - c.y)*(lp.x - c.x) + (c.x - b.x)*(lp.y - c.y)) / d;
		float l1 = ((c.y - a.y)*(lp.x - c.x) + (a.x - c.x)*(lp.y - c.y)) / d;
		float l2 = 1.0f - l0 - l1;
		if(l0 < 0.0f || l1 < 0.0f || l2 < 0.0f)
			continue;
		float z = l0*a.z + l1*b.z + l2*c.z;
		if(z - lp.z > 1.5f || lp.z - z > 1.5f)
			continue;	// another floor of the model
		rw::Material *mat = tr->matId < g->matList.numMaterials ? g->matList.materials[tr->matId] : nil;
		char col[96] = "no prelight";
		if(g->colors){
			rw::RGBA *k = g->colors;
			snprintf(col, sizeof(col), "prelight %d %d %d %d / %d %d %d %d / %d %d %d %d",
			         k[tr->v[0]].red, k[tr->v[0]].green, k[tr->v[0]].blue, k[tr->v[0]].alpha,
			         k[tr->v[1]].red, k[tr->v[1]].green, k[tr->v[1]].blue, k[tr->v[1]].alpha,
			         k[tr->v[2]].red, k[tr->v[2]].green, k[tr->v[2]].blue, k[tr->v[2]].alpha);
		}
		PS3_Logf("[aim]   triangle %d (z %.2f, model z %.2f): material %d %s (colour %d %d %d %d), %s",
		         t, z, lp.z, tr->matId, mat && mat->texture ? mat->texture->name : "no texture",
		         mat ? mat->color.red : 0, mat ? mat->color.green : 0, mat ? mat->color.blue : 0, mat ? mat->color.alpha : 0, col);
		found++;
	}
	if(!found)
		PS3_Logf("[aim]   no triangle of the model under the point");
}

// patch 114: what is drawn under the screen centre: the ray against the
// models' own triangles (not their collision), the nearest of the hit model and
// those around the point. Vertical walls, windows and overlays included
static rw::Material *ps3AimMat;
static CEntity *ps3AimMatEnt;

static bool
PS3_RayTriangles(CEntity *e, const CVector &cam, const CVector &fwd, float *tBest, int *triBest)
{
	if(e->m_rwObject == nil || RwObjectGetType(e->m_rwObject) != rpATOMIC)
		return false;
	RpGeometry *g = RpAtomicGetGeometry((RpAtomic*)e->m_rwObject);
	if(g == nil || g->triangles == nil || g->morphTargets == nil || g->morphTargets[0].vertices == nil)
		return false;
	CMatrix inv = Invert(e->GetMatrix());
	CVector o = inv * cam;
	CVector d = Multiply3x3(inv, fwd);
	rw::V3d *v = g->morphTargets[0].vertices;
	bool found = false;
	for(int32 t = 0; t < g->numTriangles; t++){
		rw::Triangle *tr = &g->triangles[t];
		CVector a(v[tr->v[0]].x, v[tr->v[0]].y, v[tr->v[0]].z);
		CVector e1 = CVector(v[tr->v[1]].x, v[tr->v[1]].y, v[tr->v[1]].z) - a;
		CVector e2 = CVector(v[tr->v[2]].x, v[tr->v[2]].y, v[tr->v[2]].z) - a;
		CVector p = CrossProduct(d, e2);
		float det = DotProduct(e1, p);
		if(det > -1e-7f && det < 1e-7f)
			continue;
		float inv_det = 1.0f/det;
		CVector s = o - a;
		float u = DotProduct(s, p)*inv_det;
		if(u < 0.0f || u > 1.0f) continue;
		CVector q = CrossProduct(s, e1);
		float w = DotProduct(d, q)*inv_det;
		if(w < 0.0f || u + w > 1.0f) continue;
		float tt = DotProduct(e2, q)*inv_det;
		if(tt > 0.05f && tt < *tBest){
			*tBest = tt;
			*triBest = t;
			found = true;
		}
	}
	return found;
}

static void
PS3_AimRender(CEntity *hit, CEntity **others, int n, const CVector &cam, const CVector &fwd, bool log = true)
{
	float tBest = 300.0f;
	int triBest = -1;
	CEntity *eBest = nil;
	CEntity *cand[17];
	int nc = 0;
	cand[nc++] = hit;
	for(int i = 0; i < n && nc < 17; i++)
		cand[nc++] = others[i];
	for(int i = 0; i < nc; i++){
		int tri;
		if(PS3_RayTriangles(cand[i], cam, fwd, &tBest, &tri)){
			eBest = cand[i];
			triBest = tri;
		}
	}
	ps3AimMat = nil;
	ps3AimMatEnt = eBest;
	if(eBest == nil){
		if(log)
			PS3_Log("[aim]   drawn under the centre: no triangle of these models");
		return;
	}
	RpGeometry *g = RpAtomicGetGeometry((RpAtomic*)eBest->m_rwObject);
	rw::Triangle *tr = &g->triangles[triBest];
	rw::Material *mat = tr->matId < g->matList.numMaterials ? g->matList.materials[tr->matId] : nil;
	ps3AimMat = mat;
	if(!log)
		return;
	char col[96] = "no prelight";
	if(g->colors){
		rw::RGBA *k = g->colors;
		snprintf(col, sizeof(col), "prelight %d %d %d %d / %d %d %d %d / %d %d %d %d",
		         k[tr->v[0]].red, k[tr->v[0]].green, k[tr->v[0]].blue, k[tr->v[0]].alpha,
		         k[tr->v[1]].red, k[tr->v[1]].green, k[tr->v[1]].blue, k[tr->v[1]].alpha,
		         k[tr->v[2]].red, k[tr->v[2]].green, k[tr->v[2]].blue, k[tr->v[2]].alpha);
	}
	bool ta = mat && mat->texture && mat->texture->raster && GETPS3RASTEREXT(mat->texture->raster)->hasAlpha;
	int meshes = g->meshHeader ? g->meshHeader->numMeshes : 0;
	// patch 118: which way the triangle faces (its winding): a back face is
	// what backface culling throws away, the hole shows the clear colour
	{
		rw::V3d *v = g->morphTargets[0].vertices;
		CVector a(v[tr->v[0]].x, v[tr->v[0]].y, v[tr->v[0]].z);
		CVector b(v[tr->v[1]].x, v[tr->v[1]].y, v[tr->v[1]].z);
		CVector c(v[tr->v[2]].x, v[tr->v[2]].y, v[tr->v[2]].z);
		CVector n = Multiply3x3(eBest->GetMatrix(), CrossProduct(b - a, c - a));
		CVector wa = eBest->GetMatrix() * a, wb = eBest->GetMatrix() * b, wc = eBest->GetMatrix() * c;
		float facing = DotProduct(n, fwd);
		PS3_Logf("[aim]   triangle %d faces %s (normal %.2f %.2f %.2f), corners %.1f %.1f %.1f / %.1f %.1f %.1f / %.1f %.1f %.1f, %d vertices",
		         triBest, facing > 0.0f ? "AWAY (a back face: culled)" : "the camera", n.x, n.y, n.z,
		         wa.x, wa.y, wa.z, wb.x, wb.y, wb.z, wc.x, wc.y, wc.z, g->numVertices);
	}
	// patch 120: every triangle of the model the ray crosses within 10 cm of
	// that one (two layers in the same place: one facing up, one down?)
	{
		CMatrix inv = Invert(eBest->GetMatrix());
		CVector o = inv * cam;
		CVector d = Multiply3x3(inv, fwd);
		rw::V3d *v = g->morphTargets[0].vertices;
		int found = 0;
		for(int32 t = 0; t < g->numTriangles && found < 8; t++){
			if(t == triBest) continue;
			rw::Triangle *q = &g->triangles[t];
			CVector a(v[q->v[0]].x, v[q->v[0]].y, v[q->v[0]].z);
			CVector e1 = CVector(v[q->v[1]].x, v[q->v[1]].y, v[q->v[1]].z) - a;
			CVector e2 = CVector(v[q->v[2]].x, v[q->v[2]].y, v[q->v[2]].z) - a;
			CVector pp = CrossProduct(d, e2);
			float det = DotProduct(e1, pp);
			if(det > -1e-7f && det < 1e-7f) continue;
			float id = 1.0f/det;
			CVector sv = o - a;
			float u = DotProduct(sv, pp)*id;
			if(u < 0.0f || u > 1.0f) continue;
			CVector qq = CrossProduct(sv, e1);
			float w = DotProduct(d, qq)*id;
			if(w < 0.0f || u + w > 1.0f) continue;
			float tt = DotProduct(e2, qq)*id;
			if(tt < tBest - 0.1f || tt > tBest + 0.1f) continue;
			CVector nn = Multiply3x3(eBest->GetMatrix(), CrossProduct(e1, e2));
			rw::Material *mm = q->matId < g->matList.numMaterials ? g->matList.materials[q->matId] : nil;
			rw::RGBA *k = g->colors;
			PS3_Logf("[aim]   same place: triangle %d at %.2f m faces %s, material %d %s, prelight %d %d %d / %d %d %d / %d %d %d",
			         t, tt, DotProduct(nn, fwd) > 0.0f ? "AWAY" : "the camera", q->matId, mm && mm->texture ? mm->texture->name : "-",
			         k ? k[q->v[0]].red : -1, k ? k[q->v[0]].green : -1, k ? k[q->v[0]].blue : -1,
			         k ? k[q->v[1]].red : -1, k ? k[q->v[1]].green : -1, k ? k[q->v[1]].blue : -1,
			         k ? k[q->v[2]].red : -1, k ? k[q->v[2]].green : -1, k ? k[q->v[2]].blue : -1);
			found++;
		}
		if(!found)
			PS3_Log("[aim]   same place: no other triangle of the model");
	}
	PS3_Logf("[aim]   geometry flags 0x%x (%s), %s", g->flags, (g->flags & rw::Geometry::PRELIT) ? "PRELIT: the Leeds pipeline" : "not prelit: lit by the lights",
	         g->colors ? "colours kept" : "no colours");
	PS3_Logf("[aim]   drawn under the centre: %s, %.1f m, triangle %d of %d, material %d of %d (%d meshes): %s (colour %d %d %d %d%s%s), %s%s",
	         CModelInfo::GetModelInfo(eBest->GetModelIndex())->GetModelName(), tBest, triBest, g->numTriangles,
	         tr->matId, g->matList.numMaterials, meshes, mat && mat->texture ? mat->texture->name : "no texture",
	         mat ? mat->color.red : 0, mat ? mat->color.green : 0, mat ? mat->color.blue : 0, mat ? mat->color.alpha : 0,
	         ta ? ", texture alpha" : "", mat && rw::MatFX::get(mat) ? ", matfx" : "", col, PS3_AimFlags(eBest));
}

static void
PS3_AimUpdate(void)
{
	int m = rw::ps3::decalMode;
	if(m != 13 && m != 14 && m != 15)
		return;
	if(ps3AimFrame++ % 10)
		return;
	CVector cam = TheCamera.GetPosition();
	CVector fwd = TheCamera.GetForward();
	CColPoint cp;
	CEntity *hit = nil;
	if(!CWorld::ProcessLineOfSight(cam, cam + fwd*200.0f, cp, hit, true, false, false, true, false, false, false, false) || hit == nil){
		if(ps3AimEnt){
			ps3AimEnt = nil;
			ps3AimNumOthers = 0;
			AsciiToUnicode("Apuntado: nada", ps3AimMsg);
			CHud::SetHelpMessage(ps3AimMsg, true);
		}
		return;
	}
	CEntity *others[24];
	int16 n = 0;
	CWorld::FindObjectsKindaColliding(cp.point, m == 15 ? 3.0f : 1.0f, false, &n, 24, others, true, false, false, true, true);
	if(m == 15){
		// patch 114: the mesh drawn under the centre is hidden; the HUD names it
		CEntity *oth[24];
		int k = 0;
		for(int i = 0; i < n; i++) if(others[i] != hit) oth[k++] = others[i];
		rw::Material *was = ps3AimMat;
		CEntity *wasEnt = ps3AimMatEnt;
		PS3_AimRender(hit, oth, k, cam, fwd, false);
		if(ps3AimMat != was || ps3AimMatEnt != wasEnt || hit != ps3AimEnt){
			ps3AimEnt = hit;
			PS3_Logf("[aim] mode 15, %02d:%02d, camera %.1f %.1f %.1f looking %.2f %.2f %.2f, collision: %s at %.1f m",
			         CClock::GetHours(), CClock::GetMinutes(), cam.x, cam.y, cam.z, fwd.x, fwd.y, fwd.z,
			         CModelInfo::GetModelInfo(hit->GetModelIndex())->GetModelName(), (cp.point - cam).Magnitude());
			PS3_AimRender(hit, oth, k, cam, fwd);
		}
		if(ps3AimMat != rw::ps3::aimHideMaterial){
			rw::ps3::aimHideMaterial = ps3AimMat;
			char msg[200];
			snprintf(msg, sizeof(msg), "Malla oculta: %s, %s", ps3AimMatEnt ? CModelInfo::GetModelInfo(ps3AimMatEnt->GetModelIndex())->GetModelName() : "nada",
			         ps3AimMat && ps3AimMat->texture ? ps3AimMat->texture->name : "sin textura");
			AsciiToUnicode(msg, ps3AimMsg);
			CHud::SetHelpMessage(ps3AimMsg, true);
		}
		return;
	}
	bool changed = hit != ps3AimEnt;
	if(!changed){
		// the set at the point may still change
		int k = 0;
		for(int i = 0; i < n; i++) if(others[i] != hit) k++;
		changed = k != ps3AimNumOthers;
	}
	if(!changed)
		return;
	ps3AimEnt = hit;
	ps3AimNumOthers = 0;
	for(int i = 0; i < n && ps3AimNumOthers < 16; i++)
		if(others[i] != hit)
			ps3AimOthers[ps3AimNumOthers++] = others[i];
	const char *name = CModelInfo::GetModelInfo(hit->GetModelIndex())->GetModelName();
	float dist = (cp.point - cam).Magnitude();
	char msg[200];
	int len = snprintf(msg, sizeof(msg), "Apuntado: %s (%.0f m)", name, dist);
	for(int i = 0; i < ps3AimNumOthers && len < (int)sizeof(msg) - 40; i++)
		len += snprintf(msg + len, sizeof(msg) - len, "%s%s", i ? ", " : " + ", CModelInfo::GetModelInfo(ps3AimOthers[i]->GetModelIndex())->GetModelName());
	AsciiToUnicode(msg, ps3AimMsg);
	CHud::SetHelpMessage(ps3AimMsg, true);
	CVector hp = hit->GetPosition();
	PS3_Logf("[aim] mode %d, %02d:%02d, camera %.1f %.1f %.1f -> point %.2f %.2f %.2f (%.1f m), normal %.2f %.2f %.2f, surface %d",
	         m, CClock::GetHours(), CClock::GetMinutes(), cam.x, cam.y, cam.z, cp.point.x, cp.point.y, cp.point.z, dist,
	         cp.normal.x, cp.normal.y, cp.normal.z, cp.surfaceB);
	PS3_Logf("[aim]   hit: %s (id %d) at %.1f %.1f %.1f%s", name, hit->GetModelIndex(), hp.x, hp.y, hp.z, PS3_AimFlags(hit));
	PS3_AimTriangle(hit, cp.point);
	PS3_AimRender(hit, ps3AimOthers, ps3AimNumOthers, cam, fwd);
	for(int i = 0; i < ps3AimNumOthers; i++){
		CEntity *e = ps3AimOthers[i];
		CVector p = e->GetPosition();
		PS3_Logf("[aim]   also here: %s (id %d) at %.1f %.1f %.1f%s", CModelInfo::GetModelInfo(e->GetModelIndex())->GetModelName(),
		         e->GetModelIndex(), p.x, p.y, p.z, PS3_AimFlags(e));
		PS3_AimTriangle(e, cp.point);
	}
}

// ---- patch 115: test mode 16, a free camera over a frozen view -----------------
// The game (and TheCamera, which ConstructRenderList culls and picks the LODs
// with) stays where it was: Toni stands, the pad goes to the free camera, and
// only the RW camera is moved for the frame's drawing (PS3_FreeCamBeginRender /
// EndRender, main.cpp). What is drawn is what Toni's camera would draw, seen from
// elsewhere. A cross at the centre: X hides the mesh under it (again: shown), square
// the whole model, O shows everything again. [free] logs every pick.
// Left stick moves, right stick looks, R1/L1 up/down, R2 faster, L2 slower
CEntity *ps3FreeHideEnt;
float gPS3LodMult = 1.0f;	// patch 122: Camera.cpp multiplies LODDistMultiplier by it
static bool ps3FreeOn, ps3FreeSaved;
static CVector ps3FreePos;
static float ps3FreeHeading, ps3FreePitch;
static unsigned ps3FreePrev;
static uint32 ps3FreeFrame;
static rw::Matrix ps3FreeSavedMat;

static CVector
PS3_FreeForward(void)
{
	return CVector(Cos(ps3FreePitch)*Cos(ps3FreeHeading), Cos(ps3FreePitch)*Sin(ps3FreeHeading), Sin(ps3FreePitch));
}

bool
PS3_FreeCamActive(void)
{
	return ps3FreeOn && PS3_logsOn && rw::ps3::decalMode == 16;
}

// patch 116: the models around the cross, and whether each was drawn. A
// window or a piece of road that isn't drawn can't be picked by what's drawn:
// these are all the models within 3 m of the cross' point, drawn or not
int gPS3WatchNum;
static CEntity *ps3Watch[18];
static uint32 ps3WatchDrawn[18];
static CEntity *ps3WatchLastEnt;
static rw::Material *ps3WatchLastMat;
static uint32 ps3WatchSetFrame;

void
PS3_WatchDrawn(CEntity *e)
{
	for(int i = 0; i < gPS3WatchNum; i++)
		if(ps3Watch[i] == e)
			ps3WatchDrawn[i] = CTimer::GetFrameCounter();
}

static void
PS3_WatchState(CEntity *e, uint32 drawnFrame, char *buf, int size)
{
	CBaseModelInfo *mi = CModelInfo::GetModelInfo(e->GetModelIndex());
	float dist = (e->GetPosition() - TheCamera.GetPosition()).Magnitude();
	float lodDist = 0.0f;
	if(mi->GetModelType() == MITYPE_SIMPLE || mi->GetModelType() == MITYPE_TIME)
		lodDist = ((CSimpleModelInfo*)mi)->GetLargestLodDistance();
	uint32 now = CTimer::GetFrameCounter();
	const char *drawn = drawnFrame == 0 ? "NOT DRAWN" : now - drawnFrame <= 2 ? "drawn" : "not drawn lately";
	snprintf(buf, size, "%s: %s, %s, %s, %s, %.0f m (draws to %.0f m)%s%s%s",
	         mi->GetModelName(), drawn,
	         e->m_rwObject ? "loaded" : "NOT LOADED",
	         e->GetIsOnScreen() ? "on Toni's screen" : "OFF Toni's screen",
	         e->IsEntityOccluded() ? "OCCLUDED" : "not occluded",
	         dist, lodDist * TheCamera.LODDistMultiplier,
	         e->bIsVisible ? "" : ", invisible",
	         e->bIsBIGBuilding ? ", BIG" : "", PS3_AimFlags(e));
}

static void
PS3_WatchReport(const char *why)
{
	char b[256];
	PS3_Logf("[free] %s: %d models within 3 m of the cross' point (occlusion %s)", why, gPS3WatchNum, gPS3NoOcclusion ? "OFF" : "on");
	for(int i = 0; i < gPS3WatchNum; i++){
		PS3_WatchState(ps3Watch[i], ps3WatchDrawn[i], b, sizeof(b));
		PS3_Logf("[free]   %s", b);
	}
}

// what is drawn under the cross: the ray from the free camera
static void
PS3_FreePick(int action)	// 0 look, 1 hide the mesh, 2 hide the model
{
	CVector cam = ps3FreePos, fwd = PS3_FreeForward();
	CColPoint cp;
	CEntity *hit = nil;
	if(!CWorld::ProcessLineOfSight(cam, cam + fwd*300.0f, cp, hit, true, false, false, true, false, false, false, false) || hit == nil){
		if(action){
			AsciiToUnicode("Libre: nada bajo la cruz", ps3AimMsg);
			CHud::SetHelpMessage(ps3AimMsg, true);
		}
		return;
	}
	CEntity *others[24], *oth[24];
	int16 n = 0;
	CWorld::FindObjectsKindaColliding(cp.point, 3.0f, false, &n, 24, others, true, false, false, true, true);
	int k = 0;
	for(int i = 0; i < n; i++) if(others[i] != hit) oth[k++] = others[i];
	PS3_AimRender(hit, oth, k, cam, fwd, action != 0);
	// patch 116: the models around the point, watched; their state logged when
	// the cross moves to another mesh (after they had a frame to be drawn)
	bool sameSet = gPS3WatchNum == k + 1 && ps3Watch[0] == hit;
	if(!sameSet){
		gPS3WatchNum = 0;
		ps3Watch[gPS3WatchNum] = hit; ps3WatchDrawn[gPS3WatchNum++] = 0;
		for(int i = 0; i < k && gPS3WatchNum < 18; i++){
			ps3Watch[gPS3WatchNum] = oth[i]; ps3WatchDrawn[gPS3WatchNum++] = 0;
		}
		ps3WatchSetFrame = CTimer::GetFrameCounter();
	}else if((ps3AimMatEnt != ps3WatchLastEnt || ps3AimMat != ps3WatchLastMat) &&
	         CTimer::GetFrameCounter() - ps3WatchSetFrame > 3){
		ps3WatchLastEnt = ps3AimMatEnt;
		ps3WatchLastMat = ps3AimMat;
		char why[160];
		snprintf(why, sizeof(why), "cross on %s / %s, from %.1f %.1f %.1f looking %.2f %.2f %.2f",
		         ps3AimMatEnt ? CModelInfo::GetModelInfo(ps3AimMatEnt->GetModelIndex())->GetModelName() : "nothing",
		         ps3AimMat && ps3AimMat->texture ? ps3AimMat->texture->name : "-", cam.x, cam.y, cam.z, fwd.x, fwd.y, fwd.z);
		PS3_WatchReport(why);
	}
	const char *model = ps3AimMatEnt ? CModelInfo::GetModelInfo(ps3AimMatEnt->GetModelIndex())->GetModelName() : "nada";
	const char *tex = ps3AimMat && ps3AimMat->texture ? ps3AimMat->texture->name : "sin textura";
	const char *what = "";
	if(action == 1){
		rw::ps3::aimHideMaterial = rw::ps3::aimHideMaterial == ps3AimMat ? nil : ps3AimMat;
		what = rw::ps3::aimHideMaterial ? " - malla oculta" : " - malla visible";
	}else if(action == 2){
		ps3FreeHideEnt = ps3FreeHideEnt == ps3AimMatEnt ? nil : ps3AimMatEnt;
		what = ps3FreeHideEnt ? " - modelo oculto" : " - modelo visible";
	}
	if(action)
		PS3_Logf("[free] %s at %.1f %.1f %.1f looking %.2f %.2f %.2f: %s / %s%s", action == 1 ? "X" : "square",
		         cam.x, cam.y, cam.z, fwd.x, fwd.y, fwd.z, model, tex, what);
	char msg[200];
	snprintf(msg, sizeof(msg), "Libre: %s / %s%s", model, tex, what);
	AsciiToUnicode(msg, ps3AimMsg);
	CHud::SetHelpMessage(ps3AimMsg, true);
}

static void
PS3_FreeCamPad(void)
{
	if(!PS3_logsOn || rw::ps3::decalMode != 16){
		if(ps3FreeOn){
			ps3FreeOn = false;
			ps3FreeHideEnt = nil;
			rw::ps3::aimHideMaterial = nil;
			gPS3WatchNum = 0;
			rw::ps3::forceNoCull = false;
			rw::ps3::leedsDebug = 0;
			gPS3LodMult = 1.0f;
			ps3WatchLastEnt = nil;
			ps3WatchLastMat = nil;
		}
		return;
	}
	CPad *pad = CPad::GetPad(0);
	if(!ps3FreeOn){
		ps3FreeOn = true;
		ps3FreeHideEnt = nil;
		rw::ps3::aimHideMaterial = nil;
		ps3FreePos = TheCamera.GetPosition();
		CVector f = TheCamera.GetForward();
		ps3FreeHeading = Atan2(f.y, f.x);
		ps3FreePitch = Asin(Clamp(f.z, -1.0f, 1.0f));
		ps3FreePrev = ~0u;
		PS3_Logf("[free] on at %.1f %.1f %.1f, %02d:%02d (the view stays Toni's camera's)", ps3FreePos.x, ps3FreePos.y, ps3FreePos.z,
		         CClock::GetHours(), CClock::GetMinutes());
	}
	float dt = Min(CTimer::GetTimeStepNonClippedInSeconds(), 0.1f);
	CControllerState &st = pad->NewState;
	float speed = 6.0f;
	if(st.RightShoulder2) speed *= 4.0f;
	if(st.LeftShoulder2) speed *= 0.2f;
	float lx = st.LeftStickX/128.0f, ly = -st.LeftStickY/128.0f;
	float rx = st.RightStickX/128.0f, ry = -st.RightStickY/128.0f;
	if(Abs(lx) < 0.15f) lx = 0.0f;
	if(Abs(ly) < 0.15f) ly = 0.0f;
	if(Abs(rx) < 0.15f) rx = 0.0f;
	if(Abs(ry) < 0.15f) ry = 0.0f;
	ps3FreeHeading -= rx*2.0f*dt;
	ps3FreePitch = Clamp(ps3FreePitch + ry*1.5f*dt, -1.5f, 1.5f);
	CVector fwd = PS3_FreeForward();
	CVector right(Sin(ps3FreeHeading), -Cos(ps3FreeHeading), 0.0f);
	ps3FreePos += (fwd*ly + right*lx)*speed*dt;
	if(st.RightShoulder1) ps3FreePos.z += speed*dt;
	if(st.LeftShoulder1) ps3FreePos.z -= speed*dt;
#define PS3B(b) (1u << (b))
	unsigned m = PS3_PadButtons();
	unsigned down = m & ~ps3FreePrev;
	ps3FreePrev = m;
	if(!(m & PS3B(GLFW_GAMEPAD_BUTTON_BACK))){	// not while Select is held (R3 + Select)
		if(down & PS3B(GLFW_GAMEPAD_BUTTON_A))
			PS3_FreePick(1);
		else if(down & PS3B(GLFW_GAMEPAD_BUTTON_X))
			PS3_FreePick(2);
		else if(down & PS3B(GLFW_GAMEPAD_BUTTON_B)){
			ps3FreeHideEnt = nil;
			rw::ps3::aimHideMaterial = nil;
			PS3_Log("[free] O: everything shown");
			AsciiToUnicode("Libre: todo visible", ps3AimMsg);
			CHud::SetHelpMessage(ps3AimMsg, true);
		}else if(down & PS3B(GLFW_GAMEPAD_BUTTON_RIGHT_THUMB)){
			// patch 117: R3 alone: hide the next of the models around the cross,
			// one after the other (also those not drawn or without collision)
			static int idx = -1;
			if(gPS3WatchNum == 0){
				AsciiToUnicode("Libre: ningun modelo cerca de la cruz", ps3AimMsg);
			}else{
				idx = (idx + 1) % (gPS3WatchNum + 1);
				char msg[200];
				if(idx == gPS3WatchNum){
					ps3FreeHideEnt = nil;
					snprintf(msg, sizeof(msg), "Libre: R3, ninguno oculto");
				}else{
					ps3FreeHideEnt = ps3Watch[idx];
					snprintf(msg, sizeof(msg), "Libre: R3, oculto %d de %d: %s", idx + 1, gPS3WatchNum,
					         CModelInfo::GetModelInfo(ps3FreeHideEnt->GetModelIndex())->GetModelName());
				}
				PS3_Logf("[free] %s", msg + 7);
				AsciiToUnicode(msg, ps3AimMsg);
			}
			CHud::SetHelpMessage(ps3AimMsg, true);
		}else if(down & PS3B(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT)){
			// patch 122: every model's draw distance x1 / x1.5 / x2 / x3 (the
			// detailed road drawn farther: does the LOD-looking wedge go?)
			static const float mults[] = { 1.0f, 1.5f, 2.0f, 3.0f };
			static int mi = 0;
			mi = (mi + 1) % 4;
			gPS3LodMult = mults[mi];
			PS3_Logf("[free] D-pad right: draw distances x%.1f (LODDistMultiplier %.2f before)", gPS3LodMult, TheCamera.LODDistMultiplier);
			char msg[96];
			snprintf(msg, sizeof(msg), "Libre: distancia de dibujo x%.1f", gPS3LodMult);
			AsciiToUnicode(msg, ps3AimMsg);
			CHud::SetHelpMessage(ps3AimMsg, true);
		}else if(down & PS3B(GLFW_GAMEPAD_BUTTON_DPAD_LEFT)){
			// patch 121: the buildings' prelight: as is / ignored (flat) / alone
			rw::ps3::leedsDebug = (rw::ps3::leedsDebug + 1) % 3;
			static const char *const names[] = { "luz normal", "SIN prelight (plano)", "SOLO prelight" };
			static const char *const lognames[] = { "as is", "the prelight ignored (flat)", "the prelight alone" };
			PS3_Logf("[free] D-pad left: Leeds lighting %s", lognames[rw::ps3::leedsDebug]);
			char msg[96];
			snprintf(msg, sizeof(msg), "Libre: %s", names[rw::ps3::leedsDebug]);
			AsciiToUnicode(msg, ps3AimMsg);
			CHud::SetHelpMessage(ps3AimMsg, true);
		}else if(down & PS3B(GLFW_GAMEPAD_BUTTON_DPAD_UP)){
			// patch 117: no backface culling at all (the inside of a building
			// seen through its windows: the far walls' inner faces)
			rw::ps3::forceNoCull = !rw::ps3::forceNoCull;
			PS3_Logf("[free] D-pad up: backface culling %s", rw::ps3::forceNoCull ? "OFF" : "on");
			AsciiToUnicode(rw::ps3::forceNoCull ? "Libre: sin culling (caras traseras visibles)" : "Libre: culling normal", ps3AimMsg);
			CHud::SetHelpMessage(ps3AimMsg, true);
		}else if(down & PS3B(GLFW_GAMEPAD_BUTTON_DPAD_DOWN)){
			// patch 117: the mesh under the cross opaque (its material alpha 255) / back
			static rw::Material *opMat;
			static uint8 opAlpha;
			char msg[200];
			if(opMat){
				opMat->color.alpha = opAlpha;
				snprintf(msg, sizeof(msg), "Libre: %s con su alfa (%d)", opMat->texture ? opMat->texture->name : "-", opAlpha);
				opMat = nil;
			}else if(ps3AimMat){
				opMat = ps3AimMat;
				opAlpha = opMat->color.alpha;
				opMat->color.alpha = 255;
				snprintf(msg, sizeof(msg), "Libre: %s opaca (era alfa %d)", opMat->texture ? opMat->texture->name : "-", opAlpha);
			}else
				snprintf(msg, sizeof(msg), "Libre: nada bajo la cruz");
			PS3_Logf("[free] D-pad down: %s", msg + 7);
			AsciiToUnicode(msg, ps3AimMsg);
			CHud::SetHelpMessage(ps3AimMsg, true);
		}else if(down & PS3B(GLFW_GAMEPAD_BUTTON_Y)){
			// patch 116: the occluders on/off, live (noocclusion.txt does it from the start)
			gPS3NoOcclusion = !gPS3NoOcclusion;
			PS3_WatchReport(gPS3NoOcclusion ? "triangle: occlusion OFF" : "triangle: occlusion on");
			AsciiToUnicode(gPS3NoOcclusion ? "Libre: oclusion APAGADA" : "Libre: oclusion encendida", ps3AimMsg);
			CHud::SetHelpMessage(ps3AimMsg, true);
		}else if(ps3FreeFrame++ % 15 == 0 && !rw::ps3::aimHideMaterial && !ps3FreeHideEnt)
			PS3_FreePick(0);
	}
#undef PS3B
	pad->NewState.Clear();	// Toni stands still
}

// main.cpp: around the frame's drawing, the RW camera at the free camera
void
PS3_FreeCamBeginRender(void)
{
	RwFrame *frame = RwCameraGetFrame(Scene.camera);
	RwMatrix *mat = RwFrameGetMatrix(frame);
	if(ps3FreeSaved){	// the frame before didn't end (its start failed)
		*mat = ps3FreeSavedMat;
		ps3FreeSaved = false;
	}
	if(!PS3_FreeCamActive())
		return;
	ps3FreeSavedMat = *mat;
	ps3FreeSaved = true;
	CVector f = PS3_FreeForward();
	CVector up(0.0f, 0.0f, 1.0f);
	CVector right = CrossProduct(up, f);
	right.Normalise();
	up = CrossProduct(f, right);
	*RwMatrixGetPos(mat) = ps3FreePos;
	*RwMatrixGetAt(mat) = f;
	*RwMatrixGetUp(mat) = up;
	*RwMatrixGetRight(mat) = right;
	RwMatrixUpdate(mat);
	RwFrameUpdateObjects(frame);
}

void
PS3_FreeCamEndRender(void)
{
	if(!ps3FreeSaved)
		return;
	RwFrame *frame = RwCameraGetFrame(Scene.camera);
	*RwFrameGetMatrix(frame) = ps3FreeSavedMat;
	RwMatrixUpdate(RwFrameGetMatrix(frame));
	RwFrameUpdateObjects(frame);
	rw::Frame::syncDirty();	// TheCamera.m_viewMatrix (the RW camera's) back to the game's at once
	ps3FreeSaved = false;
}

// Render2dStuff: the cross
void
PS3_FreeCamDraw2D(void)
{
	if(!PS3_FreeCamActive())
		return;
	float cx = SCREEN_WIDTH/2, cy = SCREEN_HEIGHT/2;
	CRGBA c(255, 40, 40, 255);
	CSprite2d::DrawRect(CRect(cx - 12.0f, cy - 1.0f, cx + 12.0f, cy + 1.0f), c);
	CSprite2d::DrawRect(CRect(cx - 1.0f, cy - 12.0f, cx + 1.0f, cy + 12.0f), c);
}

// ---- patch 123: test mode 17, what changes from frame to frame ------------------
// Toni plays as usual. Every frame the buildings drawn are compared with the
// frame before: [diff] lists those that appear (+) and go (-), with the camera
// and their distance; models and TXDs streamed in and removed (Streaming.cpp);
// and, once per model and material, a mesh drawn without a texture or with
// one not uploaded yet (rw::ps3::diffMatHook). Stand where the bug comes and
// goes, step back and forth: the log says what changed
bool gPS3DiffOn;
struct PS3DiffItem { CEntity *e; int16 model; int8 level, levels; float x, y, z; };
static PS3DiffItem ps3DiffA[3000], ps3DiffB[3000];
static PS3DiffItem *ps3DiffCur = ps3DiffA, *ps3DiffPrev = ps3DiffB;
static int ps3DiffNumCur, ps3DiffNumPrev;
static bool ps3DiffStarted;
static int16 ps3DiffCurModel = -1;
static uint32 ps3DiffMatSeen[512];

void
PS3_DiffDrawn(CEntity *e)
{
	ps3DiffCurModel = e->GetModelIndex();
	if(!e->IsBuilding() || ps3DiffNumCur >= 3000)
		return;
	PS3DiffItem &it = ps3DiffCur[ps3DiffNumCur++];
	it.e = e;
	it.model = e->GetModelIndex();
	it.x = e->GetPosition().x; it.y = e->GetPosition().y; it.z = e->GetPosition().z;
	// patch 125: which detail level (atomic) of the model is drawn
	it.level = -1;
	it.levels = 0;
	CBaseModelInfo *bmi = CModelInfo::GetModelInfo(it.model);
	if((bmi->GetModelType() == MITYPE_SIMPLE || bmi->GetModelType() == MITYPE_TIME) && e->m_rwObject &&
	   RwObjectGetType(e->m_rwObject) == rpATOMIC){
		CSimpleModelInfo *smi = (CSimpleModelInfo*)bmi;
		RpGeometry *g = RpAtomicGetGeometry((RpAtomic*)e->m_rwObject);
		it.levels = smi->m_numAtomics;
		for(int i = 0; i < smi->m_numAtomics; i++)
			if(smi->m_atomics[i] && RpAtomicGetGeometry(smi->m_atomics[i]) == g)
				it.level = i;
	}
}

static void
PS3_DiffMat(rw::Material *m, int why)
{
	// once per model and material (a small hash; a collision only skips a line)
	uint32 h = ((uint32)(uintptr)m * 2654435761u) ^ ((uint32)ps3DiffCurModel * 40503u);
	uint32 slot = (h >> 7) % 512;
	if(ps3DiffMatSeen[slot] == (h | 1))
		return;
	ps3DiffMatSeen[slot] = h | 1;
	const char *model = ps3DiffCurModel >= 0 ? CModelInfo::GetModelInfo(ps3DiffCurModel)->GetModelName() : "?";
	PS3_Logf("[diff]   %s: %s drawn %s", model, why == 1 ? "a mesh" : (m->texture ? m->texture->name : "?"),
	         why == 1 ? "WITHOUT a texture (not found when loaded)" : "with its texture NOT UPLOADED yet (white)");
}

static int
PS3_DiffCmp(const void *a, const void *b)
{
	uintptr x = (uintptr)((const PS3DiffItem*)a)->e, y = (uintptr)((const PS3DiffItem*)b)->e;
	return x < y ? -1 : x > y ? 1 : 0;
}

void
PS3_DiffEndFrame(void)
{
	bool on = PS3_logsOn && rw::ps3::decalMode == 17;
	if(!on){
		if(gPS3DiffOn){
			gPS3DiffOn = false;
			rw::ps3::diffOn = false;
			rw::ps3::diffMatHook = nil;
			PS3_Log("[diff] off");
		}
		ps3DiffNumCur = ps3DiffNumPrev = 0;
		ps3DiffStarted = false;
		return;
	}
	if(!gPS3DiffOn){
		// on from the next frame: this one wasn't recorded
		gPS3DiffOn = true;
		rw::ps3::diffOn = true;
		rw::ps3::diffMatHook = PS3_DiffMat;
		memset(ps3DiffMatSeen, 0, sizeof(ps3DiffMatSeen));
		ps3DiffNumCur = 0;
		return;
	}
	qsort(ps3DiffCur, ps3DiffNumCur, sizeof(PS3DiffItem), PS3_DiffCmp);
	int n = 0;	// dedupe (an entity can be drawn twice)
	for(int i = 0; i < ps3DiffNumCur; i++)
		if(n == 0 || ps3DiffCur[n-1].e != ps3DiffCur[i].e)
			ps3DiffCur[n++] = ps3DiffCur[i];
	ps3DiffNumCur = n;
	CVector cam = TheCamera.GetPosition();
	if(!ps3DiffStarted){
		ps3DiffStarted = true;
		PS3_Logf("[diff] on: %d buildings drawn, camera %.1f %.1f %.1f, %02d:%02d, draw distances x%.2f",
		         ps3DiffNumCur, cam.x, cam.y, cam.z, CClock::GetHours(), CClock::GetMinutes(), TheCamera.LODDistMultiplier);
	}else{
		int i = 0, j = 0, lines = 0;
		bool head = false;
		while((i < ps3DiffNumPrev || j < ps3DiffNumCur) && lines < 60){
			PS3DiffItem *it;
			char sign;
			if(j >= ps3DiffNumCur || (i < ps3DiffNumPrev && (uintptr)ps3DiffPrev[i].e < (uintptr)ps3DiffCur[j].e)){
				it = &ps3DiffPrev[i++]; sign = '-';
			}else if(i >= ps3DiffNumPrev || (uintptr)ps3DiffCur[j].e < (uintptr)ps3DiffPrev[i].e){
				it = &ps3DiffCur[j++]; sign = '+';
			}else{
				PS3DiffItem *a = &ps3DiffPrev[i++], *b = &ps3DiffCur[j++];
				if(a->level == b->level)
					continue;
				it = b; sign = '~';	// patch 125: the same model, another detail level
			}
			if(!head){
				head = true;
				PS3_Logf("[diff] frame %u, camera %.1f %.1f %.1f looking %.2f %.2f %.2f, %d drawn:", CTimer::GetFrameCounter(),
				         cam.x, cam.y, cam.z, TheCamera.GetForward().x, TheCamera.GetForward().y, TheCamera.GetForward().z, ps3DiffNumCur);
			}
			float d = (CVector(it->x, it->y, it->z) - cam).Magnitude();
			CBaseModelInfo *mi = CModelInfo::GetModelInfo(it->model);
			float lodDist = 0.0f;
			if(mi->GetModelType() == MITYPE_SIMPLE || mi->GetModelType() == MITYPE_TIME)
				lodDist = ((CSimpleModelInfo*)mi)->GetLargestLodDistance();
			if(it->levels > 1)
				PS3_Logf("[diff]   %c %s at %.1f %.1f %.1f, %.0f m (draws to %.0f m), detail level %d of %d (switches at %.0f / %.0f m)", sign, mi->GetModelName(),
				         it->x, it->y, it->z, d, lodDist, it->level, it->levels,
				         ((CSimpleModelInfo*)mi)->GetLodDistance(0), ((CSimpleModelInfo*)mi)->GetLodDistance(1));
			else
				PS3_Logf("[diff]   %c %s at %.1f %.1f %.1f, %.0f m (draws to %.0f m)", sign, mi->GetModelName(), it->x, it->y, it->z, d, lodDist);
			lines++;
		}
		if(lines >= 60)
			PS3_Log("[diff]   (more changes this frame, not listed)");
	}
	PS3DiffItem *t = ps3DiffPrev; ps3DiffPrev = ps3DiffCur; ps3DiffCur = t;
	ps3DiffNumPrev = ps3DiffNumCur;
	ps3DiffNumCur = 0;
}

// patch 126: entering mode 18, the LODs (big buildings) within 400 m: their
// collision bounds (what the game's checks use) against their drawn geometry's
// bound sphere. A LOD whose collision is small or elsewhere can't be found
// "over" the place it covers (patch 38, 124)
static void
PS3_BigBoundsReport(void)
{
	CVector cam = TheCamera.GetPosition();
	int n = 0;
	for(int lv = 0; lv < NUM_LEVELS; lv++){
		CPtrList &list = CWorld::GetBigBuildingList((eLevelName)lv);
		for(CPtrNode *node = list.first; node && n < 80; node = node->next){
			CEntity *e = (CEntity*)node->item;
			float d = (e->GetPosition() - cam).Magnitude();
			if(d > 400.0f)
				continue;
			CVector bc = e->GetBoundCentre();
			float br = e->GetBoundRadius();
			char geo[128] = "no geometry loaded";
			CBaseModelInfo *bmi = CModelInfo::GetModelInfo(e->GetModelIndex());
			if(bmi->GetModelType() == MITYPE_SIMPLE || bmi->GetModelType() == MITYPE_TIME){
				CSimpleModelInfo *smi = (CSimpleModelInfo*)bmi;
				if(smi->m_atomics && smi->m_atomics[0]){
					RpGeometry *g = RpAtomicGetGeometry(smi->m_atomics[0]);
					if(g && g->morphTargets){
						rw::Sphere &sp = g->morphTargets[0].boundingSphere;
						CVector wc = e->GetMatrix() * CVector(sp.center.x, sp.center.y, sp.center.z);
						snprintf(geo, sizeof(geo), "drawn geometry: centre %.0f %.0f %.0f, radius %.0f (%.0f m from the camera's point)",
						         wc.x, wc.y, wc.z, sp.radius, (wc - cam).Magnitude());
					}
				}
			}
			PS3_Logf("[lods] %s at %.0f %.0f %.0f, %.0f m: collision bounds centre %.0f %.0f %.0f, radius %.0f; %s",
			         bmi->GetModelName(), e->GetPosition().x, e->GetPosition().y, e->GetPosition().z, d, bc.x, bc.y, bc.z, br, geo);
			n++;
		}
	}
	PS3_Logf("[lods] %d LODs within 400 m of the camera (%.0f %.0f %.0f)", n, cam.x, cam.y, cam.z);
}

// CPad::UpdatePads (Pad.cpp), after pad 0: R3 + Select = next test mode
// (the log gets a snapshot of the mode being left)
void
PS3_DecalTestPad(void)
{
#define PS3B(b) (1u << (b))
	if(!PS3_logsOn)
		return;	// patch 98: a testing tool, only with logson.txt (the shadows are fixed, patch 97)
	PS3_AimUpdate();	// patch 110
	PS3_FreeCamPad();	// patch 115
	unsigned m = PS3_PadButtons();
	unsigned down = m & ~ps3DecalPrev;
	ps3DecalPrev = m;
	unsigned want = PS3B(GLFW_GAMEPAD_BUTTON_RIGHT_THUMB) | PS3B(GLFW_GAMEPAD_BUTTON_BACK);
	if((m & want) != want || (down & want) == 0)
		return;
	if(m & PS3B(GLFW_GAMEPAD_BUTTON_LEFT_THUMB))
		return;	// L3 + Select is the mission spawner
#undef PS3B
	CPad::GetPad(0)->NewState.Clear();	// not a camera change
	if(PS3_logsOn)
		PS3_DecalSnapshot("leaving the mode");
	rw::ps3::decalMode = (rw::ps3::decalMode + 1) % PS3_NUMTESTMODES;
	ps3AimEnt = nil;
	ps3AimNumOthers = 0;
	ps3AimFrame = 0;
	rw::ps3::aimHideMaterial = nil;
	char msg[96];
	if(rw::ps3::decalMode == 18)
		PS3_BigBoundsReport();	// patch 126
	snprintf(msg, sizeof(msg), "Sombras: modo %s", ps3DecalModeName[rw::ps3::decalMode]);
	AsciiToUnicode(msg, ps3DecalMsg);
	CHud::SetHelpMessage(ps3DecalMsg, true);
	PS3_Logf("[snap] now mode %s", ps3DecalModeName[rw::ps3::decalMode]);
}

void
PS3_WeatherReport(void)
{
	PS3_Logf("[weather] %d -> %d (%.2f), %02d:%02d, rain %.2f, fog %.2f, extra colour %s%d (%.2f); "
	         "ambient %.2f %.2f %.2f, objects %.2f %.2f %.2f, directional %.2f %.2f %.2f; "
	         "sky %d %d %d / %d %d %d, fog colour %d %d %d, far clip %.0f, fog start %.0f",
	         CWeather::OldWeatherType, CWeather::NewWeatherType, CWeather::InterpolationValue,
	         CClock::GetHours(), CClock::GetMinutes(), CWeather::Rain, CWeather::Foggyness,
	         CTimeCycle::m_bExtraColourOn ? "on " : "off ", CTimeCycle::m_ExtraColour, CTimeCycle::m_ExtraColourInter,
	         CTimeCycle::GetAmbientRed(), CTimeCycle::GetAmbientGreen(), CTimeCycle::GetAmbientBlue(),
	         CTimeCycle::GetAmbientRed_Obj(), CTimeCycle::GetAmbientGreen_Obj(), CTimeCycle::GetAmbientBlue_Obj(),
	         CTimeCycle::GetDirectionalRed(), CTimeCycle::GetDirectionalGreen(), CTimeCycle::GetDirectionalBlue(),
	         CTimeCycle::GetSkyTopRed(), CTimeCycle::GetSkyTopGreen(), CTimeCycle::GetSkyTopBlue(),
	         CTimeCycle::GetSkyBottomRed(), CTimeCycle::GetSkyBottomGreen(), CTimeCycle::GetSkyBottomBlue(),
	         CTimeCycle::GetFogRed(), CTimeCycle::GetFogGreen(), CTimeCycle::GetFogBlue(),
	         CTimeCycle::GetFarClip(), CTimeCycle::GetFogStart());
}

enum { PS3DARK_UNKNOWN, PS3DARK_OK, PS3DARK_PRELIT, PS3DARK_UNLIT, PS3DARK_NOTEX, PS3DARK_NOPRELIT };
static const char *ps3DarkWhy[] = { "?", "ok", "prelight nearly black", "no prelight, no normals or lighting", "textured mesh without its texture",
	"no prelight: lit by the ambient light (patch 100 lifts it)" };
struct PS3DarkSeen { int16 model; uint8 why; uint8 lod; int16 prelit; float dist; };
static PS3DarkSeen ps3DarkSeen[16];
static int32 ps3NumDarkSeen, ps3DarkDrawn, ps3BuildingsDrawn;
static uint8 ps3DarkClass[MODELINFOSIZE];	// 0 = not looked at yet

// what this building's geometry can give, worked out once per model
static int32
PS3_ClassifyBuilding(RpAtomic *atomic, int32 *prelitAvg)
{
	rw::Geometry *g = atomic->geometry;
	*prelitAvg = -1;
	if(g == nil)
		return PS3DARK_UNKNOWN;
	for(int32 i = 0; i < g->matList.numMaterials; i++){
		rw::Material *m = g->matList.materials[i];
		if((g->flags & rw::Geometry::TEXTURED) && m && (m->texture == nil || m->texture->raster == nil))
			return PS3DARK_NOTEX;
	}
	if(g->flags & rw::Geometry::PRELIT){
		if(g->colors == nil || g->numVertices == 0)
			return PS3DARK_UNKNOWN;
		uint32 sum = 0;
		for(int32 i = 0; i < g->numVertices; i++)
			sum += g->colors[i].red + g->colors[i].green + g->colors[i].blue;
		*prelitAvg = sum / (3*g->numVertices);
		return *prelitAvg < 12 ? PS3DARK_PRELIT : PS3DARK_OK;
	}
	if(!(g->flags & rw::Geometry::NORMALS) || !(g->flags & rw::Geometry::LIGHT))
		return PS3DARK_UNLIT;
	return PS3DARK_NOPRELIT;	// patch 100: listed too, black in the rain at night before
}

static void
PS3_NoteBuildingDrawn(CEntity *e)
{
	int32 id = e->GetModelIndex();
	if(id < 0 || id >= MODELINFOSIZE || e->m_rwObject == nil)
		return;
	ps3BuildingsDrawn++;
	int32 prelit = -1;
	if(ps3DarkClass[id] == 0)
		ps3DarkClass[id] = 1 + PS3_ClassifyBuilding((RpAtomic*)e->m_rwObject, &prelit);
	int32 why = ps3DarkClass[id] - 1;
	if(why == PS3DARK_OK || why == PS3DARK_UNKNOWN)
		return;
	ps3DarkDrawn++;
	for(int32 i = 0; i < ps3NumDarkSeen; i++)
		if(ps3DarkSeen[i].model == id)
			return;
	if(ps3NumDarkSeen < (int32)ARRAY_SIZE(ps3DarkSeen)){
		PS3DarkSeen &d = ps3DarkSeen[ps3NumDarkSeen++];
		d.model = id;
		d.why = why;
		d.lod = e->bIsBIGBuilding;
		d.prelit = prelit;
		d.dist = (e->GetPosition() - TheCamera.GetPosition()).Magnitude();
	}
}

void
PS3_DarkReport(void)
{
	PS3_Logf("[dark] in 10 s: %d building draws, %d of them of a model that can only come out dark; %d models%s",
	         ps3BuildingsDrawn, ps3DarkDrawn, ps3NumDarkSeen, ps3NumDarkSeen ? ":" : "");
	for(int32 i = 0; i < ps3NumDarkSeen; i++){
		PS3DarkSeen &d = ps3DarkSeen[i];
		CBaseModelInfo *mi = CModelInfo::GetModelInfo(d.model);
		PS3_Logf("[dark]   %d %s (%s) at %.0f m: %s%s", d.model, mi ? mi->GetModelName() : "?", d.lod ? "LOD" : "detailed",
		         d.dist, ps3DarkWhy[d.why], d.why == PS3DARK_PRELIT ? (d.prelit == 0 ? " (all 0)" : " (average under 12)") : "");
	}
	ps3NumDarkSeen = ps3DarkDrawn = ps3BuildingsDrawn = 0;
}

void
PS3_LodReport(void)
{
	const int32 *c = ps3BB[1];
	PS3_Logf("[lod] LOD buildings on the last frame: drawn %d, fading %d, draw last %d, hidden for their detailed model %d, "
	         "kept (detailed one not drawn) %d, off screen %d, occluded %d, invisible %d, too far %d, streaming %d, "
	         "time %d, area %d, planes %d",
	         c[PS3BB_VISIBLE], c[PS3BB_FADING], c[PS3BB_DRAWLAST], c[PS3BB_HIGH_DRAWN], c[PS3BB_KEPT],
	         c[PS3BB_OFFSCREEN], c[PS3BB_OCCLUDED], c[PS3BB_HIDDEN], c[PS3BB_TOOFAR], c[PS3BB_STREAM],
	         c[PS3BB_TIME], c[PS3BB_AREA], c[PS3BB_PLANE]);
	PS3_Logf("[lod]   detailed models with a LOD: drawn %d, fading %d, off screen %d, occluded %d, not loaded or out of range %d; "
	         "far clip %.0f, fog from %.0f, LOD x%.2f (draw distance %.2f), camera at %.0f %.0f %.0f%s%s",
	         c[PS3HI_VISIBLE], c[PS3HI_FADING], c[PS3HI_OFFSCREEN], c[PS3HI_OCCLUDED], c[PS3HI_NOTLOADED],
	         TheCamera.m_pRwCamera ? RwCameraGetFarClipPlane(TheCamera.m_pRwCamera) : 0.0f, CTimeCycle::GetFogStart(),
	         TheCamera.LODDistMultiplier, CRenderer::ms_lodDistScale,
	         TheCamera.GetPosition().x, TheCamera.GetPosition().y, TheCamera.GetPosition().z,
	         gPS3LodPairs ? "" : ", pairs OFF", gPS3NoOcclusion ? ", occlusion OFF" : "");
	PS3_Logf("[lod]   hidden for their detailed model by the game's rule: not paired %d, model with more than 4 instances drawn %d",
	         c[PS3BB_HIDE_UNPAIRED], c[PS3BB_HIDE_MANY]);
	PS3_WeatherReport();
	PS3_DarkReport();
	PS3_ShadReport();
	{
		extern void PS3_CoronaReport(void);	// Coronas.cpp (patch 107)
		PS3_CoronaReport();
		extern void PS3_ShadowPoolReport(void);	// Shadows.cpp (patch 108)
		PS3_ShadowPoolReport();
	}
}
#define PS3_HI(r) do { if(ps3HasLod[ent->GetModelIndex()]) ps3BB[0][r]++; } while(0)
#else
#define PS3_BB(r)
#define PS3_HI(r)
#endif

void
CRenderer::RenderOneRoad(CEntity *e)
{
	PS3_RENDER_ENTITY("render road: model|type<<16", e);
#ifndef MASTER
	if(gbDontRenderBuildings)
		return;
	if(gbShowCollisionPolys || gbShowCollisionPolysReflections || gbShowCollisionPolysNoShadows)
		CCollision::DrawColModel_Coloured(e->GetMatrix(), *CModelInfo::GetColModel(e->GetModelIndex()), e->GetModelIndex());
	else
#endif
	{
		PUSH_RENDERGROUP(CModelInfo::GetModelInfo(e->GetModelIndex())->GetModelName());

#ifdef __PS3__
		if(PS3_logsOn)
			PS3_NoteBuildingDrawn(e);	// patch 78: "[dark]"
		rw::ps3::decalRoad = true;	// patch 95: its overlays go through the decal test modes
#endif
		e->Render();
#ifdef __PS3__
		rw::ps3::decalRoad = false;
#endif

		POP_RENDERGROUP();
	}
}

void
CRenderer::RenderOneNonRoad(CEntity *e)
{
	CPed *ped;
	CVehicle *veh;
	int i;
	bool resetLights;

	PS3_RENDER_ENTITY("render: model|type<<16", e);
#ifndef MASTER
	if(gbShowCollisionPolys || gbShowCollisionPolysReflections || gbShowCollisionPolysNoShadows){
		if(!e->IsVehicle()){
			CCollision::DrawColModel_Coloured(e->GetMatrix(), *CModelInfo::GetColModel(e->GetModelIndex()), e->GetModelIndex());
			return;
		}
	}else
#endif
#ifndef FINAL
	if(e->IsBuilding()){
		if(e->bIsBIGBuilding){
			if(gbDontRenderBigBuildings)
				return;
		}else{
			if(gbDontRenderBuildings)
				return;
		}
	}else
#endif
	if(e->IsPed()){
#ifndef FINAL
		if(gbDontRenderPeds)
			return;
#endif
		ped = (CPed*)e;
		if(ped->m_nPedState == PED_DRIVING)
			return;
	}
#ifndef FINAL
	else if(e->IsObject() || e->IsDummy()){
		if(gbDontRenderObjects)
			return;
	}else if(e->IsVehicle()){
		// re3 addition
		if(gbDontRenderVehicles)
			return;
	}
#endif

	PUSH_RENDERGROUP(CModelInfo::GetModelInfo(e->GetModelIndex())->GetModelName());

	resetLights = e->SetupLighting();

	if(e->IsVehicle()){
		// unfortunately can't use GetClump here
		CVisibilityPlugins::SetupVehicleVariables((RpClump*)e->m_rwObject);
		CVisibilityPlugins::InitAlphaAtomicList();
	}

	// Render Peds in vehicle before vehicle itself
	if(e->IsVehicle()){
		veh = (CVehicle*)e;
#ifdef VIS_DISTANCE_ALPHA
		int vehalpha = CVisibilityPlugins::GetObjectDistanceAlpha(veh->m_rwObject);
#endif
		if(veh->pDriver && veh->pDriver->m_nPedState == PED_DRIVING){
			PS3_CRUMB("render vehicle: driver model", veh->pDriver->GetModelIndex());
#ifdef VIS_DISTANCE_ALPHA
			int alpha = CVisibilityPlugins::GetObjectDistanceAlpha(veh->pDriver->m_rwObject);
			CVisibilityPlugins::SetObjectDistanceAlpha(veh->pDriver->m_rwObject, vehalpha);
			veh->pDriver->Render();
			CVisibilityPlugins::SetObjectDistanceAlpha(veh->pDriver->m_rwObject, alpha);
#else
			veh->pDriver->Render();
#endif
		}
		for(i = 0; i < 8; i++)
			if(veh->pPassengers[i] && veh->pPassengers[i]->m_nPedState == PED_DRIVING){
				PS3_CRUMB("render vehicle: passenger", i);
#ifdef VIS_DISTANCE_ALPHA
				int alpha = CVisibilityPlugins::GetObjectDistanceAlpha(veh->pPassengers[i]->m_rwObject);
				CVisibilityPlugins::SetObjectDistanceAlpha(veh->pPassengers[i]->m_rwObject, vehalpha);
				veh->pPassengers[i]->Render();
				CVisibilityPlugins::SetObjectDistanceAlpha(veh->pPassengers[i]->m_rwObject, alpha);
#else
				veh->pPassengers[i]->Render();
#endif
			}
		SetCullMode(rwCULLMODECULLNONE);
		PS3_CRUMB("render vehicle: the vehicle", e->GetModelIndex());
	}
#ifdef __PS3__
	if(PS3_logsOn && e->IsBuilding())
		PS3_NoteBuildingDrawn(e);	// patch 78: "[dark]"
#endif
	e->Render();

	if(e->IsVehicle()){
		// TODO(LCS): LCS does not use bImBeingRendered, keeping it for safety
		e->bImBeingRendered = true;
		PS3_CRUMB("render vehicle: alpha atomics", e->GetModelIndex());
		CVisibilityPlugins::RenderAlphaAtomics();
		e->bImBeingRendered = false;
		SetCullMode(rwCULLMODECULLBACK);
	}

	e->RemoveLighting(resetLights);

	POP_RENDERGROUP();
}

void
CRenderer::RenderFirstPersonVehicle(void)
{
	if(m_pFirstPersonVehicle == nil)
		return;
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RenderOneNonRoad(m_pFirstPersonVehicle);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
}

inline bool IsRoad(CEntity *e) { return e->IsBuilding() && ((CSimpleModelInfo*)CModelInfo::GetModelInfo(e->GetModelIndex()))->m_wetRoadReflection; }

void
CRenderer::RenderRoads(void)
{
	int i;
	CEntity *e;

	PUSH_RENDERGROUP("CRenderer::RenderRoads");
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
//	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	SetCullMode(rwCULLMODECULLBACK);
	DeActivateDirectional();
	SetAmbientColours();

	for(i = 0; i < ms_nNoOfVisibleEntities; i++){
		e = ms_aVisibleEntityPtrs[i];
		if(IsRoad(e))
			RenderOneRoad(e);
	}
	POP_RENDERGROUP();
}

inline bool PutIntoSortedVehicleList(CVehicle *veh)
{
	if(veh->IsBoat()){
		int mode = TheCamera.Cams[TheCamera.ActiveCam].Mode;
		if(mode == CCam::MODE_WHEELCAM ||
		   mode == CCam::MODE_1STPERSON && TheCamera.GetLookDirection() != LOOKING_FORWARD && TheCamera.GetLookDirection() != LOOKING_BEHIND ||
		   CVisibilityPlugins::GetClumpAlpha(veh->GetClump()) != 255)
			return false;
		return true;
	}else
		return veh->bTouchingWater;		
}

// this only renders objects in LCS

void
CRenderer::RenderEverythingBarRoads(void)
{
	int i;
	CEntity *e;

	PUSH_RENDERGROUP("CRenderer::RenderEverythingBarRoads");
#ifdef __PS3__
	// patch 80: reLCS had this commented out ("this only renders objects in
	// LCS"), but every building that isn't a wet road, and every LOD, is
	// drawn here. Fog stayed on from RenderRoads, except in the rain:
	// CCoronas::RenderReflections (only with WetRoads) turns it off and
	// doesn't turn it back on. So in the rain the world had no fog: the far
	// clip (650 m) cut buildings in the open (Ren: pop-in at the draw
	// distance only with weather 2) and dark LODs stayed black instead of
	// fading into the fog. reVC turns it on here.
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
#else
//	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
#endif
//	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	SetCullMode(rwCULLMODECULLBACK);
//	gSortedVehiclesAndPeds.Clear();

	for(i = 0; i < ms_nNoOfVisibleEntities; i++){
		e = ms_aVisibleEntityPtrs[i];

		if(IsRoad(e))
			continue;

#ifdef EXTENDED_PIPELINES
		if(CustomPipes::bRenderingEnvMap && (e->IsPed() || e->IsVehicle()))
			continue;
#endif

		// we're not even rendering peds here....
#ifdef VIS_DISTANCE_ALPHA
		// this looks like a fix for objects just popping in
		int distAlpha = CVisibilityPlugins::GetObjectDistanceAlpha(e->m_rwObject);
		if(e->IsPed() && CVisibilityPlugins::GetClumpAlpha((RpClump*)e->m_rwObject) != 255 ||
		   distAlpha != 255){
			if(distAlpha != 0 && !CVisibilityPlugins::InsertEntityIntoSortedList(e, (ms_vecCameraPosition - e->GetPosition()).Magnitude())){
#else
		if(e->IsPed() && CVisibilityPlugins::GetClumpAlpha((RpClump*)e->m_rwObject) != 255){
			if(!CVisibilityPlugins::InsertEntityIntoSortedList(e, (ms_vecCameraPosition - e->GetPosition()).Magnitude())){
#endif
				printf("Ran out of space in alpha entity list");
				RenderOneNonRoad(e);
			}
		}else
			RenderOneNonRoad(e);
	}
	POP_RENDERGROUP();
}

void
CRenderer::RenderBoats(void)
{
	int i;
	CEntity *e;
	EntityInfo ei;
	CLink<EntityInfo> *node;

	PUSH_RENDERGROUP("CRenderer::RenderBoats");
	gbLighting = true;
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
//	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	SetCullMode(rwCULLMODECULLBACK);

	gSortedVehiclesAndPeds.Clear();
	for(i = 0; i < ms_nNoOfVisibleVehicles; i++){
		e = ms_aVisibleVehiclePtrs[i];
		if(e->IsVehicle() && PutIntoSortedVehicleList((CVehicle*)e)){
			ei.ent = e;
			ei.sort = (ms_vecCameraPosition - e->GetPosition()).MagnitudeSqr();
			gSortedVehiclesAndPeds.InsertSorted(ei);
		}
	}

	for(node = gSortedVehiclesAndPeds.tail.prev;
	    node != &gSortedVehiclesAndPeds.head;
	    node = node->prev){
		CVehicle *v = (CVehicle*)node->item.ent;
		RenderOneNonRoad(v);
	}
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	gbLighting = false;
	POP_RENDERGROUP();
}

// also renders peds
void
CRenderer::RenderVehicles(void)
{
	int i;
	CEntity *e;

	PUSH_RENDERGROUP("CRenderer::RenderVehicles");
	// LCS: not on android
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	// TODO(LCS): PS2VehicleAlphaFunc();

	gbLighting = true;
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);

	CVisibilityPlugins::InitAlphaEntityList();
	for(i = 0; i < ms_nNoOfVisibleVehicles; i++){
		e = ms_aVisibleVehiclePtrs[i];
#ifdef VIS_DISTANCE_ALPHA
		if(CVisibilityPlugins::GetClumpAlpha((RpClump*)e->m_rwObject) == 0 ||
		   CVisibilityPlugins::GetObjectDistanceAlpha(e->m_rwObject) == 0)
			continue;
#endif

		int behindDriver = e->bIsPed && ((CPed*)e)->m_nPedState == PED_DRIVING &&
				TheCamera.GetLookDirection() == LOOKING_FORWARD;
		// what is going on here? !behindDriver will always be true because we're checking for !PED_DRIVING
		if(!e->bDistanceFade && (e->IsPed() || e->bIsPed) && ((CPed*)e)->m_nPedState != PED_DRIVING && !behindDriver){
#ifdef VIS_DISTANCE_ALPHA
			if(CVisibilityPlugins::GetClumpAlpha((RpClump*)e->m_rwObject) != 255 ||
			   CVisibilityPlugins::GetObjectDistanceAlpha(e->m_rwObject) != 255)
				;	// set blend render states
			else
				;	// set default render states
#endif
			RenderOneNonRoad(e);
		}else if(!PutIntoSortedVehicleList((CVehicle*)e) &&	// boats handled elsewhere
		   !CVisibilityPlugins::InsertEntityIntoSortedList(e, (ms_vecCameraPosition - e->GetPosition()).Magnitude())){
#ifdef VIS_DISTANCE_ALPHA
			if(CVisibilityPlugins::GetClumpAlpha((RpClump*)e->m_rwObject) != 255 ||
			   CVisibilityPlugins::GetObjectDistanceAlpha(e->m_rwObject) != 255)
				;	// set blend render states
			else
				;	// set default render states
#endif
			printf("Ran out of space in alpha entity list");
			RenderOneNonRoad(e);
		}
	}
	PS3_CRUMB("render vehicles: the alpha entity list", 0);
	CVisibilityPlugins::RenderFadingEntities();

	PS3_CRUMB("render vehicles: CFerry::RenderAllRemaning", 0);
	CFerry::RenderAllRemaning();
	PS3_CRUMB("render vehicles: CPlane::RenderAllRemaning", 0);
	CPlane::RenderAllRemaning();
	// TODO(LCS): gpGlobalEnvironmentMap = nil;
	// TODO(LCS): CMattRenderer::Get().ResetRenderStates();
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	gbLighting = false;
	POP_RENDERGROUP();
}

static void
SetStencilState(int state)
{
	switch(state){
	// disable stencil
	case 0:
		rw::SetRenderState(rw::STENCILENABLE, FALSE);
		break;
	// test against stencil
	case 1:
		rw::SetRenderState(rw::STENCILENABLE, TRUE);
		rw::SetRenderState(rw::STENCILFUNCTION, rw::STENCILNOTEQUAL);
		rw::SetRenderState(rw::STENCILPASS, rw::STENCILKEEP);
		rw::SetRenderState(rw::STENCILFAIL, rw::STENCILKEEP);
		rw::SetRenderState(rw::STENCILZFAIL, rw::STENCILKEEP);
		rw::SetRenderState(rw::STENCILFUNCTIONMASK, 0xFF);
		rw::SetRenderState(rw::STENCILFUNCTIONREF, 0xFF);
		break;
	// write to stencil
	case 2:
		rw::SetRenderState(rw::STENCILENABLE, TRUE);
		rw::SetRenderState(rw::STENCILFUNCTION, rw::STENCILALWAYS);
		rw::SetRenderState(rw::STENCILPASS, rw::STENCILREPLACE);
		rw::SetRenderState(rw::STENCILFUNCTIONREF, 0xFF);
		break;
	}
}

#ifdef NEW_RENDERER
#ifndef LIBRW
#error "Need librw for EXTENDED_PIPELINES"
#endif

enum {
	// blend passes
	PASS_NOZ,	// no z-write
	PASS_ADD,	// additive
	PASS_BLEND	// normal blend
};


void
CRenderer::RenderOneBuilding(CEntity *ent, float camdist)
{
	if(ent->m_rwObject == nil)
		return;

	ent->bImBeingRendered = true;	// TODO: this seems wrong, but do we even need it?

	assert(RwObjectGetType(ent->m_rwObject) == rpATOMIC);
	RpAtomic *atomic = (RpAtomic*)ent->m_rwObject;
	CSimpleModelInfo *mi = (CSimpleModelInfo*)CModelInfo::GetModelInfo(ent->GetModelIndex());

	int pass = PASS_BLEND;
	if(mi->m_additive)	// very questionable
		pass = PASS_ADD;
	if(mi->m_noZwrite)
		pass = PASS_NOZ;

	if(ent->bDistanceFade){
		RpAtomic *lodatm;
		float fadefactor;
		uint32 alpha;

		lodatm = mi->GetAtomicFromDistance(camdist - FADE_DISTANCE);
		fadefactor = (mi->GetLargestLodDistance() - (camdist - FADE_DISTANCE))/FADE_DISTANCE;
		if(fadefactor > 1.0f)
			fadefactor = 1.0f;
		alpha = mi->m_alpha * fadefactor;

		if(alpha == 255)
			WorldRender::AtomicFirstPass(atomic, pass);
		else{
			// not quite sure what this is about, do we have to do that?
			RpGeometry *geo = RpAtomicGetGeometry(lodatm);
			if(geo != RpAtomicGetGeometry(atomic))
				RpAtomicSetGeometry(atomic, geo, rpATOMICSAMEBOUNDINGSPHERE);
			WorldRender::AtomicFullyTransparent(atomic, pass, alpha);
		}
	}else
		WorldRender::AtomicFirstPass(atomic, pass);

	ent->bImBeingRendered = false;	// TODO: this seems wrong, but do we even need it?
}

// our replacement for cWorldStream::Render
void
CRenderer::RenderWorld(int pass)
{
	int i;
	CEntity *e;
	CLink<CVisibilityPlugins::AlphaObjectInfo> *node;
	// use old renderer
	if(gbPreviewCity)
		return;

	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	SetCullMode(rwCULLMODECULLBACK);
	DeActivateDirectional();
	SetAmbientColours();

	// Temporary...have to figure out sorting better
	switch(pass){
	case 0:
		// Roads
		PUSH_RENDERGROUP("CRenderer::RenderWorld - Roads");
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
		for(i = 0; i < ms_nNoOfVisibleBuildings; i++){
			e = ms_aVisibleBuildingPtrs[i];
			if(e->bIsBIGBuilding || IsRoad(e))
				RenderOneBuilding(e);
		}
		for(node = CVisibilityPlugins::m_alphaBuildingList.tail.prev;
		    node != &CVisibilityPlugins::m_alphaBuildingList.head;
		    node = node->prev){
			e = node->item.entity;
			if(e->bIsBIGBuilding || IsRoad(e))
				RenderOneBuilding(e, node->item.sort);
		}
		POP_RENDERGROUP();
		break;
	case 1:
		// Opaque
		PUSH_RENDERGROUP("CRenderer::RenderWorld - Opaque");
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
		for(i = 0; i < ms_nNoOfVisibleBuildings; i++){
			e = ms_aVisibleBuildingPtrs[i];
			if(!(e->bIsBIGBuilding || IsRoad(e)))
				RenderOneBuilding(e);
		}
		for(node = CVisibilityPlugins::m_alphaBuildingList.tail.prev;
		    node != &CVisibilityPlugins::m_alphaBuildingList.head;
		    node = node->prev){
			e = node->item.entity;
			if(!(e->bIsBIGBuilding || IsRoad(e)))
				RenderOneBuilding(e, node->item.sort);
		}
		// Now we have iterated through all visible buildings (unsorted and sorted)
		// and the transparency list is done.

		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, FALSE);
		WorldRender::RenderBlendPass(PASS_NOZ);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
		POP_RENDERGROUP();
		break;
	case 2:
		// Transparent
		PUSH_RENDERGROUP("CRenderer::RenderWorld - Transparent");
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
		WorldRender::RenderBlendPass(PASS_ADD);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
		WorldRender::RenderBlendPass(PASS_BLEND);
		POP_RENDERGROUP();
		break;
	}
}
#endif

// actually CMattRenderer::RenderWater
// adapted slightly
void
CRenderer::RenderWater(void)
{
	int i;
	CEntity *e;

	PUSH_RENDERGROUP("CRenderer::RenderWater");

	gbLighting = false;
	CWaterLevel::RenderWater();

	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nil);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDZERO);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	SetStencilState(2);

	for(i = 0; i < ms_nNoOfVisibleVehicles; i++){
		e = ms_aVisibleVehiclePtrs[i];
		if(e->IsVehicle() && ((CVehicle*)e)->IsBoat())
			((CBoat*)e)->RenderWaterOutPolys();
	}

	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	SetStencilState(1);

	CWaterLevel::RenderTransparentWater();

	SetStencilState(0);
	gbLighting = true;
	POP_RENDERGROUP();
}

void
CRenderer::ClearForFrame(void)
{
#ifdef __PS3__
	if(!ps3LodPairsBuilt)
		PS3_BuildLodPairs();
	PS3_ShadEndFrame();	// patch 91
	ps3VisFrame++;
	memcpy(ps3BB[1], ps3BB[0], sizeof(ps3BB[0]));
	memset(ps3BB[0], 0, sizeof(ps3BB[0]));
#endif
	ms_nNoOfVisibleEntities = 0;
	ms_nNoOfVisibleVehicles = 0;
	ms_nNoOfInVisibleEntities = 0;
	gSortedVehiclesAndPeds.Clear();

#ifdef NEW_RENDERER
	ms_nNoOfVisibleBuildings = 0;
	WorldRender::numBlendInsts[PASS_NOZ] = 0;
	WorldRender::numBlendInsts[PASS_ADD] = 0;
	WorldRender::numBlendInsts[PASS_BLEND] = 0;
#endif
}

void
CRenderer::RenderFadingInEntities(void)
{
	PUSH_RENDERGROUP("CRenderer::RenderFadingInEntities");
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
//	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	SetCullMode(rwCULLMODECULLBACK);
	DeActivateDirectional();
	SetAmbientColours();
	CVisibilityPlugins::RenderFadingEntities();
	POP_RENDERGROUP();
}

void
CRenderer::RenderFadingInUnderwaterEntities(void)
{
	PUSH_RENDERGROUP("CRenderer::RenderFadingInUnderwaterEntities");
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
//	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	SetCullMode(rwCULLMODECULLBACK);
	DeActivateDirectional();
	SetAmbientColours();
	CVisibilityPlugins::RenderFadingUnderwaterEntities();
	POP_RENDERGROUP();
}

void
CRenderer::RenderCollisionLines(void)
{
	int i;

	// game doesn't draw fading in entities
	// this should probably be fixed
	for(i = 0; i < ms_nNoOfVisibleEntities; i++){
		CEntity *e = ms_aVisibleEntityPtrs[i];
		if(Abs(e->GetPosition().x - ms_vecCameraPosition.x) < 100.0f &&
		   Abs(e->GetPosition().y - ms_vecCameraPosition.y) < 100.0f)
			CCollision::DrawColModel(e->GetMatrix(), *e->GetColModel());
	}
}

// enum Visbility: moved up (patch 91)

// Time Objects can be time culled if
//   other == -1 || CModelInfo::GetModelInfo(other)->GetRwObject()
// i.e. we have to draw even at the wrong time if
//   other != -1 && CModelInfo::GetModelInfo(other)->GetRwObject() == nil

#define OTHERUNAVAILABLE (other != -1 && CModelInfo::GetModelInfo(other)->GetRwObject() == nil)
#define CANTIMECULL (!OTHERUNAVAILABLE)

int32
CRenderer::SetupEntityVisibility(CEntity *ent)
{
	CSimpleModelInfo *mi = (CSimpleModelInfo*)CModelInfo::GetModelInfo(ent->m_modelIndex);
	CTimeModelInfo *ti;
	int32 other;
	float dist;

	bool request = true;
	if(mi->GetModelType() == MITYPE_SIMPLE){
		// TODO(LCS): some cWorldStream dynamics stuff
	}
	if(mi->GetModelType() == MITYPE_TIME){
 		ti = (CTimeModelInfo*)mi;
		other = ti->GetOtherTimeModel();
		if(CClock::GetIsTimeInRange(ti->GetTimeOn(), ti->GetTimeOff())){
			// don't fade in, or between time objects
			if(CANTIMECULL)
				ti->m_alpha = 255;
		}else{
			// Hide if possible
			if(CANTIMECULL){
				ent->DeleteRwObject();
				return VIS_INVISIBLE;
			}
			// can't cull, so we'll try to draw this one, but don't request
			// it since what we really want is the other one.
			request = false;
		}
	}else{
		if(mi->GetModelType() != MITYPE_SIMPLE && mi->GetModelType() != MITYPE_WEAPON){
			if(FindPlayerVehicle() == ent &&
			   TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_1STPERSON &&
// TODO(LCS): something in CWorld::Players...
			   !(FindPlayerVehicle()->IsBike() && ((CBike*)FindPlayerVehicle())->bWheelieCam)){
				// Player's vehicle in first person mode
				CVehicle *veh = (CVehicle*)ent;
				int model = veh->GetModelIndex();
				int direction = TheCamera.Cams[TheCamera.ActiveCam].DirectionWasLooking;
				if(direction == LOOKING_FORWARD ||
				   ent->GetModelIndex() == MI_RHINO ||
				   ent->GetModelIndex() == MI_BUS ||
				   ent->GetModelIndex() == MI_COACH ||
				   TheCamera.m_bInATunnelAndABigVehicle ||
				   direction == LOOKING_BEHIND && veh->pHandling->Flags & HANDLING_UNKNOWN){
					ent->bNoBrightHeadLights = true;
					return VIS_OFFSCREEN;
				}

				if(direction != LOOKING_BEHIND ||
				   !veh->IsBoat() || model == MI_REEFER || model == MI_PREDATOR){
					m_pFirstPersonVehicle = (CVehicle*)ent;
					ent->bNoBrightHeadLights = false;
					return VIS_OFFSCREEN;
				}
			}

			// All sorts of Clumps
			if(ent->m_rwObject == nil || !ent->bIsVisible)
				return VIS_INVISIBLE;
			if(!ent->GetIsOnScreen() || ent->IsEntityOccluded())
				return VIS_OFFSCREEN;
			if(ent->bDrawLast){
				dist = (ent->GetPosition() - ms_vecCameraPosition).Magnitude();
				CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
				ent->bDistanceFade = false;
				return VIS_INVISIBLE;
			}
			return VIS_VISIBLE;
		}
		if(ent->bDontStream){
			if(ent->m_rwObject == nil || !ent->bIsVisible)
				return VIS_INVISIBLE;
			if(!ent->GetIsOnScreen() || ent->IsEntityOccluded())
				return VIS_OFFSCREEN;
			if(ent->bDrawLast){
				dist = (ent->GetPosition() - ms_vecCameraPosition).Magnitude();
				CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
				ent->bDistanceFade = false;
				return VIS_INVISIBLE;
			}
			return VIS_VISIBLE;
		}
	}

	// Simple ModelInfo

	if(!IsAreaVisible(ent->m_area))
		return VIS_INVISIBLE;

	dist = (ent->GetPosition() - ms_vecCameraPosition).Magnitude();

//#ifndef FIX_BUGS
#if 0
	// Whatever this is supposed to do, it breaks fading for objects
	// whose draw dist is > LOD_DISTANCE-FADE_DISTANCE, i.e. 280
	// because decreasing dist here makes the object visible above LOD_DISTANCE
	// before fading normally once below LOD_DISTANCE.
	// aha! this must be a workaround for the fact that we're not taking
	// the LOD multiplier into account here anywhere
	if(LOD_DISTANCE < dist && dist < mi->GetLargestLodDistance() + FADE_DISTANCE)
		dist += mi->GetLargestLodDistance() - LOD_DISTANCE;
#endif
	// LCS has this now, wonder if it's any better:
	if(LOD_DISTANCE + STREAM_DISTANCE < dist && dist < mi->GetLargestLodDistance())
		dist = mi->GetLargestLodDistance();

	if(ent->IsObject() && ent->bRenderDamaged)
		mi->m_isDamaged = true;

	RpAtomic *a = mi->GetAtomicFromDistance(dist);
#ifdef __PS3__
	// patch 125: a model with several detail levels (atomics by distance)
	// picks one by the distance to its pivot. A long road piece's pivot can be
	// 70 m from where Toni stands on it (FS_custroadA1: pivot at 294 -307, the
	// wedge at 247 -250): its far level, low detail with dark prelight, was
	// drawn under his feet until he got near the pivot. The level is chosen
	// by the distance to the model's bounds (0 inside them) when that gives a
	// more detailed one; whether it's drawn at all stays the game's rule.
	// USRDIR/noboundlod.txt: the pivot, as before
	if(a && gPS3BoundLod && mi->m_numAtomics > 1){
		float nd = (ms_vecCameraPosition - ent->GetBoundCentre()).Magnitude() - ent->GetBoundRadius();
		RpAtomic *b = mi->GetAtomicFromDistance(nd > 0.0f ? nd : 0.0f);
		if(b)
			a = b;
	}
#endif
	if(a){
		mi->m_isDamaged = false;
		if(ent->m_rwObject == nil)
			ent->CreateRwObject();
		assert(ent->m_rwObject);
		RpAtomic *rwobj = (RpAtomic*)ent->m_rwObject;
		// Make sure our atomic uses the right geometry and not
		// that of an atomic for another draw distance.
		if(RpAtomicGetGeometry(a) != RpAtomicGetGeometry(rwobj))
			RpAtomicSetGeometry(rwobj, RpAtomicGetGeometry(a), rpATOMICSAMEBOUNDINGSPHERE); // originally 5 (mistake?)
		mi->IncreaseAlpha();
		if(ent->m_rwObject == nil || !ent->bIsVisible)
			return VIS_INVISIBLE;

#ifdef __PS3__
		if(!ent->GetIsOnScreen() || ent->IsEntityOccluded()){
			PS3_HI(!ent->GetIsOnScreen() ? PS3HI_OFFSCREEN : PS3HI_OCCLUDED);
#else
		if(!ent->GetIsOnScreen() || ent->IsEntityOccluded()){
#endif
			mi->m_alpha = 255;
			return VIS_OFFSCREEN;
		}

		if(mi->m_alpha != 255){
			CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
			ent->bDistanceFade = true;
			PS3_HI(PS3HI_FADING);
			return VIS_INVISIBLE;
		}
#ifdef __PS3__
		// patch 28: drawn from here on (opaque, or in the sorted list)
		PS3_HI(PS3HI_VISIBLE);
		PS3_MarkHighDrawn(ent);
#endif

#ifdef __PS3__
		// patch 97: the road shadows of Staunton (pm_treeshad_*, flag noZwrite)
		// are meshes laid on the ground, partly under it. LCS draws noZwrite
		// models after the opaque world, without depth writes (Leeds' PASS_NOZ,
		// reLCS's NEW_RENDERER, which is off on the PS3). Here they were drawn
		// with the opaque buildings, in the order the sector scan found them,
		// which follows the camera: drawn before the road under them, the road
		// painted over them (Ren: the shadow comes and goes with the camera
		// angle; hiding "shad" in patch 96's mode 4 took it away for good, no
		// depth bias or test brought it back). The sorted list draws them
		// after the opaque world and RenderFadingEntities turns off the depth
		// writes for noZwrite.
		if(mi->m_drawLast || ent->bDrawLast || mi->m_noZwrite){
#else
		if(mi->m_drawLast || ent->bDrawLast){
#endif
			if(CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist)){
				ent->bDistanceFade = false;
				return VIS_INVISIBLE;
			}
		}
		return VIS_VISIBLE;
	}
#ifdef __PS3__
	PS3_HI(PS3HI_NOTLOADED);
#endif

	// Object is not loaded, figure out what to do

	if(mi->m_noFade){
		mi->m_isDamaged = false;
		// request model
		if(dist - STREAM_DISTANCE < mi->GetLargestLodDistance() && request)
			return VIS_STREAMME;
		return VIS_INVISIBLE;
	}

	// We might be fading

	a = mi->GetLastAtomic(dist - FADE_DISTANCE);
	mi->m_isDamaged = false;
	if(a == nil){
		// request model
		if(dist - FADE_DISTANCE - STREAM_DISTANCE < mi->GetLargestLodDistance() && request)
			return VIS_STREAMME;
		return VIS_INVISIBLE;
	}

	if(ent->m_rwObject == nil)
		ent->CreateRwObject();
	assert(ent->m_rwObject);
	RpAtomic *rwobj = (RpAtomic*)ent->m_rwObject;
	if(RpAtomicGetGeometry(a) != RpAtomicGetGeometry(rwobj))
		RpAtomicSetGeometry(rwobj, RpAtomicGetGeometry(a), rpATOMICSAMEBOUNDINGSPHERE); // originally 5 (mistake?)
	mi->IncreaseAlpha();
	if(ent->m_rwObject == nil || !ent->bIsVisible)
		return VIS_INVISIBLE;

	if(!ent->GetIsOnScreen() || ent->IsEntityOccluded()){
		mi->m_alpha = 255;
		return VIS_OFFSCREEN;
	}else{
		CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
		ent->bDistanceFade = true;
		return VIS_OFFSCREEN;	// Why this?
	}
}

int32
CRenderer::SetupBigBuildingVisibility(CEntity *ent)
{
	CSimpleModelInfo *mi = (CSimpleModelInfo*)CModelInfo::GetModelInfo(ent->m_modelIndex);
	CTimeModelInfo *ti;
	int32 other;

	if(!IsAreaVisible(ent->m_area)){
		PS3_BB(PS3BB_AREA);
		return VIS_INVISIBLE;
	}
#ifdef __PS3__
	// patch 126: test mode 18, no LOD (big building) drawn at all: holes where
	// nothing detailed is, the far city gone. Does the road wedge go with them?
	if(PS3_logsOn && rw::ps3::decalMode == 18){
		PS3_BB(PS3BB_HIDDEN);
		return VIS_INVISIBLE;
	}
#endif

	bool request = true;
	if(mi->GetModelType() == MITYPE_TIME){
		ti = (CTimeModelInfo*)mi;
		other = ti->GetOtherTimeModel();
		if(CClock::GetIsTimeInRange(ti->GetTimeOn(), ti->GetTimeOff())){
			// don't fade in, or between time objects
			if(CANTIMECULL)
				ti->m_alpha = 255;
		}else{
			// Hide if possible
			if(CANTIMECULL){
				ent->DeleteRwObject();
				PS3_BB(PS3BB_TIME);
				return VIS_INVISIBLE;
			}
			// can't cull, so we'll try to draw this one, but don't request
			// it since what we really want is the other one.
			request = false;
		}
	}else if(mi->GetModelType() == MITYPE_VEHICLE){
#ifdef __PS3__
		// The planes (CPlane: airtrain, the chopper and deaddodo of the flight
		// paths) live in the big building list. Far away they swap their model
		// for the LOD of the IDE, and LCS's chopper and deaddodo have none: no
		// model at all, drawn anyway, and SetupVehicleVariables(nil) killed the
		// game thread (patch 22-24 logs). As InsertEntityIntoList: no model,
		// nothing to draw.
		PS3_BB(PS3BB_PLANE);
		return ent->IsVisible() && ent->m_rwObject ? VIS_VISIBLE : VIS_INVISIBLE;
#else
		return ent->IsVisible() ? VIS_VISIBLE : VIS_INVISIBLE;
#endif
	}

	float dist = (ms_vecCameraPosition-ent->GetPosition()).Magnitude();
	CSimpleModelInfo *nonLOD = mi->GetRelatedModel();

	// Find out whether to draw below near distance.
	// This is only the case if there is a non-LOD which is either not
	// loaded or not completely faded in yet.
	if(dist < mi->GetNearDistance() && dist < LOD_DISTANCE + STREAM_DISTANCE){
		// No non-LOD or non-LOD is completely visible.
#ifdef __PS3__
		// patch 28: and the detailed instance at this place was drawn
		if(nonLOD == nil){
			PS3_BB(PS3BB_HIGH_DRAWN);
			return VIS_INVISIBLE;
		}
		if(nonLOD->GetRwObject() && nonLOD->m_alpha == 255){
			if(PS3_LodMayHide(ent)){
				PS3_BB(PS3BB_HIGH_DRAWN);
				return VIS_INVISIBLE;
			}
			PS3_BB(PS3BB_KEPT);
		}
#else
		if(nonLOD == nil ||
		   nonLOD->GetRwObject() && nonLOD->m_alpha == 255)
			return VIS_INVISIBLE;
#endif

		// But if it is a time object, we'd rather draw the wrong
		// non-LOD than the right LOD.
		if(nonLOD->GetModelType() == MITYPE_TIME){
			ti = (CTimeModelInfo*)nonLOD;
			other = ti->GetOtherTimeModel();
			if(other != -1 && CModelInfo::GetModelInfo(other)->GetRwObject())
				return VIS_INVISIBLE;
		}
	}

#ifdef __PS3__
	// patch 96: test mode 7, no LOD near the camera (holes where one was)
	if(rw::ps3::decalMode == 7 && dist < 100.0f){
		PS3_BB(PS3BB_HIDDEN);
		return VIS_INVISIBLE;
	}
	// patch 95: the distance the draw distance is compared with (the fog)
	float lodDist = dist / PS3_LodFogScale(mi);
#else
	float lodDist = dist;
#endif
	RpAtomic *a = mi->GetFirstAtomicFromDistance(lodDist);
	if(a){
		if(ent->m_rwObject == nil)
			ent->CreateRwObject();
		assert(ent->m_rwObject);
		RpAtomic *rwobj = (RpAtomic*)ent->m_rwObject;

		// Make sure our atomic uses the right geometry and not
		// that of an atomic for another draw distance.
		if(RpAtomicGetGeometry(a) != RpAtomicGetGeometry(rwobj))
			RpAtomicSetGeometry(rwobj, RpAtomicGetGeometry(a), rpATOMICSAMEBOUNDINGSPHERE); // originally 5 (mistake?)
		mi->IncreaseAlpha();
#ifdef __PS3__
		if(!ent->IsVisible() || !ent->GetIsOnScreenComplex() || ent->IsEntityOccluded()){
			PS3_BB(!ent->IsVisible() ? PS3BB_HIDDEN : !ent->GetIsOnScreenComplex() ? PS3BB_OFFSCREEN : PS3BB_OCCLUDED);
#else
		if(!ent->IsVisible() || !ent->GetIsOnScreenComplex() || ent->IsEntityOccluded()){
#endif
			mi->m_alpha = 255;
			return VIS_INVISIBLE;
		}

		if(mi->m_alpha != 255){
			CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
			ent->bDistanceFade = true;
			PS3_BB(PS3BB_FADING);
			return VIS_INVISIBLE;
		}

#ifdef __PS3__
		if(mi->m_drawLast || mi->m_noZwrite){	// patch 97: as above
#else
		if(mi->m_drawLast){
#endif
			CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
			ent->bDistanceFade = false;
			PS3_BB(PS3BB_DRAWLAST);
			return VIS_INVISIBLE;
		}
		PS3_BB(PS3BB_VISIBLE);
#ifdef __PS3__
		PS3_NoteNearLod(ent, dist);	// patch 96
#endif
		return VIS_VISIBLE;
	}

	if(mi->m_noFade){
		ent->DeleteRwObject();
		PS3_BB(PS3BB_TOOFAR);
		return VIS_INVISIBLE;
	}


	// get faded atomic
	a = mi->GetFirstAtomicFromDistance(lodDist - FADE_DISTANCE);
	if(a == nil){
		if(ent->bStreamBIGBuilding && lodDist-STREAM_DISTANCE-FADE_DISTANCE < mi->GetLodDistance(0) && request){
			PS3_BB(PS3BB_STREAM);
			return ent->GetIsOnScreen() ? VIS_STREAMME : VIS_INVISIBLE;
		}else{
#ifdef __PS3__
			PS3_FarNoteCut(ent, dist);	// patch 93 (before the RW object goes)
#endif
			ent->DeleteRwObject();
			PS3_BB(PS3BB_TOOFAR);
			return VIS_INVISIBLE;
		}
	}

	// Fade...
	if(ent->m_rwObject == nil)
		ent->CreateRwObject();
	assert(ent->m_rwObject);
	RpAtomic *rwobj = (RpAtomic*)ent->m_rwObject;
	if(RpAtomicGetGeometry(a) != RpAtomicGetGeometry(rwobj))
		RpAtomicSetGeometry(rwobj, RpAtomicGetGeometry(a), rpATOMICSAMEBOUNDINGSPHERE); // originally 5 (mistake?)
	mi->IncreaseAlpha();
	if(!ent->IsVisible() || !ent->GetIsOnScreenComplex() || ent->IsEntityOccluded()){
		mi->m_alpha = 255;
		PS3_BB(PS3BB_OFFSCREEN);
		return VIS_INVISIBLE;
	}
	CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
	ent->bDistanceFade = true;
	PS3_BB(PS3BB_FADING);
	return VIS_INVISIBLE;
}

void
CRenderer::ConstructRenderList(void)
{
	COcclusion::ProcessBeforeRendering();
	ms_vecCameraPosition = TheCamera.GetPosition();

	// unused
	TestCloseThings = 0;
	TestBigThings = 0;

	ScanWorld();

	// LCS: mobile has a bunch of code after this,
}

void
LimitFrustumVector(CVector &vec1, const CVector &vec2, float l)
{
	float f;
	f = (l - vec2.z) / (vec1.z - vec2.z);
	vec1.x = f*(vec1.x - vec2.x) + vec2.x;
	vec1.y = f*(vec1.y - vec2.y) + vec2.y;
	vec1.z = f*(vec1.z - vec2.z) + vec2.z;
}

enum Corners
{
	CORNER_CAM = 0,
	CORNER_FAR_TOPLEFT,
	CORNER_FAR_TOPRIGHT,
	CORNER_FAR_BOTRIGHT,
	CORNER_FAR_BOTLEFT,
	CORNER_LOD_LEFT,
	CORNER_LOD_RIGHT,
	CORNER_PRIO_LEFT,
	CORNER_PRIO_RIGHT,
};

void
CRenderer::ScanWorld(void)
{
	float f = RwCameraGetFarClipPlane(TheCamera.m_pRwCamera);
	RwV2d vw = *RwCameraGetViewWindow(TheCamera.m_pRwCamera);
	CVector vectors[9];
	RwMatrix *cammatrix;
	RwV2d poly[3];

	memset(vectors, 0, sizeof(vectors));
	vectors[CORNER_FAR_TOPLEFT].x = -vw.x * f;
	vectors[CORNER_FAR_TOPLEFT].y = vw.y * f;
	vectors[CORNER_FAR_TOPLEFT].z = f;
	vectors[CORNER_FAR_TOPRIGHT].x = vw.x * f;
	vectors[CORNER_FAR_TOPRIGHT].y = vw.y * f;
	vectors[CORNER_FAR_TOPRIGHT].z = f;
	vectors[CORNER_FAR_BOTRIGHT].x = vw.x * f;
	vectors[CORNER_FAR_BOTRIGHT].y = -vw.y * f;
	vectors[CORNER_FAR_BOTRIGHT].z = f;
	vectors[CORNER_FAR_BOTLEFT].x = -vw.x * f;
	vectors[CORNER_FAR_BOTLEFT].y = -vw.y * f;
	vectors[CORNER_FAR_BOTLEFT].z = f;

	cammatrix = RwFrameGetMatrix(RwCameraGetFrame(TheCamera.m_pRwCamera));

	m_pFirstPersonVehicle = nil;
	CVisibilityPlugins::InitAlphaEntityList();
	CWorld::AdvanceCurrentScanCode();

	// unused
	static CVector prevPos;
	static CVector prevFwd;
	static bool smallMovement;
	smallMovement = (TheCamera.GetPosition() - prevPos).MagnitudeSqr() < SQR(4.0f) &&
		DotProduct(TheCamera.GetForward(), prevFwd) > 0.98f;
	prevPos = TheCamera.GetPosition();
	prevFwd = TheCamera.GetForward();

	if(cammatrix->at.z > 0.0f){
		// looking up, bottom corners are further away
		vectors[CORNER_LOD_LEFT] = vectors[CORNER_FAR_BOTLEFT] * LOD_DISTANCE/f;
		vectors[CORNER_LOD_RIGHT] = vectors[CORNER_FAR_BOTRIGHT] * LOD_DISTANCE/f;
	}else{
		// looking down, top corners are further away
		vectors[CORNER_LOD_LEFT] = vectors[CORNER_FAR_TOPLEFT] * LOD_DISTANCE/f;
		vectors[CORNER_LOD_RIGHT] = vectors[CORNER_FAR_TOPRIGHT] * LOD_DISTANCE/f;
	}
	vectors[CORNER_PRIO_LEFT].x = vectors[CORNER_LOD_LEFT].x * 0.2f;
	vectors[CORNER_PRIO_LEFT].y = vectors[CORNER_LOD_LEFT].y * 0.2f;
	vectors[CORNER_PRIO_LEFT].z = vectors[CORNER_LOD_LEFT].z;
	vectors[CORNER_PRIO_RIGHT].x = vectors[CORNER_LOD_RIGHT].x * 0.2f;
	vectors[CORNER_PRIO_RIGHT].y = vectors[CORNER_LOD_RIGHT].y * 0.2f;
	vectors[CORNER_PRIO_RIGHT].z = vectors[CORNER_LOD_RIGHT].z;
	RwV3dTransformPoints(vectors, vectors, 9, cammatrix);

	m_loadingPriority = false;
	if(TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_TOPDOWN ||
#ifdef FIX_BUGS
	   TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_GTACLASSIC ||
#endif
	   TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_TOP_DOWN_PED){
		CRect rect;
		int x1, x2, y1, y2;
		LimitFrustumVector(vectors[CORNER_FAR_TOPLEFT], vectors[CORNER_CAM], -100.0f);
		rect.ContainPoint(vectors[CORNER_FAR_TOPLEFT]);
		LimitFrustumVector(vectors[CORNER_FAR_TOPRIGHT], vectors[CORNER_CAM], -100.0f);
		rect.ContainPoint(vectors[CORNER_FAR_TOPRIGHT]);
		LimitFrustumVector(vectors[CORNER_FAR_BOTRIGHT], vectors[CORNER_CAM], -100.0f);
		rect.ContainPoint(vectors[CORNER_FAR_BOTRIGHT]);
		LimitFrustumVector(vectors[CORNER_FAR_BOTLEFT], vectors[CORNER_CAM], -100.0f);
		rect.ContainPoint(vectors[CORNER_FAR_BOTLEFT]);
		x1 = CWorld::GetSectorIndexX(rect.left);
		if(x1 < 0) x1 = 0;
		x2 = CWorld::GetSectorIndexX(rect.right);
		if(x2 >= NUMSECTORS_X-1) x2 = NUMSECTORS_X-1;
		y1 = CWorld::GetSectorIndexY(rect.top);
		if(y1 < 0) y1 = 0;
		y2 = CWorld::GetSectorIndexY(rect.bottom);
		if(y2 >= NUMSECTORS_Y-1) y2 = NUMSECTORS_Y-1;
		for(; x1 <= x2; x1++)
			for(int y = y1; y <= y2; y++)
				ScanSectorList(CWorld::GetSector(x1, y)->m_lists);
	}else{
#ifdef GTA_TRAIN
		CVehicle *train = FindPlayerTrain();
		if(train && train->GetPosition().z < 0.0f){
			poly[0].x = CWorld::GetSectorX(vectors[CORNER_CAM].x);
			poly[0].y = CWorld::GetSectorY(vectors[CORNER_CAM].y);
			poly[1].x = CWorld::GetSectorX(vectors[CORNER_LOD_LEFT].x);
			poly[1].y = CWorld::GetSectorY(vectors[CORNER_LOD_LEFT].y);
			poly[2].x = CWorld::GetSectorX(vectors[CORNER_LOD_RIGHT].x);
			poly[2].y = CWorld::GetSectorY(vectors[CORNER_LOD_RIGHT].y);
			ScanSectorPoly(poly, 3, ScanSectorList_Subway);
		}else
#endif
		{
			if(f > LOD_DISTANCE){
				// priority
				poly[0].x = CWorld::GetSectorX(vectors[CORNER_CAM].x);
				poly[0].y = CWorld::GetSectorY(vectors[CORNER_CAM].y);
				poly[1].x = CWorld::GetSectorX(vectors[CORNER_PRIO_LEFT].x);
				poly[1].y = CWorld::GetSectorY(vectors[CORNER_PRIO_LEFT].y);
				poly[2].x = CWorld::GetSectorX(vectors[CORNER_PRIO_RIGHT].x);
				poly[2].y = CWorld::GetSectorY(vectors[CORNER_PRIO_RIGHT].y);
				ScanSectorPoly(poly, 3, ScanSectorList_Priority);

				// below LOD
				poly[0].x = CWorld::GetSectorX(vectors[CORNER_CAM].x);
				poly[0].y = CWorld::GetSectorY(vectors[CORNER_CAM].y);
				poly[1].x = CWorld::GetSectorX(vectors[CORNER_LOD_LEFT].x);
				poly[1].y = CWorld::GetSectorY(vectors[CORNER_LOD_LEFT].y);
				poly[2].x = CWorld::GetSectorX(vectors[CORNER_LOD_RIGHT].x);
				poly[2].y = CWorld::GetSectorY(vectors[CORNER_LOD_RIGHT].y);
				ScanSectorPoly(poly, 3, ScanSectorList);
			}else{
				poly[0].x = CWorld::GetSectorX(vectors[CORNER_CAM].x);
				poly[0].y = CWorld::GetSectorY(vectors[CORNER_CAM].y);
				poly[1].x = CWorld::GetSectorX(vectors[CORNER_FAR_TOPLEFT].x);
				poly[1].y = CWorld::GetSectorY(vectors[CORNER_FAR_TOPLEFT].y);
				poly[2].x = CWorld::GetSectorX(vectors[CORNER_FAR_TOPRIGHT].x);
				poly[2].y = CWorld::GetSectorY(vectors[CORNER_FAR_TOPRIGHT].y);
				ScanSectorPoly(poly, 3, ScanSectorList);
			}

#ifndef GTA_WORLDSTREAM
			// TODO(LCS): may be better to have this code with gbPreviewCity
#ifdef NO_ISLAND_LOADING
			if (FrontEndMenuManager.m_PrefsIslandLoading == CMenuManager::ISLAND_LOADING_HIGH) {
				ScanBigBuildingList(CWorld::GetBigBuildingList(LEVEL_BEACH));
				ScanBigBuildingList(CWorld::GetBigBuildingList(LEVEL_MAINLAND));
			} else 
#endif
			{
#ifdef FIX_BUGS
			if(CCollision::ms_collisionInMemory != LEVEL_GENERIC)
#endif
				ScanBigBuildingList(CWorld::GetBigBuildingList(CGame::currLevel));
			}
			ScanBigBuildingList(CWorld::GetBigBuildingList(LEVEL_GENERIC));
#endif
		}
	}
}

void
CRenderer::RequestObjectsInFrustum(void)
{
	float f = RwCameraGetFarClipPlane(TheCamera.m_pRwCamera);
	RwV2d vw = *RwCameraGetViewWindow(TheCamera.m_pRwCamera);
	CVector vectors[9];
	RwMatrix *cammatrix;
	RwV2d poly[3];

	memset(vectors, 0, sizeof(vectors));
	vectors[CORNER_FAR_TOPLEFT].x = -vw.x * f;
	vectors[CORNER_FAR_TOPLEFT].y = vw.y * f;
	vectors[CORNER_FAR_TOPLEFT].z = f;
	vectors[CORNER_FAR_TOPRIGHT].x = vw.x * f;
	vectors[CORNER_FAR_TOPRIGHT].y = vw.y * f;
	vectors[CORNER_FAR_TOPRIGHT].z = f;
	vectors[CORNER_FAR_BOTRIGHT].x = vw.x * f;
	vectors[CORNER_FAR_BOTRIGHT].y = -vw.y * f;
	vectors[CORNER_FAR_BOTRIGHT].z = f;
	vectors[CORNER_FAR_BOTLEFT].x = -vw.x * f;
	vectors[CORNER_FAR_BOTLEFT].y = -vw.y * f;
	vectors[CORNER_FAR_BOTLEFT].z = f;

	cammatrix = RwFrameGetMatrix(RwCameraGetFrame(TheCamera.m_pRwCamera));

	CWorld::AdvanceCurrentScanCode();
	ms_vecCameraPosition = TheCamera.GetPosition();

	if(cammatrix->at.z > 0.0f){
		// looking up, bottom corners are further away
		vectors[CORNER_LOD_LEFT] = vectors[CORNER_FAR_BOTLEFT] * LOD_DISTANCE/f;
		vectors[CORNER_LOD_RIGHT] = vectors[CORNER_FAR_BOTRIGHT] * LOD_DISTANCE/f;
	}else{
		// looking down, top corners are further away
		vectors[CORNER_LOD_LEFT] = vectors[CORNER_FAR_TOPLEFT] * LOD_DISTANCE/f;
		vectors[CORNER_LOD_RIGHT] = vectors[CORNER_FAR_TOPRIGHT] * LOD_DISTANCE/f;
	}
	vectors[CORNER_PRIO_LEFT].x = vectors[CORNER_LOD_LEFT].x * 0.2f;
	vectors[CORNER_PRIO_LEFT].y = vectors[CORNER_LOD_LEFT].y * 0.2f;
	vectors[CORNER_PRIO_LEFT].z = vectors[CORNER_LOD_LEFT].z;
	vectors[CORNER_PRIO_RIGHT].x = vectors[CORNER_LOD_RIGHT].x * 0.2f;
	vectors[CORNER_PRIO_RIGHT].y = vectors[CORNER_LOD_RIGHT].y * 0.2f;
	vectors[CORNER_PRIO_RIGHT].z = vectors[CORNER_LOD_RIGHT].z;
	RwV3dTransformPoints(vectors, vectors, 9, cammatrix);

	if(TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_TOPDOWN ||
#ifdef FIX_BUGS
	   TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_GTACLASSIC ||
#endif
	   TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_TOP_DOWN_PED){
		CRect rect;
		int x1, x2, y1, y2;
		LimitFrustumVector(vectors[CORNER_FAR_TOPLEFT], vectors[CORNER_CAM], -100.0f);
		rect.ContainPoint(vectors[CORNER_FAR_TOPLEFT]);
		LimitFrustumVector(vectors[CORNER_FAR_TOPRIGHT], vectors[CORNER_CAM], -100.0f);
		rect.ContainPoint(vectors[CORNER_FAR_TOPRIGHT]);
		LimitFrustumVector(vectors[CORNER_FAR_BOTRIGHT], vectors[CORNER_CAM], -100.0f);
		rect.ContainPoint(vectors[CORNER_FAR_BOTRIGHT]);
		LimitFrustumVector(vectors[CORNER_FAR_BOTLEFT], vectors[CORNER_CAM], -100.0f);
		rect.ContainPoint(vectors[CORNER_FAR_BOTLEFT]);
		x1 = CWorld::GetSectorIndexX(rect.left);
		if(x1 < 0) x1 = 0;
		x2 = CWorld::GetSectorIndexX(rect.right);
		if(x2 >= NUMSECTORS_X-1) x2 = NUMSECTORS_X-1;
		y1 = CWorld::GetSectorIndexY(rect.top);
		if(y1 < 0) y1 = 0;
		y2 = CWorld::GetSectorIndexY(rect.bottom);
		if(y2 >= NUMSECTORS_Y-1) y2 = NUMSECTORS_Y-1;
		for(; x1 <= x2; x1++)
			for(int y = y1; y <= y2; y++)
				ScanSectorList_RequestModels(CWorld::GetSector(x1, y)->m_lists);
	}else{
		poly[0].x = CWorld::GetSectorX(vectors[CORNER_CAM].x);
		poly[0].y = CWorld::GetSectorY(vectors[CORNER_CAM].y);
		poly[1].x = CWorld::GetSectorX(vectors[CORNER_LOD_LEFT].x);
		poly[1].y = CWorld::GetSectorY(vectors[CORNER_LOD_LEFT].y);
		poly[2].x = CWorld::GetSectorX(vectors[CORNER_LOD_RIGHT].x);
		poly[2].y = CWorld::GetSectorY(vectors[CORNER_LOD_RIGHT].y);
		ScanSectorPoly(poly, 3, ScanSectorList_RequestModels);
	}
}

bool
CEntity::SetupLighting(void)
{
	return false;
}

void
CEntity::RemoveLighting(bool)
{
}

bool
CPed::SetupLighting(void)
{
	ActivateDirectional();
	SetAmbientColoursForPedsCarsAndObjects();

#ifndef MASTER
	// Originally this was being called through iteration of Sectors, but putting it here is better.
	if (GetDebugDisplay() != 0 && !IsPlayer())
		DebugRenderOnePedText();
#endif

	if (bRenderScorched) {
		WorldReplaceNormalLightsWithScorched(Scene.world, 0.1f);
	} else {
		// Note that this lightMult is only affected by LIGHT_DARKEN. If there's no LIGHT_DARKEN, it will be 1.0.
		float lightMult = CPointLights::GenerateLightsAffectingObject(&GetPosition());
		if (lightMult != 1.0f) {
			SetAmbientAndDirectionalColours(lightMult);
			return true;
		}
	}
	return false;
}

void
CPed::RemoveLighting(bool reset)
{
	if (!bRenderScorched) {
		CRenderer::RemoveVehiclePedLights(this, reset);
		if (reset)
			ReSetAmbientAndDirectionalColours();
	}
	SetAmbientColours();
	DeActivateDirectional();
}

float
CalcNewDelta(RwV2d *a, RwV2d *b)
{
	return (b->x - a->x) / (b->y - a->y);
}

#ifdef FIX_BUGS
#define TOINT(x) ((int)Floor(x))
#else
#define TOINT(x) ((int)(x))
#endif

void
CRenderer::ScanSectorPoly(RwV2d *poly, int32 numVertices, void (*scanfunc)(CPtrList *))
{
	float miny, maxy;
	int y, yend;
	int x, xstart, xend;
	int i;
	int a1, a2, b1, b2;
	float deltaA, deltaB;
	float xA, xB;

	miny = poly[0].y;
	maxy = poly[0].y;
	a2 = 0;
	xstart = 9999;
	xend = -9999;

	for(i = 1; i < numVertices; i++){
		if(poly[i].y > maxy)
			maxy = poly[i].y;
		if(poly[i].y < miny){
			miny = poly[i].y;
			a2 = i;
		}
	}
	y = TOINT(miny);
	yend = TOINT(maxy);

	// Go left in poly to find first edge b
	b2 = a2;
	for(i = 0; i < numVertices; i++){
		b1 = b2--;
		if(b2 < 0) b2 = numVertices-1;
		if(poly[b1].x < xstart)
			xstart = TOINT(poly[b1].x);
		if(TOINT(poly[b1].y) != TOINT(poly[b2].y))
			break;
	}
	// Go right to find first edge a
	for(i = 0; i < numVertices; i++){
		a1 = a2++;
		if(a2 == numVertices) a2 = 0;
		if(poly[a1].x > xend)
			xend = TOINT(poly[a1].x);
		if(TOINT(poly[a1].y) != TOINT(poly[a2].y))
			break;
	}

	// prestep x1 and x2 to next integer y
	deltaA = CalcNewDelta(&poly[a1], &poly[a2]);
	xA = deltaA * (Ceil(poly[a1].y) - poly[a1].y) + poly[a1].x;
	deltaB = CalcNewDelta(&poly[b1], &poly[b2]);
	xB = deltaB * (Ceil(poly[b1].y) - poly[b1].y) + poly[b1].x;

	if(y != yend){
		if(deltaB < 0.0f && TOINT(xB) < xstart)
			xstart = TOINT(xB);
		if(deltaA >= 0.0f && TOINT(xA) > xend)
			xend = TOINT(xA);
	}

	while(y <= yend && y < NUMSECTORS_Y){
		// scan one x-line
		if(y >= 0 && xstart < NUMSECTORS_X)
			for(x = xstart; x <= xend && x != NUMSECTORS_X; x++)
				if(x >= 0)
					scanfunc(CWorld::GetSector(x, y)->m_lists);

		// advance one scan line
		y++;
		xA += deltaA;
		xB += deltaB;

		// update left side
		if(y == TOINT(poly[b2].y)){
			// reached end of edge
			if(y == yend){
				if(deltaB < 0.0f){
					do{
						xstart = TOINT(poly[b2--].x);
						if(b2 < 0) b2 = numVertices-1;
					}while(xstart > TOINT(poly[b2].x));
				}else
					xstart = TOINT(xB - deltaB);
			}else{
				// switch edges
				if(deltaB < 0.0f)
					xstart = TOINT(poly[b2].x);
				else
					xstart = TOINT(xB - deltaB);
				do{
					b1 = b2--;
					if(b2 < 0) b2 = numVertices-1;
					if(TOINT(poly[b1].x) < xstart)
						xstart = TOINT(poly[b1].x);
				}while(y == TOINT(poly[b2].y));
				deltaB = CalcNewDelta(&poly[b1], &poly[b2]);
				xB = deltaB * (Ceil(poly[b1].y) - poly[b1].y) + poly[b1].x;
				if(deltaB < 0.0f && TOINT(xB) < xstart)
					xstart = TOINT(xB);
			}
		}else{
			if(deltaB < 0.0f)
				xstart = TOINT(xB);
			else
				xstart = TOINT(xB - deltaB);
		}

		// update right side
		if(y == TOINT(poly[a2].y)){
			// reached end of edge
			if(y == yend){
				if(deltaA < 0.0f)
					xend = TOINT(xA - deltaA);
				else{
					do{
						xend = TOINT(poly[a2++].x);
						if(a2 == numVertices) a2 = 0;
					}while(xend < TOINT(poly[a2].x));
				}
			}else{
				// switch edges
				if(deltaA < 0.0f)
					xend = TOINT(xA - deltaA);
				else
					xend = TOINT(poly[a2].x);
				do{
					a1 = a2++;
					if(a2 == numVertices) a2 = 0;
					if(TOINT(poly[a1].x) > xend)
						xend = TOINT(poly[a1].x);
				}while(y == TOINT(poly[a2].y));
				deltaA = CalcNewDelta(&poly[a1], &poly[a2]);
				xA = deltaA * (Ceil(poly[a1].y) - poly[a1].y) + poly[a1].x;
				if(deltaA >= 0.0f && TOINT(xA) > xend)
					xend = TOINT(xA);
			}
		}else{
			if(deltaA < 0.0f)
				xend = TOINT(xA - deltaA);
			else
				xend = TOINT(xA);
		}
	}
}

// probably didn't exist as a separate function in LCS
void
CRenderer::InsertEntityIntoList(CEntity *ent)
{
#ifdef FIX_BUGS
	if (!ent->m_rwObject) return;
#endif
#ifdef __PS3__
	// patch 24: the lists hold NUMVISIBLEENTITIES (2000) and nothing checked
	if(ms_nNoOfVisibleVehicles >= NUMVISIBLEENTITIES || ms_nNoOfVisibleEntities >= NUMVISIBLEENTITIES){
		static int logged;
		if(logged++ < 4)
			PS3_Logf("[render] visible list full (%d entities, %d vehicles and peds): an entity left out", ms_nNoOfVisibleEntities, ms_nNoOfVisibleVehicles);
		return;
	}
#endif

	if(ent->IsVehicle() || ent->bIsVehicle || ent->IsPed() || ent->bIsPed)
		ms_aVisibleVehiclePtrs[ms_nNoOfVisibleVehicles++] = ent;
#ifdef NEW_RENDERER
	else if(!gbPreviewCity && ent->IsBuilding())
		ms_aVisibleBuildingPtrs[ms_nNoOfVisibleBuildings++] = ent;
#endif
	else
		ms_aVisibleEntityPtrs[ms_nNoOfVisibleEntities++] = ent;
#if defined(VIS_DISTANCE_ALPHA) && defined(FIX_BUGS)
	// this flag can only be trusted if entity is in alpha list
	// unfortunately we're checking it in other cases too, which is bad.
	// no idea why this isn't a problem on android
	ent->bDistanceFade = false;
#endif
}

void
CRenderer::AddVisibleEntity(CEntity *ent)
{
#ifdef __PS3__
	// ScanBigBuildingList takes an entity seen last time as visible without
	// asking SetupBigBuildingVisibility again: a plane that went far (and
	// lost its model, see there) in between got here with no model.
	if(ent->m_rwObject == nil){
		ent->bOffscreen = true;
		return;
	}
	if(ms_nNoOfVisibleEntities >= NUMVISIBLEENTITIES){
		static int logged;
		if(logged++ < 4)
			PS3_Logf("[render] visible list full (%d entities): a big building left out", ms_nNoOfVisibleEntities);
		return;
	}
#endif
#ifdef NEW_RENDERER
	if(!gbPreviewCity && ent->IsBuilding())
		ms_aVisibleBuildingPtrs[ms_nNoOfVisibleBuildings++] = ent;
	else
#endif
		ms_aVisibleEntityPtrs[ms_nNoOfVisibleEntities++] = ent;
	ent->bOffscreen = false;
}

void
CRenderer::ScanBigBuildingList(CPtrList &list)
{
	CPtrNode *node;
	CEntity *ent;
	int vis;

	int f = CTimer::GetFrameCounter() & 6;
	for(node = list.first; node; node = node->next){
		ent = (CEntity*)node->item;
#ifdef __PS3__
		// patch 24: a node whose neighbours don't point back was freed and
		// used again in another list (the walk goes on in that list)
		{
			static int brokenLogged;
			if(node->next && node->next->prev != node && brokenLogged < 8){
				brokenLogged++;
				PS3_Logf("[bigbld] big building list broken: node %p (item %p) -> next %p, whose prev is %p",
				         node, node->item, node->next, node->next->prev);
			}
		}
		// patch 24: only big buildings and the planes belong here (World.cpp:
		// [bigbld]). Anything else is logged and not drawn.
		if(ent == nil || !ent->bIsBIGBuilding ||
		   !(ent->IsBuilding() || ent->IsDummy() || ent->IsObject() || ent->IsVehicle() && ((CVehicle*)ent)->IsPlane())){
			int level = -1;
			for(int l = 0; l < NUM_LEVELS; l++)
				if(&CWorld::GetBigBuildingList((eLevelName)l) == &list)
					level = l;
			// once per entity (it is there every frame)
			static CEntity *seen[32];
			static int nSeen;
			bool logged = false;
			for(int k = 0; k < nSeen; k++)
				if(seen[k] == ent) logged = true;
			if(!logged){
				seen[nSeen++ & 31] = ent;
				if(nSeen > 32) nSeen = 32;
				PS3_BigBuildingLogEntity("in a big building list but not a big building (not drawn)", ent, level, node, 0);
			}
			continue;
		}
#endif
		if(ent->bOffscreen || (ent->m_randomSeed&6) != f){
			ent->bOffscreen = true;
			vis = SetupBigBuildingVisibility(ent);
		}else
			vis = ent->bOffscreen ? VIS_INVISIBLE : VIS_VISIBLE;
#ifdef __PS3__
		PS3_ShadNoteVis(ent, vis);	// patch 91 (big buildings too)
#endif
		if(ent->bMakeVisible){
			ent->bMakeVisible = false;
			ent->bIsVisible = true;
		}
		switch(vis){
		case VIS_VISIBLE:
			AddVisibleEntity(ent);
			break;
		case VIS_STREAMME:
			if(!CStreaming::ms_disableStreaming)
				CStreaming::RequestModel(ent->GetModelIndex(), 0);
			break;
		}
	}
}

void
CRenderer::ScanSectorList(CPtrList *lists)
{
	CPtrNode *node;
	CPtrList *list;
	CEntity *ent;
	int i;
	float dx, dy;

	int numLists = NUMSECTORENTITYLISTS;
#ifdef GTA_WORLDSTREAM
	if(gbPreviewCity){
		numLists -= ENTITYLIST_UNKNOWN;
		lists += ENTITYLIST_UNKNOWN;
	}
#endif

	for(i = 0; i < numLists; i++){
		list = &lists[i];
		for(node = list->first; node; node = node->next){
			ent = (CEntity*)node->item;
			if(ent->m_scanCode == CWorld::GetCurrentScanCode())
				continue;	// already seen
			ent->m_scanCode = CWorld::GetCurrentScanCode();
			ent->bOffscreen = false;

			int vis = SetupEntityVisibility(ent);
#ifdef __PS3__
			PS3_ShadNoteVis(ent, vis);	// patch 91
#endif
			if(ent->bMakeVisible){
				ent->bMakeVisible = false;
				ent->bIsVisible = true;
			}
			switch(vis){
			case VIS_VISIBLE:
				InsertEntityIntoList(ent);
				break;
			case VIS_INVISIBLE:
				if(!IsGlass(ent->GetModelIndex()))
					break;
				// fall through
			case VIS_OFFSCREEN:
				ent->bOffscreen = true;
				dx = ms_vecCameraPosition.x - ent->GetPosition().x;
				dy = ms_vecCameraPosition.y - ent->GetPosition().y;
				if(dx > -30.0f && dx < 30.0f &&
				   dy > -30.0f && dy < 30.0f &&
				   ms_nNoOfInVisibleEntities < NUMINVISIBLEENTITIES - 1)
					ms_aInVisibleEntityPtrs[ms_nNoOfInVisibleEntities++] = ent;
				break;
			case VIS_STREAMME:
				if(!CStreaming::ms_disableStreaming)
					if(!m_loadingPriority || CStreaming::ms_numModelsRequested < 10)
						CStreaming::RequestModel(ent->GetModelIndex(), 0);
				break;
			}
		}
	}
}

void
CRenderer::ScanSectorList_Priority(CPtrList *lists)
{
	CPtrNode *node;
	CPtrList *list;
	CEntity *ent;
	int i;
	float dx, dy;

	int numLists = NUMSECTORENTITYLISTS;
#ifdef GTA_WORLDSTREAM
	if(gbPreviewCity){
		numLists -= ENTITYLIST_UNKNOWN;
		lists += ENTITYLIST_UNKNOWN;
	}
#endif

	for(i = 0; i < numLists; i++){
		list = &lists[i];
		for(node = list->first; node; node = node->next){
			ent = (CEntity*)node->item;
			if(ent->m_scanCode == CWorld::GetCurrentScanCode())
				continue;	// already seen
			ent->m_scanCode = CWorld::GetCurrentScanCode();
			ent->bOffscreen = false;

			int vis = SetupEntityVisibility(ent);
#ifdef __PS3__
			PS3_ShadNoteVis(ent, vis);	// patch 91
#endif
			if(ent->bMakeVisible){
				ent->bMakeVisible = false;
				ent->bIsVisible = true;
			}
			switch(vis){
			case VIS_VISIBLE:
				InsertEntityIntoList(ent);
				break;
			case VIS_INVISIBLE:
				if(!IsGlass(ent->GetModelIndex()))
					break;
				// fall through
			case VIS_OFFSCREEN:
				ent->bOffscreen = true;
				dx = ms_vecCameraPosition.x - ent->GetPosition().x;
				dy = ms_vecCameraPosition.y - ent->GetPosition().y;
				if(dx > -30.0f && dx < 30.0f &&
				   dy > -30.0f && dy < 30.0f &&
				   ms_nNoOfInVisibleEntities < NUMINVISIBLEENTITIES - 1)
					ms_aInVisibleEntityPtrs[ms_nNoOfInVisibleEntities++] = ent;
				break;
			case VIS_STREAMME:
				if(!CStreaming::ms_disableStreaming){
					CStreaming::RequestModel(ent->GetModelIndex(), 0);
					if(CStreaming::ms_aInfoForModel[ent->GetModelIndex()].m_loadState != STREAMSTATE_LOADED)
						m_loadingPriority = true;
				}
				break;
			}
		}
	}
}

#ifdef GTA_TRAIN
void
CRenderer::ScanSectorList_Subway(CPtrList *lists)
{
	CPtrNode *node;
	CPtrList *list;
	CEntity *ent;
	int i;
	float dx, dy;

	int numLists = NUMSECTORENTITYLISTS;
#ifdef GTA_WORLDSTREAM
	if(gbPreviewCity){
		numLists -= ENTITYLIST_UNKNOWN;
		lists += ENTITYLIST_UNKNOWN;
	}
#endif

	for(i = 0; i < numLists; i++){
		list = &lists[i];
		for(node = list->first; node; node = node->next){
			ent = (CEntity*)node->item;
			if(ent->m_scanCode == CWorld::GetCurrentScanCode())
				continue;	// already seen
			ent->m_scanCode = CWorld::GetCurrentScanCode();
			ent->bOffscreen = false;
			int ps3Vis = SetupEntityVisibility(ent);
#ifdef __PS3__
			PS3_ShadNoteVis(ent, ps3Vis);	// patch 91
#endif
			switch(ps3Vis){
			case VIS_VISIBLE:
				InsertEntityIntoList(ent);
				break;
			case VIS_OFFSCREEN:
				ent->bOffscreen = true;
				dx = ms_vecCameraPosition.x - ent->GetPosition().x;
				dy = ms_vecCameraPosition.y - ent->GetPosition().y;
				if(dx > -30.0f && dx < 30.0f &&
				   dy > -30.0f && dy < 30.0f &&
				   ms_nNoOfInVisibleEntities < NUMINVISIBLEENTITIES - 1)
					ms_aInVisibleEntityPtrs[ms_nNoOfInVisibleEntities++] = ent;
				break;
			}
		}
	}
}
#endif

void
CRenderer::ScanSectorList_RequestModels(CPtrList *lists)
{
	CPtrNode *node;
	CPtrList *list;
	CEntity *ent;
	int i;

	int numLists = NUMSECTORENTITYLISTS;
#ifdef GTA_WORLDSTREAM
	if(gbPreviewCity){
		numLists -= ENTITYLIST_UNKNOWN;
		lists += ENTITYLIST_UNKNOWN;
	}
#endif

	for(i = 0; i < numLists; i++){
		list = &lists[i];
		for(node = list->first; node; node = node->next){
			ent = (CEntity*)node->item;
			if(ent->m_scanCode == CWorld::GetCurrentScanCode())
				continue;	// already seen
			ent->m_scanCode = CWorld::GetCurrentScanCode();
			if(ShouldModelBeStreamed(ent, ms_vecCameraPosition))
				CStreaming::RequestModel(ent->GetModelIndex(), 0);
		}
	}
}

// Put big buildings in front
// This seems pointless because the sector lists shouldn't have big buildings in the first place
void
CRenderer::SortBIGBuildings(void)
{
	int x, y;
	for(y = 0; y < NUMSECTORS_Y; y++)
		for(x = 0; x < NUMSECTORS_X; x++){
			SortBIGBuildingsForSectorList(&CWorld::GetSector(x, y)->m_lists[ENTITYLIST_BUILDINGS]);
			SortBIGBuildingsForSectorList(&CWorld::GetSector(x, y)->m_lists[ENTITYLIST_BUILDINGS_OVERLAP]);
		}
}

void
CRenderer::SortBIGBuildingsForSectorList(CPtrList *list)
{
	CPtrNode *node;
	CEntity *ent;

	for(node = list->first; node; node = node->next){
		ent = (CEntity*)node->item;
		if(ent->bIsBIGBuilding){
			list->RemoveNode(node);
			list->InsertNode(node);
		}
	}
}

bool
CRenderer::ShouldModelBeStreamed(CEntity *ent, const CVector &campos)
{
	if(!IsAreaVisible(ent->m_area))
		return false;
	CTimeModelInfo *mi = (CTimeModelInfo *)CModelInfo::GetModelInfo(ent->GetModelIndex());
	if(!ent->IsObject() && !ent->IsDummy()){
		if(mi->GetModelType() == MITYPE_TIME)
			if(!CClock::GetIsTimeInRange(mi->GetTimeOn(), mi->GetTimeOff()))
				return false;
		// LCS(TODO): cWorldStream::pDynamic, but just returns 0 anyway
	}
	float dist = (ent->GetPosition() - campos).Magnitude();
	if(mi->m_noFade)
		return dist - STREAM_DISTANCE < mi->GetLargestLodDistance();
	else
		return dist - FADE_DISTANCE - STREAM_DISTANCE < mi->GetLargestLodDistance();
}

void
CRenderer::RemoveVehiclePedLights(CEntity *ent, bool reset)
{
	if(!ent->bRenderScorched){
		CPointLights::RemoveLightsAffectingObject();
		if(reset)
			ReSetAmbientAndDirectionalColours();
	}
//	SetAmbientColours();
//	DeActivateDirectional();
}


#ifdef EXTENDED_PIPELINES
#include "postfx.h"

static RwIm2DVertex Screen2EnvQuad[4];
static RwImVertexIndex EnvQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };

static void
SetQuadVertices(RwRaster *env, RwRaster *screen, float z)
{
	uint32 width  = RwRasterGetWidth(env);
	uint32 height = RwRasterGetHeight(env);

	float zero, xmax, ymax;

	zero = -HALFPX;
	xmax = width - HALFPX;
	ymax = height - HALFPX;

	float recipz = 1.0f/z;
	float umax = (float)SCREEN_WIDTH/RwRasterGetWidth(screen);
	float vmax = (float)SCREEN_HEIGHT/RwRasterGetHeight(screen);

	RwIm2DVertexSetScreenX(&Screen2EnvQuad[0], zero);
	RwIm2DVertexSetScreenY(&Screen2EnvQuad[0], zero);
	RwIm2DVertexSetScreenZ(&Screen2EnvQuad[0], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Screen2EnvQuad[0], z);
	RwIm2DVertexSetRecipCameraZ(&Screen2EnvQuad[0], recipz);
	RwIm2DVertexSetU(&Screen2EnvQuad[0], 0.0f, recipz);
	RwIm2DVertexSetV(&Screen2EnvQuad[0], 0.0f, recipz);
	RwIm2DVertexSetIntRGBA(&Screen2EnvQuad[0], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Screen2EnvQuad[1], zero);
	RwIm2DVertexSetScreenY(&Screen2EnvQuad[1], ymax);
	RwIm2DVertexSetScreenZ(&Screen2EnvQuad[1], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Screen2EnvQuad[1], z);
	RwIm2DVertexSetRecipCameraZ(&Screen2EnvQuad[1], recipz);
	RwIm2DVertexSetU(&Screen2EnvQuad[1], 0.0f, recipz);
	RwIm2DVertexSetV(&Screen2EnvQuad[1], vmax, recipz);
	RwIm2DVertexSetIntRGBA(&Screen2EnvQuad[1], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Screen2EnvQuad[2], xmax);
	RwIm2DVertexSetScreenY(&Screen2EnvQuad[2], ymax);
	RwIm2DVertexSetScreenZ(&Screen2EnvQuad[2], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Screen2EnvQuad[2], z);
	RwIm2DVertexSetRecipCameraZ(&Screen2EnvQuad[2], recipz);
	RwIm2DVertexSetU(&Screen2EnvQuad[2], umax, recipz);
	RwIm2DVertexSetV(&Screen2EnvQuad[2], vmax, recipz);
	RwIm2DVertexSetIntRGBA(&Screen2EnvQuad[2], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&Screen2EnvQuad[3], xmax);
	RwIm2DVertexSetScreenY(&Screen2EnvQuad[3], zero);
	RwIm2DVertexSetScreenZ(&Screen2EnvQuad[3], RwIm2DGetNearScreenZ());
	RwIm2DVertexSetCameraZ(&Screen2EnvQuad[3], z);
	RwIm2DVertexSetRecipCameraZ(&Screen2EnvQuad[3], recipz);
	RwIm2DVertexSetU(&Screen2EnvQuad[3], umax, recipz);
	RwIm2DVertexSetV(&Screen2EnvQuad[3], 0.0f, recipz);
	RwIm2DVertexSetIntRGBA(&Screen2EnvQuad[3], 255, 255, 255, 255);
}

static RwIm2DVertex coronaVerts[4*4];
static RwImVertexIndex coronaIndices[6*4];
static int numCoronaVerts, numCoronaIndices;

static void
AddCorona(float x, float y, float sz)
{
	float nearz, recipz;
	RwIm2DVertex *v;
	nearz = RwIm2DGetNearScreenZ();
	float z = RwCameraGetNearClipPlane(RwCameraGetCurrentCamera());
	recipz = 1.0f/z;

	v = &coronaVerts[numCoronaVerts];
	RwIm2DVertexSetScreenX(&v[0], x);
	RwIm2DVertexSetScreenY(&v[0], y);
	RwIm2DVertexSetScreenZ(&v[0], z);
	RwIm2DVertexSetScreenZ(&v[0], nearz);
	RwIm2DVertexSetRecipCameraZ(&v[0], recipz);
	RwIm2DVertexSetU(&v[0], 0.0f, recipz);
	RwIm2DVertexSetV(&v[0], 0.0f, recipz);
	RwIm2DVertexSetIntRGBA(&v[0], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&v[1], x);
	RwIm2DVertexSetScreenY(&v[1], y + sz);
	RwIm2DVertexSetScreenZ(&v[1], z);
	RwIm2DVertexSetScreenZ(&v[1], nearz);
	RwIm2DVertexSetRecipCameraZ(&v[1], recipz);
	RwIm2DVertexSetU(&v[1], 0.0f, recipz);
	RwIm2DVertexSetV(&v[1], 1.0f, recipz);
	RwIm2DVertexSetIntRGBA(&v[1], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&v[2], x + sz);
	RwIm2DVertexSetScreenY(&v[2], y + sz);
	RwIm2DVertexSetScreenZ(&v[2], z);
	RwIm2DVertexSetScreenZ(&v[2], nearz);
	RwIm2DVertexSetRecipCameraZ(&v[2], recipz);
	RwIm2DVertexSetU(&v[2], 1.0f, recipz);
	RwIm2DVertexSetV(&v[2], 1.0f, recipz);
	RwIm2DVertexSetIntRGBA(&v[2], 255, 255, 255, 255);

	RwIm2DVertexSetScreenX(&v[3], x + sz);
	RwIm2DVertexSetScreenY(&v[3], y);
	RwIm2DVertexSetScreenZ(&v[3], z);
	RwIm2DVertexSetScreenZ(&v[3], nearz);
	RwIm2DVertexSetRecipCameraZ(&v[3], recipz);
	RwIm2DVertexSetU(&v[3], 1.0f, recipz);
	RwIm2DVertexSetV(&v[3], 0.0f, recipz);
	RwIm2DVertexSetIntRGBA(&v[3], 255, 255, 255, 255);


	coronaIndices[numCoronaIndices++] = numCoronaVerts;
	coronaIndices[numCoronaIndices++] = numCoronaVerts + 1;
	coronaIndices[numCoronaIndices++] = numCoronaVerts + 2;
	coronaIndices[numCoronaIndices++] = numCoronaVerts;
	coronaIndices[numCoronaIndices++] = numCoronaVerts + 2;
	coronaIndices[numCoronaIndices++] = numCoronaVerts + 3;
	numCoronaVerts += 4;
}

static void
DrawEnvMapCoronas(float heading)
{
	RwRaster *rt = RwTextureGetRaster(CustomPipes::EnvMapTex);
	const float BIG = 89.0f * RwRasterGetWidth(rt)/128.0f;
	const float SMALL = 38.0f * RwRasterGetHeight(rt)/128.0f;

	float x;
	numCoronaVerts = 0;
	numCoronaIndices = 0;
	x = (heading - PI)/TWOPI;// - 1.0f;
	x *= BIG+SMALL;
	AddCorona(x, 0.0f, BIG);	x += BIG;
	AddCorona(x, 12.0f, SMALL);	x += SMALL;
	AddCorona(x, 0.0f, BIG);	x += BIG;
	AddCorona(x, 12.0f, SMALL);	x += SMALL;

	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RwTextureGetRaster(gpCoronaTexture[CCoronas::TYPE_STAR]));
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, coronaVerts, numCoronaVerts, coronaIndices, numCoronaIndices);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
}

void
CRenderer::GenerateEnvironmentMap(void)
{
	// We'll probably do this differently eventually
	// re-using all sorts of stuff here...

	CPostFX::GetBackBuffer(Scene.camera);

	RwCameraBeginUpdate(CustomPipes::EnvMapCam);

	// get current scene
	SetQuadVertices(RwTextureGetRaster(CustomPipes::EnvMapTex), CPostFX::pBackBuffer, RwCameraGetNearClipPlane(RwCameraGetCurrentCamera()));
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, CPostFX::pBackBuffer);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, Screen2EnvQuad, 4, EnvQuadIndices, 6);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);

	// Draw coronas
	if(CustomPipes::VehiclePipeSwitch != CustomPipes::VEHICLEPIPE_MOBILE)
		DrawEnvMapCoronas(TheCamera.GetForward().Heading());

	RwCameraEndUpdate(CustomPipes::EnvMapCam);


	RwCameraBeginUpdate(Scene.camera);

	if(gbRenderDebugEnvMap){
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RwTextureGetRaster(CustomPipes::EnvMapTex));
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, CustomPipes::EnvScreenQuad, 4, (RwImVertexIndex*)CustomPipes::QuadIndices, 6);
	}
}
#else
// PS3: no environment map (custom pipelines are GL3/D3D9 only)
void
CRenderer::GenerateEnvironmentMap(void)
{
}
#endif
