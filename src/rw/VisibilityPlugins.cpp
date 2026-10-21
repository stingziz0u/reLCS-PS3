#include "common.h"
#ifdef _3DS
#include "../../../../common/3ds/PS2GraphicsProfile.h"
#endif
#ifdef _3DS
#include "../../../../common/3ds/WorldDrawDistance.h"
#endif

#include "RwHelper.h"
#include "templates.h"
#include "main.h"
#include "Entity.h"
#ifdef _3DS
#include "../../../../common/3ds/CameraOcclusionRender.h"
#endif
#include "ModelInfo.h"
#include "Lights.h"
#include "RwHelper.h"
#include "Renderer.h"
#include "Camera.h"
#include "VisibilityPlugins.h"
#include "World.h"
#include "custompipes.h"

#ifdef _3DS
#include <citro3d.h>
#include <3ds/os.h>
#endif
#include "MemoryHeap.h"

//--LCS: file done
// LCS: no transparent water in LCS so no need for alpha boat and alpha underwater lists

CLinkList<CVisibilityPlugins::AlphaObjectInfo> CVisibilityPlugins::m_alphaList;
//CLinkList<CVisibilityPlugins::AlphaObjectInfo> CVisibilityPlugins::m_alphaBoatAtomicList;
CLinkList<CVisibilityPlugins::AlphaObjectInfo> CVisibilityPlugins::m_alphaEntityList;
//CLinkList<CVisibilityPlugins::AlphaObjectInfo> CVisibilityPlugins::m_alphaUnderwaterEntityList;
#ifdef NEW_RENDERER
CLinkList<CVisibilityPlugins::AlphaObjectInfo> CVisibilityPlugins::m_alphaBuildingList;
#endif

int32 CVisibilityPlugins::ms_atomicPluginOffset = -1;
int32 CVisibilityPlugins::ms_framePluginOffset = -1;
int32 CVisibilityPlugins::ms_clumpPluginOffset = -1;

RwCamera *CVisibilityPlugins::ms_pCamera;
RwV3d *CVisibilityPlugins::ms_pCameraPosn;
float CVisibilityPlugins::ms_cullCompsDist;
float CVisibilityPlugins::ms_vehicleLod0Dist;
float CVisibilityPlugins::ms_vehicleLod1Dist;
float CVisibilityPlugins::ms_vehicleFadeDist;
float CVisibilityPlugins::ms_bigVehicleLod0Dist;
float CVisibilityPlugins::ms_bigVehicleLod1Dist;
float CVisibilityPlugins::ms_pedLodDist;
float CVisibilityPlugins::ms_pedFadeDist;

#define RENDERCALLBACK AtomicDefaultRenderCallBack

void
CVisibilityPlugins::Initialise(void)
{
	m_alphaList.Init(NUMALPHALIST);
	m_alphaList.head.item.sort = 0.0f;
	m_alphaList.tail.item.sort = 100000000.0f;

//	m_alphaBoatAtomicList.Init(NUMBOATALPHALIST);
//	m_alphaBoatAtomicList.head.item.sort = 0.0f;
//	m_alphaBoatAtomicList.tail.item.sort = 100000000.0f;

#ifdef ASPECT_RATIO_SCALE
	// default 150 is not enough for bigger FOVs
	m_alphaEntityList.Init(NUMALPHAENTITYLIST * 3);
#else
	m_alphaEntityList.Init(NUMALPHAENTITYLIST);
#endif // ASPECT_RATIO_SCALE
	m_alphaEntityList.head.item.sort = 0.0f;
	m_alphaEntityList.tail.item.sort = 100000000.0f;

//	m_alphaUnderwaterEntityList.Init(NUMALPHAUNTERWATERENTITYLIST);
//	m_alphaUnderwaterEntityList.head.item.sort = 0.0f;
//	m_alphaUnderwaterEntityList.tail.item.sort = 100000000.0f;

#ifdef NEW_RENDERER
	m_alphaBuildingList.Init(NUMALPHAENTITYLIST);
	m_alphaBuildingList.head.item.sort = 0.0f;
	m_alphaBuildingList.tail.item.sort = 100000000.0f;
#endif

	base::RegisterRelocatableChunkFunc((void*)RENDERCALLBACK);
	base::RegisterRelocatableChunkFunc((void*)RenderVehicleReallyLowDetailCB);
	base::RegisterRelocatableChunkFunc((void*)RenderVehicleHiDetailCB);
	base::RegisterRelocatableChunkFunc((void*)RenderVehicleHiDetailAlphaCB);
	base::RegisterRelocatableChunkFunc((void*)RenderTrainHiDetailCB);
	base::RegisterRelocatableChunkFunc((void*)RenderTrainHiDetailAlphaCB);
	base::RegisterRelocatableChunkFunc((void*)RenderWheelAtomicCB);
	base::RegisterRelocatableChunkFunc((void*)RenderVehicleRotorAlphaCB);
	base::RegisterRelocatableChunkFunc((void*)RenderVehicleTailRotorAlphaCB);
	base::RegisterRelocatableChunkFunc((void*)RenderVehicleReallyLowDetailCB_BigVehicle);
	base::RegisterRelocatableChunkFunc((void*)RenderVehicleLowDetailCB_BigVehicle);
	base::RegisterRelocatableChunkFunc((void*)RenderVehicleHiDetailCB_BigVehicle);
	base::RegisterRelocatableChunkFunc((void*)RenderVehicleLowDetailAlphaCB_BigVehicle);
	base::RegisterRelocatableChunkFunc((void*)RenderVehicleHiDetailAlphaCB_BigVehicle);
	base::RegisterRelocatableChunkFunc((void*)RenderVehicleHiDetailCB_Boat);
	base::RegisterRelocatableChunkFunc((void*)RenderVehicleLoDetailCB_Boat);
	base::RegisterRelocatableChunkFunc((void*)RenderVehicleHiDetailCB_Boat_Far);
	base::RegisterRelocatableChunkFunc((void*)RenderVehicleLoDetailCB_Boat_Far);
	base::RegisterRelocatableChunkFunc((void*)RenderPedCB);
}

void
CVisibilityPlugins::Shutdown(void)
{
	m_alphaList.Shutdown();
//	m_alphaBoatAtomicList.Shutdown();
	m_alphaEntityList.Shutdown();
//	m_alphaUnderwaterEntityList.Shutdown();
#ifdef NEW_RENDERER
	m_alphaBuildingList.Shutdown();
#endif
}

