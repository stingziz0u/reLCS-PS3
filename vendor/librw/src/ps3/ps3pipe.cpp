// ps3pipe.cpp -- object pipelines of the RSX renderer: instancing into VRAM
// and the render callbacks of the default, skin and MatFX pipelines
// (librw's gl3 ones: gl3pipe.cpp, gl3render.cpp, gl3skin.cpp, gl3matfx.cpp).
//
// Instancing builds the vertex buffer in main memory and copies it to VRAM in
// one go (the RSX reads it from there; the CPU only writes). Indices are
// 16 bit, native (big-endian), drawn with rsxDrawIndexArray.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwrender.h"
#include "../rwengine.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwanim.h"
#include "../rwplugins.h"
#include "rwps3.h"
#ifdef RW_PS3_RSX
#include "ps3rsx.h"
#endif

// patch 22: a crumb per draw while RenderScene runs (common.h, ps3_log.cpp)
extern "C" void PS3_Crumb(const char *tag, int val);
extern "C" int PS3_crumbRender;
#define RW_CRUMB(tag, val) do { if(PS3_crumbRender) PS3_Crumb(tag, val); } while(0)

namespace rw {
namespace ps3 {

#ifndef RW_PS3_RSX

void *destroyNativeData(void *object, int32, int32) { return object; }
void initSkin(void) { }
void initMatFX(void) { }

#else

// attribute slots: the ATTRn of the vertex programs (shaders/)
enum {
	ATTR_POS = GCM_VERTEX_ATTRIB_POS,		// 0
	ATTR_WEIGHTS = GCM_VERTEX_ATTRIB_WEIGHT,	// 1
	ATTR_NORMAL = GCM_VERTEX_ATTRIB_NORMAL,		// 2
	ATTR_COLOR = GCM_VERTEX_ATTRIB_COLOR0,		// 3
	ATTR_INDICES = 7,
	ATTR_TEX0 = GCM_VERTEX_ATTRIB_TEX0,		// 8
	NUM_ATTRS = 16
};

struct AttribDesc
{
	uint8 index;
	uint8 type;	// GCM_VERTEX_DATA_TYPE_*
	uint8 size;
	uint8 offset;
};

struct InstanceData
{
	uint32    numIndex;
	uint32    minVert;
	int32     numVertices;
	Material *material;
	bool32    vertexAlpha;
	uint32    offset;	// in the index buffer, bytes
	bool32    decal;	// patch 94: its texture's name has "shad" in it (the road shadows)
};

// patch 94: a shadow overlay mesh, by its texture's name (1). Patch 96:
// 2 for the ground overlays of LCS's roads and pavements (dirtywhite,
// cover7_64, crackedgroundb, gridwire64HV), drawn on the road's plane
static bool32
isShadowTexture(Material *m)
{
	if(m == nil || m->texture == nil)
		return 0;
	const char *name = m->texture->name;
	for(const char *n = name; *n; n++)
		if(strncasecmp(n, "shad", 4) == 0)
			return 1;
	static const char *const overlays[] = { "dirtywhite", "cover7", "cracked", "gridwire" };
	for(int i = 0; i < 4; i++)
		if(strncasecmp(name, overlays[i], strlen(overlays[i])) == 0)
			return 2;
	return 0;
}

struct InstanceDataHeader : rw::InstanceDataHeader
{
	uint32      serialNumber;
	uint32      numMeshes;
	uint32      primType;	// GCM_TYPE_*
	uint32      totalNumIndex;
	uint32      totalNumVertex;
	int32       numAttribs;
	AttribDesc  attribs[8];
	uint32      stride;
	VramAlloc   vb;
	VramAlloc   ib;
	InstanceData *inst;
};

class ObjPipeline : public rw::ObjPipeline
{
public:
	void init(void);
	static ObjPipeline *create(void);