void
CVisibilityPlugins::InitAlphaEntityList(void)
{
	m_alphaEntityList.Clear();
//	m_alphaBoatAtomicList.Clear();
//	m_alphaUnderwaterEntityList.Clear();
#ifdef NEW_RENDERER
	m_alphaBuildingList.Clear();
#endif
}

bool
CVisibilityPlugins::InsertEntityIntoSortedList(CEntity *e, float dist)
{
#ifdef FIX_BUGS
	if (!e->m_rwObject) return true;
#endif

	AlphaObjectInfo item;
	item.entity = e;
	item.sort = dist;
#ifdef NEW_RENDERER
	if(!gbPreviewCity && e->IsBuilding())
		return !!m_alphaBuildingList.InsertSorted(item);
#endif
//	if(e->bUnderwater && m_alphaUnderwaterEntityList.InsertSorted(item))
//		return true;
	return !!m_alphaEntityList.InsertSorted(item);
}

void
CVisibilityPlugins::InitAlphaAtomicList(void)
{
	m_alphaList.Clear();
}

bool
CVisibilityPlugins::InsertAtomicIntoSortedList(RpAtomic *a, float dist)
{
	AlphaObjectInfo item;
	item.atomic = a;
	item.sort = dist;
	return !!m_alphaList.InsertSorted(item);
}

/*
bool
CVisibilityPlugins::InsertAtomicIntoBoatSortedList(RpAtomic *a, float dist)
{
	AlphaObjectInfo item;
	item.atomic = a;
	item.sort = dist;
	return !!m_alphaBoatAtomicList.InsertSorted(item);
}
*/

// can't increase this yet unfortunately...
// probably have to fix fading for this so material alpha isn't overwritten
// LCS: VIS_DISTANCE_ALPHA will probably take care of this
#define VEHICLE_LODDIST_MULTIPLIER (TheCamera.GenerationDistMultiplier)
#ifdef _3DS
#define VEHICLE_HIDETAIL_DIST_MULTIPLIER 0.60f
#else
#define VEHICLE_HIDETAIL_DIST_MULTIPLIER 1.0f
#endif

void
CVisibilityPlugins::SetRenderWareCamera(RwCamera *camera)
{
	ms_pCamera = camera;
	ms_pCameraPosn = RwMatrixGetPos(RwFrameGetMatrix(RwCameraGetFrame(camera)));

	if(TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_TOPDOWN ||
	   TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_TOP_DOWN_PED)
		ms_cullCompsDist = 1000000.0f;
	else
		ms_cullCompsDist = sq(TheCamera.LODDistMultiplier * 20.0f);

	float vehicleDetail = 1.0f;
	float pedDetail = 1.0f;
#ifdef _3DS
	if(rw::c3d::stereoControlsActive()){
		vehicleDetail = rw::c3d::performanceModeActive() ? 0.75f : 0.90f;
		pedDetail = rw::c3d::performanceModeActive() ? 0.35f : 0.50f;
	}else if(rw::c3d::performanceModeActive()){
		vehicleDetail = 0.80f;
		pedDetail = 0.55f;
	}
#endif
#ifdef _3DS
	if(rw::c3d::ps2GraphicsEnabled()){
		vehicleDetail *= PS2Graphics3DS::DetailRangeScale;
		pedDetail *= PS2Graphics3DS::DetailRangeScale;
	}
	rw::c3d::setVegetationLodDistance(70.0f * VEHICLE_LODDIST_MULTIPLIER * VEHICLE_HIDETAIL_DIST_MULTIPLIER *
		(rw::c3d::stereoControlsActive() ? 0.75f : 0.80f) *
		(rw::c3d::ps2GraphicsEnabled() ? PS2Graphics3DS::DetailRangeScale : 1.0f));
#endif
	ms_vehicleLod0Dist = sq(70.0f * VEHICLE_LODDIST_MULTIPLIER * VEHICLE_HIDETAIL_DIST_MULTIPLIER * vehicleDetail);
	ms_vehicleLod1Dist = sq(90.0f * VEHICLE_LODDIST_MULTIPLIER);
	ms_vehicleFadeDist = sq(100.0f * VEHICLE_LODDIST_MULTIPLIER);
	ms_bigVehicleLod0Dist = sq(60.0f * VEHICLE_LODDIST_MULTIPLIER * VEHICLE_HIDETAIL_DIST_MULTIPLIER * vehicleDetail);
	ms_bigVehicleLod1Dist = sq(150.0f * VEHICLE_LODDIST_MULTIPLIER);
	ms_pedLodDist = sq(60.0f * TheCamera.LODDistMultiplier * pedDetail);
	ms_pedFadeDist = sq(70.0f * TheCamera.LODDistMultiplier);
}

static float
GetVehicleLod0DistSq(RwFrame *frame, bool bigVehicle)
{
	float limit = bigVehicle ? CVisibilityPlugins::ms_bigVehicleLod0Dist :
		CVisibilityPlugins::ms_vehicleLod0Dist;
#ifdef _3DS
	RwV3d toVehicle;
	RwV3dSub(&toVehicle, &RwFrameGetLTM(frame)->pos, CVisibilityPlugins::ms_pCameraPosn);
	const RwV3d *cameraForward = RwMatrixGetAt(
		RwFrameGetLTM(RwCameraGetFrame(CVisibilityPlugins::ms_pCamera)));
	const float front = Sqrt(limit);
	const float rear = rw::c3d::stereoControlsActive() ? Min(front, bigVehicle ? 16.0f : 12.0f) : front * 0.50f;
	limit = sq(WorldDrawDistance3DS::FrontScale(rear, front,
		Sqrt(RwV3dDotProduct(&toVehicle, &toVehicle)), RwV3dDotProduct(&toVehicle, cameraForward)));
#endif
	return limit;
}

static float DistToCameraSq;
static float PitchToCamera;
static float VehicleLod0DistSq;
static float BigVehicleLod0DistSq;

void
CVisibilityPlugins::SetupVehicleVariables(RpClump *vehicle)
{
	if (RwObjectGetType((RwObject*)vehicle) != rpCLUMP)
		return;
	DistToCameraSq = GetDistanceSquaredFromCamera(RpClumpGetFrame(vehicle));
	VehicleLod0DistSq = GetVehicleLod0DistSq(RpClumpGetFrame(vehicle), false);
	BigVehicleLod0DistSq = GetVehicleLod0DistSq(RpClumpGetFrame(vehicle), true);
	RwV3d distToCam;
	RwV3dSub(&distToCam, ms_pCameraPosn, &RwFrameGetMatrix(RpClumpGetFrame(vehicle))->pos);
	float dist2d = Sqrt(SQR(distToCam.x) + SQR(distToCam.y));
	if(distToCam.z == 0.0f && dist2d == 0.0f)
		PitchToCamera = 0.0f;
	else
		PitchToCamera = Atan2(distToCam.z, dist2d);
}

RpMaterial*
SetAlphaCB(RpMaterial *material, void *data)
{
	((RwRGBA*)RpMaterialGetColor(material))->alpha = (uint8)(uintptr)data;
	return material;
}

RpMaterial*
SetTextureCB(RpMaterial *material, void *data)
{
	RpMaterialSetTexture(material, (RwTexture*)data);
	return material;
}

void
CVisibilityPlugins::RenderAtomicList(CLinkList<AlphaObjectInfo> &list)
{
	CLink<AlphaObjectInfo> *node;
	for(node = list.tail.prev; node != &list.head; node = node->prev)
		RENDERCALLBACK(node->item.atomic);
}

void
CVisibilityPlugins::RenderAlphaAtomics(void)
{
	RenderAtomicList(m_alphaList);
}

/*
//LCS: removed
void
CVisibilityPlugins::RenderBoatAlphaAtomics(void)
{
	SetCullMode(rwCULLMODECULLNONE);
	RenderAtomicList(m_alphaBoatAtomicList);
	SetCullMode(rwCULLMODECULLBACK);
}
*/

void
CVisibilityPlugins::RenderFadingEntities(CLinkList<AlphaObjectInfo> &list)
{
	CLink<AlphaObjectInfo> *node;
	CSimpleModelInfo *mi;
	for(node = list.tail.prev; node != &list.head; node = node->prev){
		CEntity *e = node->item.entity;
		if(e->m_rwObject == nil)
			continue;
#ifdef _3DS
		if(!CRenderer::ShouldRenderAttachedWindowLights(e))
			continue;
#endif
#ifdef EXTENDED_PIPELINES
		if(CustomPipes::bRenderingEnvMap && (e->IsPed() || e->IsVehicle()))
			continue;
#endif
		mi = (CSimpleModelInfo *)CModelInfo::GetModelInfo(e->GetModelIndex());
		if(mi->GetModelType() == MITYPE_SIMPLE && mi->m_noZwrite)
			RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, FALSE);

#ifdef _3DS
		/* LCS night window/light shells are authored as draw-last, no-Z-write
		 * geometry almost coplanar with their building facade.  Offset them at
		 * the actual old-renderer draw point used by 3DS, covering the same
		 * semantic overlay class on every island without touching normal glass. */
		const bool depthOffset = mi->GetModelType() == MITYPE_SIMPLE &&
			mi->m_drawLast && mi->m_noZwrite;
		if(depthOffset)
			C3D_DepthMap(true, -1.0f, 0.0030f);
#endif

#if defined(FIX_BUGS) && !defined(VIS_DISTANCE_ALPHA)
		//LCS: removed, but that's dumb cause it breaks distance fading
		if(e->bDistanceFade){
#ifdef _3DS
			CameraOcclusion3DS::RenderScope cameraFade(e);
#endif
			DeActivateDirectional();
			SetAmbientColours();
			e->bImBeingRendered = true;
			PUSH_RENDERGROUP(mi->GetModelName());
#ifdef _3DS
			RenderFadingAtomic((RpAtomic*)e->m_rwObject,
				CRenderer::GetNew3DSWorldDistance(e, node->item.sort),
				CRenderer::GetNew3DSWorldLodScale(mi, e->GetModelIndex(), e));
#else
			RenderFadingAtomic((RpAtomic*)e->m_rwObject, node->item.sort, 1.0f);
#endif
			POP_RENDERGROUP();
			e->bImBeingRendered = false;
		}else
#endif
		{
#ifdef VIS_DISTANCE_ALPHA
			// BUG: we don't even know if this is a clump
			if(GetClumpAlpha((RpClump*)e->m_rwObject) != 255 ||
			   GetObjectDistanceAlpha(e->m_rwObject) != 255)
				;	// set blend render states
			else
				;	// set default render states
#endif
			CRenderer::RenderOneNonRoad(e);
		}

#ifdef _3DS
		if(depthOffset)
			C3D_DepthMap(true, -1.0f, 0.0f);
#endif

		if(mi->GetModelType() == MITYPE_SIMPLE && mi->m_noZwrite)
			RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	}
}

void
CVisibilityPlugins::RenderFadingEntities(void)
{
	RenderFadingEntities(m_alphaEntityList);
//	RenderBoatAlphaAtomics();
}

void
CVisibilityPlugins::RenderFadingUnderwaterEntities(void)
{
//	RenderFadingEntities(m_alphaUnderwaterEntityList);
}

RpAtomic*
CVisibilityPlugins::RenderWheelAtomicCB(RpAtomic *atomic)
{
	RpAtomic *lodatm;
	float len;
	CSimpleModelInfo *mi;

	mi = GetAtomicModelInfo(atomic);
	len = Sqrt(DistToCameraSq);
#ifdef FIX_BUGS
	len *= 0.5f;	// HACK HACK, LOD wheels look shite
	lodatm = mi->GetAtomicFromDistance(len * TheCamera.LODDistMultiplier / VEHICLE_LODDIST_MULTIPLIER);
#else
	lodatm = mi->GetAtomicFromDistance(len);
#endif
	if(lodatm){
		if(RpAtomicGetGeometry(lodatm) != RpAtomicGetGeometry(atomic))
			RpAtomicSetGeometry(atomic, RpAtomicGetGeometry(lodatm), rpATOMICSAMEBOUNDINGSPHERE);
		RENDERCALLBACK(atomic);
	}
	return atomic;
}

RpAtomic*
CVisibilityPlugins::RenderObjNormalAtomic(RpAtomic *atomic)
{
	RwMatrix *m;
	RwV3d view;
	float len;

	m = RwFrameGetLTM(RpAtomicGetFrame(atomic));
	RwV3dSub(&view, RwMatrixGetPos(m), ms_pCameraPosn);
	len = RwV3dLength(&view);
	if(RwV3dDotProduct(&view, RwMatrixGetUp(m)) < -0.3f*len && len > 8.0f)
		return atomic;
	RENDERCALLBACK(atomic);
	return atomic;
}