	void (*instanceCB)(Geometry *geo, InstanceDataHeader *header, bool32 reinstance);
	void (*renderCB)(Atomic *atomic, InstanceDataHeader *header);
};

// ---- instance data --------------------------------------------------------------

static void
freeInstanceData(Geometry *geometry)
{
	if(geometry->instData == nil ||
	   geometry->instData->platform != PLATFORM_PS3)
		return;
	InstanceDataHeader *header = (InstanceDataHeader*)geometry->instData;
	geometry->instData = nil;
	vramFree(&header->vb);
	vramFree(&header->ib);
	rwFree(header->inst);
	rwFree(header);
}

void*
destroyNativeData(void *object, int32, int32)
{
	freeInstanceData((Geometry*)object);
	return object;
}

// ---- res-PS3 patch 119: the windings ----------------------------------------
// The converted LCS meshes are triangle strips, and some strips (a part of
// FS_custroadA1 at Staunton's traffic light: a dark wedge, the road seen only
// from below) come out with their triangles turned the other way: with
// backface culling the GPU throws them away. The geometry's own triangle list
// has them right (the camera ray: the normal up, turned away from below). So
// every mesh is drawn as a triangle list, each triangle in the winding of the
// geometry triangle with the same corners. One with both windings in the
// geometry (double sided) or none is kept as the strip gives it.
bool windFix = true;
uint32 windFixGeos, windFixTris;

struct WindSlot { uint64 key; int8 dir; };	// dir: 1 / -1, 2 both (keep)

static inline uint64
windKey(uint32 a, uint32 b, uint32 c, int *dir)
{
	// sorted corners; dir: whether (a,b,c) is an even rotation of the sorted order
	uint32 lo = a, mid = b, hi = c;
	if(lo > mid){ uint32 t = lo; lo = mid; mid = t; }
	if(mid > hi){ uint32 t = mid; mid = hi; hi = t; }
	if(lo > mid){ uint32 t = lo; lo = mid; mid = t; }
	// (a,b,c) is an even rotation of (lo,mid,hi) when, starting from lo, mid comes next
	bool even = (a == lo && b == mid) || (b == lo && c == mid) || (c == lo && a == mid);
	*dir = even ? 1 : -1;
	return ((uint64)lo << 32) | ((uint64)mid << 16) | hi;
}

static WindSlot*
windTableBuild(Geometry *geo, uint32 *maskOut)
{
	uint32 size = 16;
	while(size < (uint32)geo->numTriangles*2) size <<= 1;
	WindSlot *t = (WindSlot*)rwMalloc(size*sizeof(WindSlot), MEMDUR_FUNCTION | ID_GEOMETRY);
	if(t == nil)
		return nil;
	for(uint32 i = 0; i < size; i++) t[i].key = ~0ull;
	uint32 mask = size - 1;
	for(int32 i = 0; i < geo->numTriangles; i++){
		Triangle *tr = &geo->triangles[i];
		if(tr->v[0] == tr->v[1] || tr->v[1] == tr->v[2] || tr->v[0] == tr->v[2])
			continue;
		int dir;
		uint64 k = windKey(tr->v[0], tr->v[1], tr->v[2], &dir);
		uint32 h = (uint32)((k * 0x9E3779B97F4A7C15ull) >> 40) & mask;
		while(t[h].key != ~0ull && t[h].key != k) h = (h + 1) & mask;
		if(t[h].key == k){
			if(t[h].dir != dir) t[h].dir = 2;
		}else{
			t[h].key = k;
			t[h].dir = dir;
		}
	}
	*maskOut = mask;
	return t;
}

static int
windLookup(WindSlot *t, uint32 mask, uint64 k)
{
	uint32 h = (uint32)((k * 0x9E3779B97F4A7C15ull) >> 40) & mask;
	while(t[h].key != ~0ull){
		if(t[h].key == k) return t[h].dir;
		h = (h + 1) & mask;
	}
	return 0;
}

// a strip (or a list) to a list in the geometry's windings; returns the count
static uint32
windMesh(Mesh *mesh, bool strip, WindSlot *t, uint32 mask, uint16 *out, uint32 *turned)
{
	uint32 n = 0;
	uint16 *ix = mesh->indices;
	uint32 count = strip ? (mesh->numIndices >= 3 ? mesh->numIndices - 2 : 0) : mesh->numIndices/3;
	for(uint32 i = 0; i < count; i++){
		uint32 a, b, c;
		if(strip){
			a = ix[i]; b = ix[i+1]; c = ix[i+2];
			if(i & 1){ uint32 x = a; a = b; b = x; }	// the GPU's winding of an odd strip triangle
		}else{
			a = ix[i*3]; b = ix[i*3+1]; c = ix[i*3+2];
		}
		if(a == b || b == c || a == c)
			continue;	// a strip's joint
		int dir;
		uint64 k = windKey(a, b, c, &dir);
		int want = windLookup(t, mask, k);
		if((want == 1 || want == -1) && want != dir){
			uint32 x = a; a = b; b = x;
			(*turned)++;
		}
		out[n++] = a; out[n++] = b; out[n++] = c;
	}
	return n;
}

static InstanceDataHeader*
instanceMesh(rw::ObjPipeline *rwpipe, Geometry *geo)
{
	InstanceDataHeader *header = rwNewT(InstanceDataHeader, 1, MEMDUR_EVENT | ID_GEOMETRY);
	memset(header, 0, sizeof(*header));
	MeshHeader *meshh = geo->meshHeader;
	geo->instData = header;
	header->platform = PLATFORM_PS3;

	header->serialNumber = meshh->serialNum;
	header->numMeshes = meshh->numMeshes;
	header->primType = meshh->flags == 1 ? GCM_TYPE_TRIANGLE_STRIP : GCM_TYPE_TRIANGLES;
	header->totalNumVertex = geo->numVertices;
	header->totalNumIndex = meshh->totalIndices;
	header->inst = rwNewT(InstanceData, header->numMeshes, MEMDUR_EVENT | ID_GEOMETRY);

	// patch 119: the windings checked against the geometry's triangles; only
	// a geometry with a turned triangle is drawn as lists (the rest as before)
	if(windFix && geo->triangles && geo->numTriangles > 0 && geo->numVertices <= 0xFFFF){
		bool strip = meshh->flags == 1;
		uint32 mask;
		WindSlot *t = windTableBuild(geo, &mask);
		if(t){
			uint32 maxIdx = 0;
			Mesh *m = meshh->getMeshes();
			for(uint32 i = 0; i < meshh->numMeshes; i++, m++)
				maxIdx += strip ? (m->numIndices >= 3 ? (m->numIndices - 2)*3 : 0) : m->numIndices;
			uint16 *list = (uint16*)rwMalloc(maxIdx*2 + 16, MEMDUR_FUNCTION | ID_GEOMETRY);
			uint32 *counts = (uint32*)rwMalloc(meshh->numMeshes*4 + 4, MEMDUR_FUNCTION | ID_GEOMETRY);
			uint32 turned = 0, total = 0;
			if(list && counts){
				m = meshh->getMeshes();
				for(uint32 i = 0; i < meshh->numMeshes; i++, m++){
					counts[i] = windMesh(m, strip, t, mask, list + total, &turned);
					total += counts[i];
				}
			}
			rwFree(t);
			if(list && counts && turned){
				windFixGeos++;
				windFixTris += turned;
				header->primType = GCM_TYPE_TRIANGLES;
				header->totalNumIndex = total;
				if(!vramAlloc(&header->ib, total*2 + 16, "index buffer")){
					header->numMeshes = 0;
					rwFree(list); rwFree(counts);
					return header;
				}
				InstanceData *inst = header->inst;
				Mesh *mesh = meshh->getMeshes();
				uint32 off = 0;
				for(uint32 i = 0; i < header->numMeshes; i++, mesh++, inst++){
					uint16 *mi = list + off/2;
					findMinVertAndNumVertices(mi, counts[i], &inst->minVert, &inst->numVertices);
					inst->numIndex = counts[i];
					inst->material = mesh->material;
					inst->vertexAlpha = 0;
					inst->offset = off;
					inst->decal = isShadowTexture(mesh->material);
					off += counts[i]*2;
				}
				memcpy(header->ib.ptr, list, off);
				vramFlush(header->ib.ptr, off);
				vertexCacheTouched();
				rwFree(list); rwFree(counts);
				return header;
			}
			if(list) rwFree(list);
			if(counts) rwFree(counts);
		}
	}

	if(!vramAlloc(&header->ib, header->totalNumIndex*2 + 16, "index buffer")){
		header->numMeshes = 0;
		return header;
	}

	uint16 *indices = (uint16*)rwMalloc(header->totalNumIndex*2 + 16, MEMDUR_FUNCTION | ID_GEOMETRY);
	InstanceData *inst = header->inst;
	Mesh *mesh = meshh->getMeshes();
	uint32 offset = 0;
	for(uint32 i = 0; i < header->numMeshes; i++){
		findMinVertAndNumVertices(mesh->indices, mesh->numIndices,
		                          &inst->minVert, &inst->numVertices);
		inst->numIndex = mesh->numIndices;
		inst->material = mesh->material;
		inst->vertexAlpha = 0;
		inst->offset = offset;
		inst->decal = isShadowTexture(mesh->material);
		memcpy((uint8*)indices + offset, mesh->indices, inst->numIndex*2);
		offset += inst->numIndex*2;
		mesh++;
		inst++;
	}
	memcpy(header->ib.ptr, indices, offset);
	vramFlush(header->ib.ptr, offset);
	vertexCacheTouched();
	rwFree(indices);
	return header;
}

static void
instance(rw::ObjPipeline *rwpipe, Atomic *atomic)
{
	ObjPipeline *pipe = (ObjPipeline*)rwpipe;
	Geometry *geo = atomic->geometry;
	// don't try to (re)instance native data
	if(geo->flags & Geometry::NATIVE)
		return;

	InstanceDataHeader *header = (InstanceDataHeader*)geo->instData;
	if(geo->instData){
		// Already have instanced data, so check if we have to reinstance
		if(header->platform != PLATFORM_PS3 || header->serialNumber != geo->meshHeader->serialNum){
			// Mesh changed, so reinstance everything
			freeInstanceData(geo);
		}
	}

	// no instance or complete reinstance
	if(geo->instData == nil){
		uint64 t0 = texProfNow();
		geo->instData = instanceMesh(rwpipe, geo);
		pipe->instanceCB(geo, (InstanceDataHeader*)geo->instData, 0);
		uint64 dt = texProfNow() - t0;
		texProfile.instUs += dt;
		texProfile.instCount++;
		if(dt > texProfile.instWorstUs)
			texProfile.instWorstUs = dt;
		drawStats.instanced++;
	}else if(geo->lockedSinceInst)
		pipe->instanceCB(geo, (InstanceDataHeader*)geo->instData, 1);

	geo->lockedSinceInst = 0;
}

static void
uninstance(rw::ObjPipeline *rwpipe, Atomic *atomic)
{
	assert(0 && "can't uninstance");
}

static void renderImpl(rw::ObjPipeline *rwpipe, Atomic *atomic);

// patch 46: the CPU time of the atomic pipelines (perf.txt)
static void
render(rw::ObjPipeline *rwpipe, Atomic *atomic)
{
	uint64 t0, t1;
	__asm__ volatile("mftb %0" : "=r"(t0));
	renderImpl(rwpipe, atomic);
	__asm__ volatile("mftb %0" : "=r"(t1));
	perfCounters.renderTb += t1 - t0;
}

static void
renderImpl(rw::ObjPipeline *rwpipe, Atomic *atomic)
{
	ObjPipeline *pipe = (ObjPipeline*)rwpipe;
	Geometry *geo = atomic->geometry;
	RW_CRUMB("rw: atomic, vertices", geo ? geo->numVertices : -1);
	pipe->instance(atomic);
	InstanceDataHeader *header = (InstanceDataHeader*)geo->instData;
	if(header == nil || header->platform != PLATFORM_PS3 || header->vb.ptr == nil || header->ib.ptr == nil)
		return;
	if(pipe->renderCB)
		pipe->renderCB(atomic, header);
}

void
ObjPipeline::init(void)
{
	this->rw::ObjPipeline::init(PLATFORM_PS3);
	this->impl.instance = ps3::instance;
	this->impl.uninstance = ps3::uninstance;
	this->impl.render = ps3::render;
	this->instanceCB = nil;
	this->renderCB = nil;
}

ObjPipeline*
ObjPipeline::create(void)
{
	ObjPipeline *pipe = rwNewT(ObjPipeline, 1, MEMDUR_GLOBAL);
	pipe->init();
	return pipe;
}

static AttribDesc*
findAttrib(InstanceDataHeader *header, int index)
{
	for(int i = 0; i < header->numAttribs; i++)
		if(header->attribs[i].index == index)
			return &header->attribs[i];
	return nil;
}

// Vertex layout + fill. skin: weights and indices too.
static void
instanceCommon(Geometry *geo, InstanceDataHeader *header, bool32 reinstance, bool skin)
{
	bool isPrelit = !!(geo->flags & Geometry::PRELIT);
	bool hasNormals = !!(geo->flags & Geometry::NORMALS);
	AttribDesc *a;

	if(!reinstance){
		uint32 stride = 0;
		a = header->attribs;

		a->index = ATTR_POS; a->type = GCM_VERTEX_DATA_TYPE_F32; a->size = 3; a->offset = stride;
		stride += 12; a++;
		if(hasNormals){
			a->index = ATTR_NORMAL; a->type = GCM_VERTEX_DATA_TYPE_F32; a->size = 3; a->offset = stride;
			stride += 12; a++;
		}
		if(isPrelit){
			a->index = ATTR_COLOR; a->type = GCM_VERTEX_DATA_TYPE_U8; a->size = 4; a->offset = stride;
			stride += 4; a++;
		}
		if(geo->numTexCoordSets > 0){
			a->index = ATTR_TEX0; a->type = GCM_VERTEX_DATA_TYPE_F32; a->size = 2; a->offset = stride;
			stride += 8; a++;
		}
		if(skin){
			a->index = ATTR_WEIGHTS; a->type = GCM_VERTEX_DATA_TYPE_F32; a->size = 4; a->offset = stride;
			stride += 16; a++;
			a->index = ATTR_INDICES; a->type = GCM_VERTEX_DATA_TYPE_U8; a->size = 4; a->offset = stride;
			stride += 4; a++;
		}
		header->numAttribs = a - header->attribs;
		header->stride = stride;

		if(!vramAlloc(&header->vb, header->totalNumVertex*stride + 16, "vertex buffer"))
			return;
	}
	if(header->vb.ptr == nil)
		return;

	uint32 stride = header->stride;
	uint32 size = header->totalNumVertex*stride;
	uint8 *verts = (uint8*)rwMalloc(size + 16, MEMDUR_FUNCTION | ID_GEOMETRY);
	if(verts == nil)
		return;
	// a reinstance only writes what changed: start from zero, the rest
	// would be rewritten from the geometry anyway (the CPU can't read VRAM)
	memset(verts, 0, size);

	a = findAttrib(header, ATTR_POS);
	instV3d(VERT_FLOAT3, verts + a->offset, geo->morphTargets[0].vertices,
	        header->totalNumVertex, stride);

	if(hasNormals){
		a = findAttrib(header, ATTR_NORMAL);
		instV3d(VERT_FLOAT3, verts + a->offset, geo->morphTargets[0].normals,
		        header->totalNumVertex, stride);
	}

	if(isPrelit){
		a = findAttrib(header, ATTR_COLOR);
		if(skin)
			instColor(VERT_RGBA, verts + a->offset, geo->colors,
			          header->totalNumVertex, stride);
		else{
			InstanceData *inst = header->inst;
			for(uint32 n = 0; n < header->numMeshes; n++, inst++){
				if(inst->minVert == 0xFFFFFFFF)
					continue;
				inst->vertexAlpha = instColor(VERT_RGBA,
					verts + a->offset + stride*inst->minVert,
					geo->colors + inst->minVert,
					inst->numVertices, stride);
			}
		}
	}

	if(geo->numTexCoordSets > 0){
		a = findAttrib(header, ATTR_TEX0);
		instTexCoords(VERT_FLOAT2, verts + a->offset, geo->texCoords[0],
		              header->totalNumVertex, stride);
	}

	if(skin){
		Skin *sk = Skin::get(geo);
		a = findAttrib(header, ATTR_WEIGHTS);
		instV4d(VERT_FLOAT4, verts + a->offset, (V4d*)sk->weights,
		        header->totalNumVertex, stride);
		a = findAttrib(header, ATTR_INDICES);
		// not colors of course, but four bytes all the same
		instColor(VERT_RGBA, verts + a->offset, (RGBA*)sk->indices,
		          header->totalNumVertex, stride);
	}

	memcpy(header->vb.ptr, verts, size);
	vramFlush(header->vb.ptr, size);
	vertexCacheTouched();
	rwFree(verts);
	// the copy must be in VRAM before the RSX reads it
	__asm__ volatile("sync" ::: "memory");
}

static void
defaultInstanceCB(Geometry *geo, InstanceDataHeader *header, bool32 reinstance)
{
	instanceCommon(geo, header, reinstance, false);
}

static void
skinInstanceCB(Geometry *geo, InstanceDataHeader *header, bool32 reinstance)
{
	instanceCommon(geo, header, reinstance, true);
}

// ---- drawing --------------------------------------------------------------------------

// Binds the geometry's attributes; the ones the programs read and the
// geometry doesn't have get constant values (GL's defaults).
static uint32 boundAttribMask;

static void
bindAttribs(InstanceDataHeader *header)
{
	gcmContextData *ctx = rsxCtx;
	uint32 mask = 0;
	if(ctx == nil)
		return;
	for(int i = 0; i < header->numAttribs; i++){
		AttribDesc *a = &header->attribs[i];
		rsxBindVertexArrayAttrib(ctx, a->index, 0, header->vb.offset + a->offset,
		                         header->stride, a->size, a->type, GCM_LOCATION_RSX);
		mask |= 1 << a->index;
	}
	// disable the arrays this geometry doesn't have
	uint32 off = boundAttribMask & ~mask;
	for(int i = 0; i < NUM_ATTRS; i++)
		if(off & (1 << i))
			rsxBindVertexArrayAttrib(ctx, i, 0, 0, 0, 0, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
	boundAttribMask = mask;

	static const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	static const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	if(!(mask & (1 << ATTR_NORMAL)))
		rsxDrawVertex4f(ctx, ATTR_NORMAL, zero);
	if(!(mask & (1 << ATTR_COLOR)))
		rsxDrawVertex4f(ctx, ATTR_COLOR, black);
	if(!(mask & (1 << ATTR_TEX0)))
		rsxDrawVertex4f(ctx, ATTR_TEX0, zero);
}

// for the immediate mode: its own vertices in the ring
void
bindImmediateAttribs(uint32 offset, uint32 stride, int posSize, uint32 colorOff, uint32 texOff)
{
	gcmContextData *ctx = rsxCtx;
	uint32 mask = (1 << ATTR_POS) | (1 << ATTR_COLOR) | (1 << ATTR_TEX0);
	if(ctx == nil)
		return;
	rsxBindVertexArrayAttrib(ctx, ATTR_POS, 0, offset, stride, posSize, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
	rsxBindVertexArrayAttrib(ctx, ATTR_COLOR, 0, offset + colorOff, stride, 4, GCM_VERTEX_DATA_TYPE_U8, GCM_LOCATION_RSX);
	rsxBindVertexArrayAttrib(ctx, ATTR_TEX0, 0, offset + texOff, stride, 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
	uint32 off = boundAttribMask & ~mask;
	for(int i = 0; i < NUM_ATTRS; i++)
		if(off & (1 << i))
			rsxBindVertexArrayAttrib(ctx, i, 0, 0, 0, 0, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
	boundAttribMask = mask;
}

void
drawIndexed(uint32 prim, uint32 ibOffset, uint32 numIndices, uint32 numVertices)
{
	rsxDrawIndexArray(rsxCtx, prim, ibOffset, numIndices, GCM_INDEX_TYPE_16B, GCM_LOCATION_RSX);
	drawStats.indices += numIndices;
	drawStats.verts += numVertices;
	countDraw();
}

// the RSX starts on the frame while the CPU still builds it: commands are
// kicked every 32 draws, not only at present
void
countDraw(void)
{
	if((++drawStats.draws & 31) == 0)
		rsxFlushBuffer(rsxCtx);
}

static void
drawInst_simple(InstanceDataHeader *header, InstanceData *inst)
{
	if(inst->numIndex == 0 || !flushCache())
		return;
	drawIndexed(header->primType, header->ib.offset + inst->offset, inst->numIndex, inst->numVertices);
}

// Emulate PS2 GS alpha test FB_ONLY case: failed alpha writes to frame- but not to depth buffer
static void
drawInst_GSemu(InstanceDataHeader *header, InstanceData *inst)
{
	uint32 hasAlpha;
	int alphafunc, alpharef, gsalpharef;
	int zwrite;
	hasAlpha = getAlphaBlend();
	if(hasAlpha){
		zwrite = rw::GetRenderState(rw::ZWRITEENABLE);
		alphafunc = rw::GetRenderState(rw::ALPHATESTFUNC);
		if(zwrite){
			alpharef = rw::GetRenderState(rw::ALPHATESTREF);
			gsalpharef = rw::GetRenderState(rw::GSALPHATESTREF);

			SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
			SetRenderState(rw::ALPHATESTREF, gsalpharef);
			drawInst_simple(header, inst);
			SetRenderState(rw::ALPHATESTFUNC, rw::ALPHALESS);
			SetRenderState(rw::ZWRITEENABLE, 0);
			drawInst_simple(header, inst);
			SetRenderState(rw::ZWRITEENABLE, 1);
			SetRenderState(rw::ALPHATESTFUNC, alphafunc);
			SetRenderState(rw::ALPHATESTREF, alpharef);
		}else{
			SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
			drawInst_simple(header, inst);
			SetRenderState(rw::ALPHATESTFUNC, alphafunc);
		}
	}else
		drawInst_simple(header, inst);
}

static void
drawInstImpl(InstanceDataHeader *header, InstanceData *inst)
{
	if(rw::GetRenderState(rw::GSALPHATEST))
		drawInst_GSemu(header, inst);
	else
		drawInst_simple(header, inst);
}

// ---- patch 93: the road shadow decals, what they're drawn with and test modes ----
int32 decalMode;
bool decalCur;
Material *aimHideMaterial;
bool diffOn;
void (*diffMatHook)(Material *m, int why);

// patch 123: test mode 17, a material drawn with no texture or one not uploaded
static void
diffCheckMaterial(Material *m)
{
	if(diffMatHook == nil)
		return;
	if(m->texture == nil){
		diffMatHook(m, 1);
		return;
	}
	Raster *r = m->texture->raster;
	if(r == nil){
		diffMatHook(m, 2);
		return;
	}
	gcmTexture tex;
	bool swz;
	int32 levels = 0;
	rasterBindInfo(r, &tex, &swz, &levels);
	if(levels == 0)
		diffMatHook(m, 2);
}
bool forceNoCull;
bool decalBuilding;	// patch 96: a building is being drawn (mode 6 hides its meshes after the first)
bool decalRoad;	// patch 95: a road is being drawn: its meshes after the first with an alpha texture (dirt, shadows) are overlays
DecalStats decalStats;

static void
drawDecal(InstanceDataHeader *header, InstanceData *inst, int32 kind)
{
	DecalStats &d = decalStats;
	// patch 96: the hiding modes, to see which meshes the road shadows are
	if(decalMode == 3 && kind == 2 || decalMode == 4 && kind == 1 || decalMode == 6)
		return;
	Material *m = inst->material;
	d.meshes++;
	if(m->texture){
		d.textured++;
		Raster *r = m->texture->raster;
		if(r && GETPS3RASTEREXT(r)->hasAlpha)
			d.texAlpha++;
		// up to 4 different texture names, "a,b,c,d"
		const char *tn = m->texture->name;
		if(strstr(d.tex, tn) == nil && strlen(d.tex) + strlen(tn) + 2 < sizeof(d.tex)){
			if(d.tex[0])
				strcat(d.tex, ",");
			strcat(d.tex, tn);
		}
	}
	d.matAlpha = m->color.alpha;
	if(inst->vertexAlpha) d.vertexAlpha++;
	if(rw::GetRenderState(rw::VERTEXALPHA)) d.blendOn++;
	if(rw::GetRenderState(rw::ZTESTENABLE)) d.ztestOn++;
	if(rw::GetRenderState(rw::ZWRITEENABLE)) d.zwriteOn++;
	if(rw::GetRenderState(rw::FOGENABLE)) d.fogOn++;
	if(rw::GetRenderState(rw::GSALPHATEST)) d.gsAlphaTest++;
	if(rw::GetRenderState(rw::CULLMODE) == rw::CULLNONE) d.cullNone++;
	d.srcBlend = rw::GetRenderState(rw::SRCBLEND);
	d.dstBlend = rw::GetRenderState(rw::DESTBLEND);
	d.alphaFunc = rw::GetRenderState(rw::ALPHATESTFUNC);
	d.alphaRef = rw::GetRenderState(rw::ALPHATESTREF);
	d.gsAlphaRef = rw::GetRenderState(rw::GSALPHATESTREF);

	switch(decalMode){
	case 1:	// in front of the road under it (patch 94: stronger, ~3 cm at 30 m)
		if(rsxCtx){
			rsxSetPolygonOffset(rsxCtx, -4.0f, -512.0f);
			rsxSetPolygonOffsetFillEnable(rsxCtx, GCM_TRUE);
		}
		drawInstImpl(header, inst);
		if(rsxCtx)
			rsxSetPolygonOffsetFillEnable(rsxCtx, GCM_FALSE);
		break;
	case 2: {	// over everything
		uint32 zt = rw::GetRenderState(rw::ZTESTENABLE);
		rw::SetRenderState(rw::ZTESTENABLE, 0);
		drawInstImpl(header, inst);
		rw::SetRenderState(rw::ZTESTENABLE, zt);
		break;
	}
	case 5: {	// a solid shape where it is
		uint32 va = rw::GetRenderState(rw::VERTEXALPHA);
		uint32 af = rw::GetRenderState(rw::ALPHATESTFUNC);
		uint32 gs = rw::GetRenderState(rw::GSALPHATEST);
		rw::SetRenderState(rw::GSALPHATEST, 0);
		rw::SetRenderState(rw::VERTEXALPHA, 0);
		rw::SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
		drawInstImpl(header, inst);
		rw::SetRenderState(rw::ALPHATESTFUNC, af);
		rw::SetRenderState(rw::VERTEXALPHA, va);
		rw::SetRenderState(rw::GSALPHATEST, gs);
		break;
	}
	default:	// 0, 7 (the game hides near LODs)
		drawInstImpl(header, inst);
	}
}

static void
drawInst(InstanceDataHeader *header, InstanceData *inst)
{
	if(aimHideMaterial && inst->material == aimHideMaterial)
		return;	// patch 114: test mode 15
	if(diffOn)
		diffCheckMaterial(inst->material);	// patch 123
	if(decalCur || inst->decal){
		drawDecal(header, inst, inst->decal ? inst->decal : 1);
		return;
	}
	if(decalMode == 6 && decalBuilding && inst != header->inst){
		drawDecal(header, inst, 3);	// hidden
		return;
	}
	drawInstImpl(header, inst);
}

static int32
lightingCB(Atomic *atomic)
{
	WorldLights lightData;
	Light *directionals[8];
	Light *locals[8];
	lightData.directionals = directionals;
	lightData.numDirectionals = 8;
	lightData.locals = locals;
	lightData.numLocals = 8;

	if(atomic->geometry->flags & rw::Geometry::LIGHT){
		((World*)engine->currentWorld)->enumerateLights(atomic, &lightData);
		if((atomic->geometry->flags & rw::Geometry::NORMALS) == 0){
			// Get rid of lights that need normals when we don't have any
			lightData.numDirectionals = 0;
			lightData.numLocals = 0;
		}
		return setLights(&lightData);
	}else{
		memset(&lightData, 0, sizeof(lightData));
		return setLights(&lightData);
	}
}

static bool liftAmbient;	// patch 100: set by leedsRenderCB for a building without prelight
bool leedsEmissionFloor = true;	// patch 101 (USRDIR/noemissfloor.txt turns it off)

static void
defaultRenderCB(Atomic *atomic, InstanceDataHeader *header)
{
	Material *m;

	uint32 flags = atomic->geometry->flags;
	setWorldMatrix(atomic->getFrame()->getLTM());
	lightingCB(atomic);
	if(liftAmbient)
		liftAmbientToWorld(0.9f);	// ~ a prelit model of mid prelight x 255/128

	bindAttribs(header);
	setVertexProgram(VP_DEFAULT);
	setFragmentProgram(FP_SIMPLE);

	InstanceData *inst = header->inst;
	int32 n = header->numMeshes;
	while(n--){
		m = inst->material;
		setMaterial(flags, m->color, m->surfaceProps);
		setTexture(0, m->texture);
		rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || m->color.alpha != 0xFF);
		drawInst(header, inst);
		inst++;
	}
}

// patch 66 (leedsRenderCB): the depth bias of a Leeds model's meshes after
// its first. Patch 67: off (it broke the lighting and the lines stayed, Ren);
// USRDIR/leedsbias.txt turns it on, with two numbers ("-1 -48") for factor
// and units
bool leedsBiasOn = false;
float leedsBiasFactor = -1.0f;
float leedsBiasUnits = -48.0f;

// ---- Leeds world pipeline (res-PS3 patch 27) ------------------------------------------
// LCS's buildings and map objects, as reLCS's leedsBuilding.vert /
// leedsDefault.frag with their PS2 settings (custompipes_d3d9.cpp
// worldRenderCB): prelit colour * time cycle ambient + the ambient light *
// 0.5 as emission, clamped; textured meshes * 255/128, the PS2's vertex
// colours going to 128. Through the default pipeline these were drawn at
// half brightness and without the time of day: night roofs went black.
// Models without prelight are lit by the lights, as before.

static void
leedsRenderCB(Atomic *atomic, InstanceDataHeader *header)
{
	if(!leedsLighting || !(atomic->geometry->flags & Geometry::PRELIT)){
		liftAmbient = leedsLighting && leedsUnlitLift;
		defaultRenderCB(atomic, header);
		liftAmbient = false;
		return;
	}
	Material *m;

	setWorldMatrix(atomic->getFrame()->getLTM());
	lightingCB(atomic);	// the ambient light: the emissive part
	// patch 101: the emission has a floor of 0.2 x the time cycle ambient.
	// TIMECYC's emission at night is 34,31,40 clear and 2,3,5 in the rain:
	// with the rain the faces of low prelight (the upper floors, the road)
	// lost the ~13% lift they have clear and came out pitch black (Ren,
	// Staunton). 0.2 x ambient is about the clear night's emission; by day
	// the emission is higher and stays as it is.
	if(leedsEmissionFloor)
		liftAmbientToWorld(0.2f);
	if(leedsDebug)
		leedsDebugLights();	// patch 121

	bindAttribs(header);
	setVertexProgram(VP_LEEDS);

	InstanceData *inst = header->inst;
	int32 n = header->numMeshes;
	leedsDraws++;
	if(leedsWantSample){
		// patch 33: what one Leeds model is drawn with (setWorldAmbient asks)
		leedsWantSample = false;
		Geometry *g = atomic->geometry;
		RGBA *c = g->colors;
		rsxLog("[leeds] sample: %d vertices, prelit %s (first %d %d %d %d), %d meshes, first textured %d, material alpha %d",
		       g->numVertices, c ? "kept" : "freed", c ? c[0].red : -1, c ? c[0].green : -1, c ? c[0].blue : -1,
		       c ? c[0].alpha : -1, n, header->inst->material->texture != nil, header->inst->material->color.alpha);
	}
	bool biased = false;
	for(int32 k = 0; n--; k++){
		m = inst->material;
		// patch 66: the meshes after the first (LCS's overlays on the walls:
		// edges, dirt, signs, on the same plane as the wall under them) a
		// little in front, as the PS2 drew them after it; they fought with it
		// as the camera moved (jagged black lines on the buildings)
		if(k == 1 && leedsBiasOn && rsxCtx){
			rsxSetPolygonOffset(rsxCtx, leedsBiasFactor, leedsBiasUnits);
			rsxSetPolygonOffsetFillEnable(rsxCtx, GCM_TRUE);
			biased = true;
		}
		// only the material's alpha (distance fading) is used, the time
		// cycle ambient goes in its place; textured meshes x255/128 in
		// rw_leeds.fcg, untextured ones as they are (rw_simple.fcg)
		setLeedsMaterial(m->color.alpha, m->surfaceProps);
		setFragmentProgram(m->texture ? FP_LEEDS : FP_SIMPLE);
		setTexture(0, m->texture);
		rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || m->color.alpha != 0xFF);
		drawInst(header, inst);
		inst++;
	}
	if(biased)
		rsxSetPolygonOffsetFillEnable(rsxCtx, GCM_FALSE);
}

rw::ObjPipeline*
leedsWorldPipeline(void)
{
	static ObjPipeline *pipe;
	if(pipe == nil){
		pipe = ObjPipeline::create();
		pipe->instanceCB = defaultInstanceCB;
		pipe->renderCB = leedsRenderCB;
	}
	return pipe;
}

// ---- skin -------------------------------------------------------------------------------

static float skinRows[MAX_SKIN_BONES*12];

static void
uploadSkinMatrices(Atomic *a)
{
	int i;
	Skin *skin = Skin::get(a->geometry);
	HAnimHierarchy *hier = Skin::getHierarchy(a);
	Matrix tmp, m;
	int32 numBones = skin->numBones > MAX_SKIN_BONES ? MAX_SKIN_BONES : skin->numBones;
	if(skin->numBones > MAX_SKIN_BONES){
		static int warned;
		if(warned++ < 5)
			rsxLog("[rsx] WARNING: skin with %d bones, only %d fit", skin->numBones, MAX_SKIN_BONES);
	}

	float *row = skinRows;
	Matrix invAtmMat;
	bool local = hier && (hier->flags & HAnimHierarchy::LOCALSPACEMATRICES);
	if(hier && !local)
		Matrix::invert(&invAtmMat, a->getFrame()->getLTM());
	Matrix *invMats = (Matrix*)skin->inverseMatrices;
	for(i = 0; i < numBones; i++){
		if(hier && i < hier->numNodes){
			invMats[i].flags = 0;
			if(local)
				Matrix::mult(&m, &invMats[i], &hier->matrices[i]);
			else{
				Matrix::mult(&tmp, &hier->matrices[i], &invAtmMat);
				Matrix::mult(&m, &invMats[i], &tmp);
			}
		}else
			m.setIdentity();
		// rows of the 3x4 (the columns are right, up, at, pos)
		row[0] = m.right.x; row[1] = m.up.x; row[2] = m.at.x; row[3] = m.pos.x;
		row[4] = m.right.y; row[5] = m.up.y; row[6] = m.at.y; row[7] = m.pos.y;
		row[8] = m.right.z; row[9] = m.up.z; row[10] = m.at.z; row[11] = m.pos.z;
		row += 12;
	}
	setBones(skinRows, numBones);
}

static void
skinRenderCB(Atomic *atomic, InstanceDataHeader *header)
{
	Material *m;

	uint32 flags = atomic->geometry->flags;
	setWorldMatrix(atomic->getFrame()->getLTM());
	lightingCB(atomic);

	bindAttribs(header);
	setVertexProgram(VP_SKIN);
	setFragmentProgram(FP_SIMPLE);

	uploadSkinMatrices(atomic);

	InstanceData *inst = header->inst;
	int32 n = header->numMeshes;
	while(n--){
		m = inst->material;
		setMaterial(flags, m->color, m->surfaceProps);
		setTexture(0, m->texture);
		rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || m->color.alpha != 0xFF);
		drawInst(header, inst);
		inst++;
	}
}

static void*
skinOpen(void *o, int32, int32)
{
	ObjPipeline *pipe = ObjPipeline::create();
	pipe->instanceCB = skinInstanceCB;
	pipe->renderCB = skinRenderCB;
	pipe->pluginID = ID_SKIN;
	pipe->pluginData = 1;
	skinGlobals.pipelines[PLATFORM_PS3] = pipe;
	return o;
}

static void*
skinClose(void *o, int32, int32)
{
	if(skinGlobals.pipelines[PLATFORM_PS3]){
		((ObjPipeline*)skinGlobals.pipelines[PLATFORM_PS3])->destroy();
		skinGlobals.pipelines[PLATFORM_PS3] = nil;
	}
	return o;
}

void
initSkin(void)
{
	Driver::registerPlugin(PLATFORM_PS3, 0, ID_SKIN,
	                       skinOpen, skinClose);
}

// ---- MatFX --------------------------------------------------------------------------------

static void
matfxDefaultRender(InstanceDataHeader *header, InstanceData *inst, uint32 flags)
{
	Material *m = inst->material;
	setVertexProgram(VP_DEFAULT);
	setFragmentProgram(FP_SIMPLE);
	setMaterial(flags, m->color, m->surfaceProps);
	setTexture(0, m->texture);
	rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || m->color.alpha != 0xFF);
	drawInst(header, inst);
}

static void
matfxEnvRender(InstanceDataHeader *header, InstanceData *inst, uint32 flags, MatFX::Env *env)
{
	Material *m = inst->material;

	if(env->tex == nil || env->coefficient == 0.0f){
		matfxDefaultRender(header, inst, flags);
		return;
	}

	setVertexProgram(VP_ENV);
	setFragmentProgram(FP_ENV);
	setTexture(0, m->texture);
	setTexture(1, env->tex);
	setEnvParams(env->frame, env->coefficient, env->fbAlpha);
	setMaterial(flags, m->color, m->surfaceProps);

	rw::SetRenderState(VERTEXALPHA, 1);
	rw::SetRenderState(SRCBLEND, BLENDONE);

	drawInst(header, inst);

	rw::SetRenderState(SRCBLEND, BLENDSRCALPHA);
	setTexture(1, nil);
}

static void
matfxRenderCB(Atomic *atomic, InstanceDataHeader *header)
{
	uint32 flags = atomic->geometry->flags;
	setWorldMatrix(atomic->getFrame()->getLTM());
	lightingCB(atomic);

	bindAttribs(header);

	InstanceData *inst = header->inst;
	int32 n = header->numMeshes;
	while(n--){
		MatFX *matfx = MatFX::get(inst->material);

		if(matfx == nil)
			matfxDefaultRender(header, inst, flags);
		else switch(matfx->type){
		case MatFX::ENVMAP:
			matfxEnvRender(header, inst, flags, &matfx->fx[0].env);
			break;
		default:
			matfxDefaultRender(header, inst, flags);
			break;
		}
		inst++;
	}
}

static void*
matfxOpen(void *o, int32, int32)
{
	ObjPipeline *pipe = ObjPipeline::create();
	pipe->instanceCB = defaultInstanceCB;
	pipe->renderCB = matfxRenderCB;
	pipe->pluginID = ID_MATFX;
	pipe->pluginData = 0;
	matFXGlobals.pipelines[PLATFORM_PS3] = pipe;
	return o;
}

static void*
matfxClose(void *o, int32, int32)
{
	if(matFXGlobals.pipelines[PLATFORM_PS3]){
		((ObjPipeline*)matFXGlobals.pipelines[PLATFORM_PS3])->destroy();
		matFXGlobals.pipelines[PLATFORM_PS3] = nil;
	}
	return o;
}

void
initMatFX(void)
{
	Driver::registerPlugin(PLATFORM_PS3, 0, ID_MATFX,
	                       matfxOpen, matfxClose);
}

// ---- default pipeline ---------------------------------------------------------------------

void
initPipelines(void)
{
	ObjPipeline *pipe = ObjPipeline::create();
	pipe->instanceCB = defaultInstanceCB;
	pipe->renderCB = defaultRenderCB;
	engine->driver[PLATFORM_PS3]->defaultPipeline = pipe;
}

#endif	// RW_PS3_RSX

}
}