RpAtomic*
CVisibilityPlugins::RenderAlphaAtomic(RpAtomic *atomic, int alpha)
{
	RpGeometry *geo;
	uint32 flags;

	geo = RpAtomicGetGeometry(atomic);
	flags = RpGeometryGetFlags(geo);
	RpGeometrySetFlags(geo, flags | rpGEOMETRYMODULATEMATERIALCOLOR);
#ifdef _3DS
	// Compose fades without destroying glass/material alpha. The backend also
	// scales the alpha-test threshold with opacity, avoiding a half-fade cut.
	const rw::c3d::EntityRenderStyle previous = rw::c3d::getEntityRenderStyle();
	rw::c3d::EntityRenderStyle style = previous;
	style.opacity *= Max(0, Min(alpha, 255)) / 255.0f;
	rw::c3d::setEntityRenderStyle(style);
	RENDERCALLBACK(atomic);
	rw::c3d::setEntityRenderStyle(previous);
#else
	RpGeometryForAllMaterials(geo, SetAlphaCB, (void*)alpha);
	RENDERCALLBACK(atomic);
	RpGeometryForAllMaterials(geo, SetAlphaCB, (void*)255);
#endif
	RpGeometrySetFlags(geo, flags);
	return atomic;
}

/*
//LCS: removed
RpAtomic*
CVisibilityPlugins::RenderWeaponCB(RpAtomic *atomic)
{
	RwMatrix *m;
	RwV3d view;
	float maxdist, distsq;
	CSimpleModelInfo *mi;

	mi = GetAtomicModelInfo(atomic);
	m = RwFrameGetLTM(RpAtomicGetFrame(atomic));
	RwV3dSub(&view, RwMatrixGetPos(m), ms_pCameraPosn);
	maxdist = mi->GetLodDistance(0);
	distsq = RwV3dDotProduct(&view, &view);
	if(distsq < maxdist*maxdist)
		RENDERCALLBACK(atomic);
	return atomic;
}
*/

//LCS: removed, but we want it
RpAtomic*
CVisibilityPlugins::RenderFadingAtomic(RpAtomic *atomic, float camdist, float lodScale)
{
	RpAtomic *lodatm;
	float fadefactor;
	float lodCamDist;
	uint32 alpha;
	CSimpleModelInfo *mi;

	mi = GetAtomicModelInfo(atomic);
	if(mi->m_alpha == 0)
		return atomic;
	lodCamDist = camdist / lodScale;
	lodatm = mi->GetAtomicFromDistance(lodCamDist - FADE_DISTANCE);
	/* The reduced New3DS LOD range can put an LCS model just outside every
	 * configured atomic while it is still queued for distance fading.  The
	 * original renderer assumes that this lookup always succeeds and crashes
	 * at RpAtomicGetGeometry below.  Fade the entity's current atomic instead;
	 * it is already resident and is the safest visual fallback for this frame. */
	if(lodatm == nil)
		lodatm = atomic;
	if(mi->m_additive)
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);

	fadefactor = (mi->GetLargestLodDistance() - (lodCamDist - FADE_DISTANCE))/FADE_DISTANCE;
	if(fadefactor > 1.0f)
		fadefactor = 1.0f;
	alpha = mi->m_alpha * Max(0.0f, Min(fadefactor, 1.0f));
	if(alpha == 0){
		if(mi->m_additive)
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
		return atomic;
	}
	if(alpha == 255)
		RENDERCALLBACK(atomic);
	else{
		RpGeometry *geo = RpAtomicGetGeometry(lodatm);
		uint32 flags = RpGeometryGetFlags(geo);
		RpGeometrySetFlags(geo, flags | rpGEOMETRYMODULATEMATERIALCOLOR);
#ifdef _3DS
		if(geo != RpAtomicGetGeometry(atomic))
			RpAtomicSetGeometry(atomic, geo, rpATOMICSAMEBOUNDINGSPHERE); // originally 5 (mistake?)
		RenderAlphaAtomic(atomic, alpha);
#else
		RpGeometryForAllMaterials(geo, SetAlphaCB, (void*)alpha);
		if(geo != RpAtomicGetGeometry(atomic))
			RpAtomicSetGeometry(atomic, geo, rpATOMICSAMEBOUNDINGSPHERE); // originally 5 (mistake?)
		RENDERCALLBACK(atomic);
		RpGeometryForAllMaterials(geo, SetAlphaCB, (void*)255);
#endif
		RpGeometrySetFlags(geo, flags);
	}

	if(mi->m_additive)
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	return atomic;
}


RpAtomic*
CVisibilityPlugins::RenderVehicleHiDetailCB(RpAtomic *atomic)
{
	RwFrame *clumpframe;
	float dot;
	uint32 flags;

	clumpframe = RpClumpGetFrame(RpAtomicGetClump(atomic));
	if(DistToCameraSq < VehicleLod0DistSq){
		flags = GetAtomicId(atomic);
		if(DistToCameraSq > ms_cullCompsDist && (flags & ATOMIC_FLAG_NOCULL) == 0 && PitchToCamera < 0.2f){
			dot = GetDotProductWithCameraVector(RwFrameGetLTM(RpAtomicGetFrame(atomic)),
				RwFrameGetLTM(clumpframe), flags);
			if(dot > 0.0f && ((flags & ATOMIC_FLAG_ANGLECULL) || 0.1f*DistToCameraSq < dot*dot))
				return atomic;
		}
#ifdef VIS_DISTANCE_ALPHA
		if(GetObjectDistanceAlpha((RwObject*)RpAtomicGetClump(atomic)) == 255 ||
		   !InsertAtomicIntoSortedList(atomic, DistToCameraSq))
#endif
		RENDERCALLBACK(atomic);
	}
	return atomic;
}

RpAtomic*
CVisibilityPlugins::RenderVehicleHiDetailAlphaCB(RpAtomic *atomic)
{
	RwFrame *clumpframe;
	float dot;
	uint32 flags;

	clumpframe = RpClumpGetFrame(RpAtomicGetClump(atomic));
	if(DistToCameraSq < VehicleLod0DistSq){
		flags = GetAtomicId(atomic);
		dot = GetDotProductWithCameraVector(RwFrameGetLTM(RpAtomicGetFrame(atomic)),
			RwFrameGetLTM(clumpframe), flags);
		if(DistToCameraSq > ms_cullCompsDist && (flags & ATOMIC_FLAG_NOCULL) == 0 && PitchToCamera < 0.2f)
			if(dot > 0.0f && ((flags & ATOMIC_FLAG_ANGLECULL) || 0.1f*DistToCameraSq < dot*dot))
				return atomic;

		if(flags & ATOMIC_FLAG_DRAWLAST){
			// sort before clump
			if(!InsertAtomicIntoSortedList(atomic, DistToCameraSq - 0.0001f))
				RENDERCALLBACK(atomic);
		}else{
			if(!InsertAtomicIntoSortedList(atomic, DistToCameraSq + dot))
				RENDERCALLBACK(atomic);
		}
	}
	return atomic;
}

RpAtomic*
CVisibilityPlugins::RenderVehicleHiDetailCB_BigVehicle(RpAtomic *atomic)
{
	RwFrame *clumpframe;
	float dot;
	uint32 flags;

	clumpframe = RpClumpGetFrame(RpAtomicGetClump(atomic));
	if(DistToCameraSq < BigVehicleLod0DistSq){
		flags = GetAtomicId(atomic);
		if(DistToCameraSq > ms_cullCompsDist && (flags & ATOMIC_FLAG_NOCULL) == 0 && PitchToCamera < 0.2f){
			dot = GetDotProductWithCameraVector(RwFrameGetLTM(RpAtomicGetFrame(atomic)),
				RwFrameGetLTM(clumpframe), flags);
			if(dot > 0.0f)
				return atomic;
		}
		RENDERCALLBACK(atomic);
	}
	return atomic;
}

RpAtomic*
CVisibilityPlugins::RenderVehicleHiDetailAlphaCB_BigVehicle(RpAtomic *atomic)
{
	RwFrame *clumpframe;
	float dot;
	uint32 flags;

	clumpframe = RpClumpGetFrame(RpAtomicGetClump(atomic));
	if(DistToCameraSq < BigVehicleLod0DistSq){
		flags = GetAtomicId(atomic);
		dot = GetDotProductWithCameraVector(RwFrameGetLTM(RpAtomicGetFrame(atomic)),
			RwFrameGetLTM(clumpframe), flags);
		if(DistToCameraSq > ms_cullCompsDist && (flags & ATOMIC_FLAG_NOCULL) == 0 && PitchToCamera < 0.2f)
			if(dot > 0.0f && ((flags & ATOMIC_FLAG_ANGLECULL) || 0.1f*DistToCameraSq < dot*dot))
				return atomic;

		if(!InsertAtomicIntoSortedList(atomic, DistToCameraSq + dot))
			RENDERCALLBACK(atomic);
	}
	return atomic;
}

RpAtomic*
CVisibilityPlugins::RenderVehicleHiDetailCB_Boat(RpAtomic *atomic)
{
	if(DistToCameraSq < VehicleLod0DistSq)
		RENDERCALLBACK(atomic);
	return atomic;
}

RpAtomic*
CVisibilityPlugins::RenderVehicleHiDetailCB_Boat_Far(RpAtomic *atomic)
{
	if(DistToCameraSq < ms_bigVehicleLod1Dist)
		RENDERCALLBACK(atomic);
	return atomic;
}

/*
//LCS: removed
RpAtomic*
CVisibilityPlugins::RenderVehicleHiDetailAlphaCB_Boat(RpAtomic *atomic)
{
	if(DistToCameraSq < ms_vehicleLod0Dist){
		if(GetAtomicId(atomic) & ATOMIC_FLAG_DRAWLAST){
			if(!InsertAtomicIntoBoatSortedList(atomic, DistToCameraSq))
				RENDERCALLBACK(atomic);
		}else
			RENDERCALLBACK(atomic);
	}
	return atomic;
}
*/

RpAtomic*
CVisibilityPlugins::RenderVehicleLoDetailCB_Boat(RpAtomic *atomic)
{
	RpClump *clump;
	int32 alpha;

	clump = RpAtomicGetClump(atomic);
	if(DistToCameraSq >= VehicleLod0DistSq){
		alpha = GetClumpAlpha(clump);
	//	if(alpha == 255)
	//		RENDERCALLBACK(atomic);
	//	else
			RenderAlphaAtomic(atomic, alpha);
	}
	return atomic;
}

RpAtomic*
CVisibilityPlugins::RenderVehicleLoDetailCB_Boat_Far(RpAtomic *atomic)
{
	RpClump *clump;
	int32 alpha;

	clump = RpAtomicGetClump(atomic);
	if(DistToCameraSq >= ms_bigVehicleLod1Dist){
		alpha = GetClumpAlpha(clump);
	//	if(alpha == 255)
	//		RENDERCALLBACK(atomic);
	//	else
			RenderAlphaAtomic(atomic, alpha);
	}
	return atomic;
}

RpAtomic*
CVisibilityPlugins::RenderVehicleLowDetailCB_BigVehicle(RpAtomic *atomic)
{
	RwFrame *clumpframe;
	float dot;
	uint32 flags;

	clumpframe = RpClumpGetFrame(RpAtomicGetClump(atomic));
	if(DistToCameraSq >= BigVehicleLod0DistSq &&
	   DistToCameraSq < ms_bigVehicleLod1Dist){
		flags = GetAtomicId(atomic);
		if(DistToCameraSq > ms_cullCompsDist && (flags & ATOMIC_FLAG_NOCULL) == 0 && PitchToCamera < 0.2f){
			dot = GetDotProductWithCameraVector(RwFrameGetLTM(RpAtomicGetFrame(atomic)),
				RwFrameGetLTM(clumpframe), flags);
			if(dot > 0.0f)
				return atomic;
		}
		RENDERCALLBACK(atomic);
	}
	return atomic;
}

RpAtomic*
CVisibilityPlugins::RenderVehicleLowDetailAlphaCB_BigVehicle(RpAtomic *atomic)
{
	RwFrame *clumpframe;
	float dot;
	uint32 flags;

	clumpframe = RpClumpGetFrame(RpAtomicGetClump(atomic));
	if(DistToCameraSq >= BigVehicleLod0DistSq &&
	   DistToCameraSq < ms_bigVehicleLod1Dist){
		flags = GetAtomicId(atomic);
		dot = GetDotProductWithCameraVector(RwFrameGetLTM(RpAtomicGetFrame(atomic)),
			RwFrameGetLTM(clumpframe), flags);
		if(dot > 0.0f)
			if(DistToCameraSq > ms_cullCompsDist && (flags & ATOMIC_FLAG_NOCULL) == 0 && PitchToCamera < 0.2f)
				return atomic;

		if(!InsertAtomicIntoSortedList(atomic, DistToCameraSq + dot))
			RENDERCALLBACK(atomic);
	}
	return atomic;
}

RpAtomic*
CVisibilityPlugins::RenderVehicleReallyLowDetailCB(RpAtomic *atomic)
{
	RpClump *clump;
	int32 alpha;

	clump = RpAtomicGetClump(atomic);
	if(DistToCameraSq >= VehicleLod0DistSq){
		alpha = GetClumpAlpha(clump);
//		if(alpha == 255)
//			RENDERCALLBACK(atomic);
//		else
			RenderAlphaAtomic(atomic, alpha);
	}
	return atomic;

}

RpAtomic*
CVisibilityPlugins::RenderVehicleReallyLowDetailCB_BigVehicle(RpAtomic *atomic)
{
	if(DistToCameraSq >= ms_bigVehicleLod1Dist)
		RENDERCALLBACK(atomic);
	return atomic;
}

RpAtomic*
CVisibilityPlugins::RenderTrainHiDetailCB(RpAtomic *atomic)
{
	RwFrame *clumpframe;
	float dot;
	uint32 flags;

	clumpframe = RpClumpGetFrame(RpAtomicGetClump(atomic));
	if(DistToCameraSq < ms_bigVehicleLod1Dist){
		flags = GetAtomicId(atomic);
		if(DistToCameraSq > ms_cullCompsDist && (flags & ATOMIC_FLAG_NOCULL) == 0 && PitchToCamera < 0.2f){
			dot = GetDotProductWithCameraVector(RwFrameGetLTM(RpAtomicGetFrame(atomic)),
				RwFrameGetLTM(clumpframe), flags);
			if(dot > 0.0f && ((flags & ATOMIC_FLAG_ANGLECULL) || 0.1f*DistToCameraSq < dot*dot))
				return atomic;
		}
		RENDERCALLBACK(atomic);
	}
	return atomic;
}

RpAtomic*
CVisibilityPlugins::RenderTrainHiDetailAlphaCB(RpAtomic *atomic)
{
	RwFrame *clumpframe;
	float dot;
	uint32 flags;

	clumpframe = RpClumpGetFrame(RpAtomicGetClump(atomic));
	if(DistToCameraSq < ms_bigVehicleLod1Dist){
		flags = GetAtomicId(atomic);
		dot = GetDotProductWithCameraVector(RwFrameGetLTM(RpAtomicGetFrame(atomic)),
			RwFrameGetLTM(clumpframe), flags);
		if(DistToCameraSq > ms_cullCompsDist && (flags & ATOMIC_FLAG_NOCULL) == 0 && PitchToCamera < 0.2f)
			if(dot > 0.0f && ((flags & ATOMIC_FLAG_ANGLECULL) || 0.1f*DistToCameraSq < dot*dot))
				return atomic;

		if(flags & ATOMIC_FLAG_DRAWLAST){
			if(!InsertAtomicIntoSortedList(atomic, DistToCameraSq))
				RENDERCALLBACK(atomic);
		}else{
			if(!InsertAtomicIntoSortedList(atomic, DistToCameraSq + dot))
				RENDERCALLBACK(atomic);
		}
	}
	return atomic;
}

RpAtomic*
CVisibilityPlugins::RenderVehicleRotorAlphaCB(RpAtomic *atomic)
{
	RwFrame *clumpframe;
	float dot;
	RwV3d cam2atm;

	clumpframe = RpClumpGetFrame(RpAtomicGetClump(atomic));
	if(DistToCameraSq < ms_bigVehicleLod1Dist){
		RwV3dSub(&cam2atm, &RwFrameGetLTM(RpAtomicGetFrame(atomic))->pos, ms_pCameraPosn);
		dot = RwV3dDotProduct(&cam2atm, &RwFrameGetLTM(clumpframe)->at);
		if(!InsertAtomicIntoSortedList(atomic, DistToCameraSq + dot*20.0f))
			RENDERCALLBACK(atomic);
	}
	return atomic;
}

RpAtomic*
CVisibilityPlugins::RenderVehicleTailRotorAlphaCB(RpAtomic *atomic)
{
	RwMatrix *clumpMat, *atmMat;
	float dot;
	RwV3d cam2atm;

	if(DistToCameraSq < BigVehicleLod0DistSq){
		atmMat = RwFrameGetLTM(RpAtomicGetFrame(atomic));
		clumpMat = RwFrameGetLTM(RpClumpGetFrame(RpAtomicGetClump(atomic)));
		RwV3dSub(&cam2atm, &atmMat->pos, ms_pCameraPosn);
		dot = RwV3dDotProduct(&cam2atm, &clumpMat->up) + RwV3dDotProduct(&cam2atm, &clumpMat->right);
		if(!InsertAtomicIntoSortedList(atomic, DistToCameraSq - dot))
			RENDERCALLBACK(atomic);
	}
	return atomic;
}

/*
RpAtomic*
CVisibilityPlugins::RenderPlayerCB(RpAtomic *atomic)
{
	if(CWorld::Players[0].m_pSkinTexture)
		RpGeometryForAllMaterials(RpAtomicGetGeometry(atomic), SetTextureCB, CWorld::Players[0].m_pSkinTexture);
	RENDERCALLBACK(atomic);
	return atomic;
}
*/

RpAtomic*
CVisibilityPlugins::RenderPedCB(RpAtomic *atomic)
{
	RpClump *clump;
	float dist;
	int32 alpha;

	clump = RpAtomicGetClump(atomic);
	dist = GetDistanceSquaredFromCamera(RpClumpGetFrame(clump));
	float limitSq = ms_pedLodDist;
#ifdef _3DS
	const bool smoothDistance = true;
	const float distance = Sqrt(dist);
	float limit = Sqrt(limitSq);
	if(smoothDistance){
		RwV3d offset;
		RwV3dSub(&offset, &RwFrameGetLTM(RpClumpGetFrame(clump))->pos, ms_pCameraPosn);
		const RwV3d *forward = RwMatrixGetAt(RwFrameGetLTM(RwCameraGetFrame(ms_pCamera)));
		const float frontLimit = Max(limit, 60.0f * TheCamera.LODDistMultiplier * 0.75f);
		limit = WorldDrawDistance3DS::FrontScale(rw::c3d::stereoControlsActive() ? limit : limit * 0.50f, frontLimit,
			distance, RwV3dDotProduct(&offset, forward));
		limitSq = sq(limit);
	}
#endif
	if(dist < limitSq){
		alpha = GetClumpAlpha(clump);
#ifdef _3DS
		if(smoothDistance) alpha = WorldDrawDistance3DS::FadeAlpha(distance, limit, alpha);
#endif
//		if(alpha == 255)
//			RENDERCALLBACK(atomic);
//		else
			RenderAlphaAtomic(atomic, alpha);
	}
	return atomic;
}

float
CVisibilityPlugins::GetDistanceSquaredFromCamera(RwV3d *pos)
{
	RwV3d dist;
	RwV3dSub(&dist, pos, ms_pCameraPosn);
	return RwV3dDotProduct(&dist, &dist);
}

float
CVisibilityPlugins::GetDistanceSquaredFromCamera(RwFrame *frame)
{
	RwMatrix *m;
	RwV3d dist;
	m = RwFrameGetLTM(frame);
	RwV3dSub(&dist, RwMatrixGetPos(m), ms_pCameraPosn);
	return RwV3dDotProduct(&dist, &dist);
}

float
CVisibilityPlugins::GetDotProductWithCameraVector(RwMatrix *atomicMat, RwMatrix *clumpMat, uint32 flags)
{
	RwV3d dist;
	float dot, dotdoor;

	// Vehicle forward is the y axis (RwMatrix.up)
	// Vehicle right is the x axis (RwMatrix.right)

	RwV3dSub(&dist, RwMatrixGetPos(atomicMat), ms_pCameraPosn);
	// forward/backward facing
	if(flags & (ATOMIC_FLAG_FRONT | ATOMIC_FLAG_REAR))
		dot = RwV3dDotProduct(&dist, RwMatrixGetUp(clumpMat));
	// left/right facing
	else if(flags & (ATOMIC_FLAG_LEFT | ATOMIC_FLAG_RIGHT))
		dot = RwV3dDotProduct(&dist, RwMatrixGetRight(clumpMat));
	else
		dot = 0.0f;
	if(flags & (ATOMIC_FLAG_LEFT | ATOMIC_FLAG_REAR))
		dot = -dot;

	if(flags & (ATOMIC_FLAG_REARDOOR | ATOMIC_FLAG_FRONTDOOR)){
		if(flags & ATOMIC_FLAG_REARDOOR)
			dotdoor = -RwV3dDotProduct(&dist, RwMatrixGetUp(clumpMat));
		else if(flags & ATOMIC_FLAG_FRONTDOOR)
			dotdoor = RwV3dDotProduct(&dist, RwMatrixGetUp(clumpMat));
		else
			dotdoor = 0.0f;

		if(dot < 0.0f && dotdoor < 0.0f)
			dot += dotdoor;
		if(dot > 0.0f && dotdoor > 0.0f)
			dot += dotdoor;
	}

	return dot;
}

/* These are all unused */

bool
CVisibilityPlugins::DefaultVisibilityCB(RpClump *clump)
{
	return true;
}

bool
CVisibilityPlugins::FrustumSphereCB(RpClump *clump)
{
	return true;
}

bool
CVisibilityPlugins::MloVisibilityCB(RpClump *clump)
{
	RwFrame *frame = RpClumpGetFrame(clump);
	CMloModelInfo *modelInfo = (CMloModelInfo*)GetFrameHierarchyId(frame);
	if (SQR(modelInfo->drawDist) < GetDistanceSquaredFromCamera(frame))
		return false;
	return CVisibilityPlugins::FrustumSphereCB(clump);
}

bool
CVisibilityPlugins::VehicleVisibilityCB(RpClump *clump)
{
	RwFrame *frame = RpClumpGetFrame(clump);
	if (ms_vehicleLod1Dist < GetDistanceSquaredFromCamera(frame))
		return false;
	return FrustumSphereCB(clump);
}

bool
CVisibilityPlugins::VehicleVisibilityCB_BigVehicle(RpClump *clump)
{
	return FrustumSphereCB(clump);
}




//
// RW Plugins
//

enum
{
	ID_VISIBILITYATOMIC = MAKECHUNKID(rwVENDORID_ROCKSTAR, 0x00),
	ID_VISIBILITYCLUMP  = MAKECHUNKID(rwVENDORID_ROCKSTAR, 0x01),
	ID_VISIBILITYFRAME  = MAKECHUNKID(rwVENDORID_ROCKSTAR, 0x02),
};

bool
CVisibilityPlugins::PluginAttach(void)
{
	ms_atomicPluginOffset = RpAtomicRegisterPlugin(sizeof(AtomicExt),
		ID_VISIBILITYATOMIC,
		AtomicConstructor, AtomicDestructor, AtomicCopyConstructor);

	ms_framePluginOffset = RwFrameRegisterPlugin(sizeof(FrameExt),
		ID_VISIBILITYFRAME,
		FrameConstructor, FrameDestructor, FrameCopyConstructor);

	ms_clumpPluginOffset = RpClumpRegisterPlugin(sizeof(ClumpExt),
		ID_VISIBILITYCLUMP,
		ClumpConstructor, ClumpDestructor, ClumpCopyConstructor);
	return ms_atomicPluginOffset != -1 && ms_clumpPluginOffset != -1;
}

#define ATOMICEXT(o) (RWPLUGINOFFSET(CVisibilityPlugins::AtomicExt, o, CVisibilityPlugins::ms_atomicPluginOffset))
#define FRAMEEXT(o) (RWPLUGINOFFSET(CVisibilityPlugins::FrameExt, o, CVisibilityPlugins::ms_framePluginOffset))
#define CLUMPEXT(o) (RWPLUGINOFFSET(CVisibilityPlugins::ClumpExt, o, CVisibilityPlugins::ms_clumpPluginOffset))

bool
CVisibilityPlugins::IsVehicleHighDetail(RpClump *vehicle, bool bigVehicle)
{
	float distance = GetDistanceSquaredFromCamera(RpClumpGetFrame(vehicle));
	float limit = GetVehicleLod0DistSq(RpClumpGetFrame(vehicle), bigVehicle);
	return distance < limit;
}

//
// Atomic
//

void*
CVisibilityPlugins::AtomicConstructor(void *object, int32, int32)
{
	ATOMICEXT(object)->modelId = -1;
	ATOMICEXT(object)->flags = 0;
#ifdef VIS_DISTANCE_ALPHA
	// This seems strange, want to start out invisible before fading in
	// but maybe it's set elsewhere?
	ATOMICEXT(object)->distanceAlpha = 255;
#endif
	return object;
}

void*
CVisibilityPlugins::AtomicDestructor(void *object, int32, int32)
{
	return object;
}

void*
CVisibilityPlugins::AtomicCopyConstructor(void *dst, const void *src, int32, int32)
{
	*ATOMICEXT(dst) = *ATOMICEXT(src);
	return dst;
}

void
CVisibilityPlugins::SetAtomicModelInfo(RpAtomic *atomic,
                                       CSimpleModelInfo *modelInfo)
{
	int id;

	for(id = 0; id < MODELINFOSIZE; id++)
		if(CModelInfo::GetModelInfo(id) == modelInfo){
			ATOMICEXT(atomic)->modelId = id;
			return;
		}
	ATOMICEXT(atomic)->modelId = -1;
}

void
CVisibilityPlugins::SetAtomicModelIndex(RpAtomic *atomic, int modelId)
{
	ATOMICEXT(atomic)->modelId = modelId;
}

CSimpleModelInfo*
CVisibilityPlugins::GetAtomicModelInfo(RpAtomic *atomic)
{
	int id = ATOMICEXT(atomic)->modelId;
	if(id == -1)
		return nil;
	return (CSimpleModelInfo*)CModelInfo::GetModelInfo(id);
}

void
CVisibilityPlugins::SetAtomicFlag(RpAtomic *atomic, int f)
{
	ATOMICEXT(atomic)->flags |= f;
}

void
CVisibilityPlugins::ClearAtomicFlag(RpAtomic *atomic, int f)
{
	ATOMICEXT(atomic)->flags &= ~f;
}

void
CVisibilityPlugins::SetAtomicId(RpAtomic *atomic, int id)
{
	ATOMICEXT(atomic)->flags = id;
}

int
CVisibilityPlugins::GetAtomicId(RpAtomic *atomic)
{
	return ATOMICEXT(atomic)->flags;
}

void
CVisibilityPlugins::SetAtomicRenderCallback(RpAtomic *atomic, RpAtomicCallBackRender cb)
{
	if(cb == nil)
		cb = RENDERCALLBACK;	// not necessary
	RpAtomicSetRenderCallBack(atomic, cb);
}

//
// Frame
//

void*
CVisibilityPlugins::FrameConstructor(void *object, int32, int32)
{
	FRAMEEXT(object)->id = 0;
	return object;
}

void*
CVisibilityPlugins::FrameDestructor(void *object, int32, int32)
{
	return object;
}

void*
CVisibilityPlugins::FrameCopyConstructor(void *dst, const void *src, int32, int32)
{
	*FRAMEEXT(dst) = *FRAMEEXT(src);
	return dst;
}

void
CVisibilityPlugins::SetFrameHierarchyId(RwFrame *frame, intptr id)
{
	FRAMEEXT(frame)->id = id;
}

intptr
CVisibilityPlugins::GetFrameHierarchyId(RwFrame *frame)
{
	return FRAMEEXT(frame)->id;
}


//
// Clump
//

void*
CVisibilityPlugins::ClumpConstructor(void *object, int32, int32)
{
	ClumpExt *ext = CLUMPEXT(object);
	ext->visibilityCB = DefaultVisibilityCB;
	ext->alpha = 0xFF;
	return object;
}

void*
CVisibilityPlugins::ClumpDestructor(void *object, int32, int32)
{
	return object;
}

void*
CVisibilityPlugins::ClumpCopyConstructor(void *dst, const void *src, int32, int32)
{
	CLUMPEXT(dst)->visibilityCB = CLUMPEXT(src)->visibilityCB;
	return dst;
}

void
CVisibilityPlugins::SetClumpModelInfo(RpClump *clump, CClumpModelInfo *modelInfo)
{
	CVehicleModelInfo *vmi;
	SetFrameHierarchyId(RpClumpGetFrame(clump), (intptr)modelInfo);

	// Unused
	switch (modelInfo->GetModelType()) {
	case MITYPE_MLO:
		CLUMPEXT(clump)->visibilityCB = MloVisibilityCB;
		break;
	case MITYPE_VEHICLE:
		vmi = (CVehicleModelInfo*)modelInfo;
		if(vmi->m_vehicleType == VEHICLE_TYPE_TRAIN ||
		   vmi->m_vehicleType == VEHICLE_TYPE_HELI ||
		   vmi->m_vehicleType == VEHICLE_TYPE_PLANE)
			CLUMPEXT(clump)->visibilityCB = VehicleVisibilityCB_BigVehicle;
		else
			CLUMPEXT(clump)->visibilityCB = VehicleVisibilityCB;
		break;
	default: break;
	}
}

CClumpModelInfo*
CVisibilityPlugins::GetClumpModelInfo(RpClump *clump)
{
	return (CClumpModelInfo*)GetFrameHierarchyId(RpClumpGetFrame(clump));
}

void
CVisibilityPlugins::SetClumpAlpha(RpClump *clump, int alpha)
{
	CLUMPEXT(clump)->alpha = alpha;
}

int
CVisibilityPlugins::GetClumpAlpha(RpClump *clump)
{
	return CLUMPEXT(clump)->alpha;
}

bool
CVisibilityPlugins::IsClumpVisible(RpClump *clump)
{
	return CLUMPEXT(clump)->visibilityCB(clump);
}

#ifdef VIS_DISTANCE_ALPHA
// LCS walks the atomic list manually but we want to be compatible with both RW and librw,
// so this code isn't quite original and uses callbacks instead.
static RpAtomic*
SetAtomicDistanceAlphaCB(RpAtomic *atomic, void *data)
{
	ATOMICEXT(atomic)->distanceAlpha = *(int*)data;
	return atomic;
}
void
CVisibilityPlugins::SetClumpDistanceAlpha(RpClump *clump, int alpha)
{
	RpClumpForAllAtomics(clump, SetAtomicDistanceAlphaCB, &alpha);
}

static RpAtomic*
GetAtomicDistanceAlphaCB(RpAtomic *atomic, void *data)
{
	*(int*)data = ATOMICEXT(atomic)->distanceAlpha;
	return atomic;
}
int
CVisibilityPlugins::GetClumpDistanceAlpha(RpClump *clump)
{
	int alpha = 255;
	RpClumpForAllAtomics(clump, GetAtomicDistanceAlphaCB, &alpha);
	return alpha;
}




void
CVisibilityPlugins::SetObjectDistanceAlpha(RwObject *object, int alpha)
{
	if(object == nil)
		return;
	if(RwObjectGetType(object) == rpATOMIC)
		ATOMICEXT(object)->distanceAlpha = alpha;
	else
		SetClumpDistanceAlpha((RpClump*)object, alpha);
}

int
CVisibilityPlugins::GetObjectDistanceAlpha(RwObject *object)
{
	if(object == nil)
		return 255;
	if(RwObjectGetType(object) == rpATOMIC)
		return ATOMICEXT(object)->distanceAlpha;
	else
		return GetClumpDistanceAlpha((RpClump*)object);
}
#endif
