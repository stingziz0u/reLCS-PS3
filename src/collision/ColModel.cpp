#include "common.h"
#include "main.h"
#include "ColModel.h"
#include "Collision.h"
#include "Game.h"
#include "MemoryHeap.h"
#include "Pools.h"

#ifdef __PS3__
#include <malloc.h>
// patch 46: the bytes of the triangle planes, for the [col] log line
uint32 PS3_colPlaneBytes;

// patch 62: with the planes, each triangle's bounding box, by axis (min x of
// every triangle, then min y... max z; 4 at a time for AltiVec, the padding
// boxes empty), after the link pointer's slot: Collision.cpp's broadphase
static size_t
PS3_PlaneBytes(int numTriangles)
{
	int n4 = (numTriangles + 3) & ~3;
	size_t planes = (sizeof(CColTrianglePlane) * (numTriangles + 1) + sizeof(void*) + 15) & ~(size_t)15;
	return planes + 6 * n4 * sizeof(float) + 16;	// patch 180: + the grid's pointer
}

const float *PS3_TriBoxes(const CColModel *m);

// patch 180: a grid over the col model in x and y, each cell the list of the
// triangles whose box touches it (in their order), for the models with many
// triangles: the broadphase looked at every triangle's box (28000 a frame
// for the 384 kept). Kept with the planes (built and freed with them).
struct PS3TriGrid {
	int32 gx, gy;
	float x0, y0, ix, iy;	// the origin, cells per unit
	uint32 *start;	// gx*gy + 1
	uint16 *idx;
	uint32 bytes;
};
uint32 PS3_colGridBytes, PS3_colGrids;

static void**
PS3_TriGridSlot(const CColModel *m)
{
	int n4 = (m->numTriangles + 3) & ~3;
	return (void**)((uint8*)PS3_TriBoxes(m) + 6 * n4 * sizeof(float));
}

static inline int32
PS3_GridCell(float v, float o, float inv, int32 g)
{
	int32 c = (int32)((v - o) * inv);
	return c < 0 ? 0 : c >= g ? g - 1 : c;
}

static PS3TriGrid*
PS3_BuildTriGrid(const float *b, int n)
{
	if(n < 96 || n > 32768)
		return nil;
	int n4 = (n + 3) & ~3;
	const float *mnx = b, *mny = b + n4, *mxx = b + 3*n4, *mxy = b + 4*n4;
	float x0 = mnx[0], y0 = mny[0], x1 = mxx[0], y1 = mxy[0];
	for(int i = 1; i < n; i++){
		x0 = Min(x0, mnx[i]); y0 = Min(y0, mny[i]);
		x1 = Max(x1, mxx[i]); y1 = Max(y1, mxy[i]);
	}
	int32 g = (int32)sqrtf(n / 6.0f);
	g = g < 2 ? 2 : g > 32 ? 32 : g;
	int32 gx = x1 - x0 > 0.5f ? g : 1;
	int32 gy = y1 - y0 > 0.5f ? g : 1;
	float ix = gx / Max(x1 - x0, 0.001f), iy = gy / Max(y1 - y0, 0.001f);
	uint32 total = 0;
	for(int i = 0; i < n; i++){
		int32 cx0 = PS3_GridCell(mnx[i], x0, ix, gx), cx1 = PS3_GridCell(mxx[i], x0, ix, gx);
		int32 cy0 = PS3_GridCell(mny[i], y0, iy, gy), cy1 = PS3_GridCell(mxy[i], y0, iy, gy);
		total += (cx1 - cx0 + 1) * (cy1 - cy0 + 1);
	}
	if(total > (uint32)n * 6)
		return nil;	// mostly big triangles: the scan of every box is as good
	uint32 cells = gx * gy;
	uint32 bytes = sizeof(PS3TriGrid) + (cells + 1) * sizeof(uint32) + total * sizeof(uint16);
	PS3TriGrid *gr = (PS3TriGrid*)malloc(bytes);
	if(gr == nil)
		return nil;
	gr->gx = gx; gr->gy = gy; gr->x0 = x0; gr->y0 = y0; gr->ix = ix; gr->iy = iy;
	gr->start = (uint32*)(gr + 1);
	gr->idx = (uint16*)(gr->start + cells + 1);
	gr->bytes = bytes;
	memset(gr->start, 0, (cells + 1) * sizeof(uint32));
	for(int i = 0; i < n; i++){
		int32 cx0 = PS3_GridCell(mnx[i], x0, ix, gx), cx1 = PS3_GridCell(mxx[i], x0, ix, gx);
		int32 cy0 = PS3_GridCell(mny[i], y0, iy, gy), cy1 = PS3_GridCell(mxy[i], y0, iy, gy);
		for(int32 cy = cy0; cy <= cy1; cy++)
			for(int32 cx = cx0; cx <= cx1; cx++)
				gr->start[cy*gx + cx + 1]++;
	}
	for(uint32 c = 0; c < cells; c++)
		gr->start[c + 1] += gr->start[c];
	uint32 *fill = (uint32*)malloc(cells * sizeof(uint32));	// (any thread: no statics)
	if(fill == nil){
		free(gr);
		return nil;
	}
	memcpy(fill, gr->start, cells * sizeof(uint32));
	for(int i = 0; i < n; i++){
		int32 cx0 = PS3_GridCell(mnx[i], x0, ix, gx), cx1 = PS3_GridCell(mxx[i], x0, ix, gx);
		int32 cy0 = PS3_GridCell(mny[i], y0, iy, gy), cy1 = PS3_GridCell(mxy[i], y0, iy, gy);
		for(int32 cy = cy0; cy <= cy1; cy++)
			for(int32 cx = cx0; cx <= cx1; cx++)
				gr->idx[fill[cy*gx + cx]++] = (uint16)i;	// in order: each cell's list ascending
	}
	free(fill);
	PS3_colGridBytes += bytes;
	PS3_colGrids++;
	return gr;
}

// the candidates for the box lo..hi (already grown), in ascending order, as
// PS3_ColBoxCandidatesBox gives them; -1: no grid (or too many cells), scan
uint32 PS3_colGridQueries, PS3_colGridTested;

int
PS3_ColGridCandidates(const CColModel *m, const float *lo, const float *hi, int *out)
{
	extern bool gPS3ColGrid;
	if(!gPS3ColGrid || m->trianglePlanes == nil)
		return -1;
	PS3TriGrid *gr = *(PS3TriGrid**)PS3_TriGridSlot(m);
	if(gr == nil)
		return -1;
	int32 cx0 = PS3_GridCell(lo[0], gr->x0, gr->ix, gr->gx), cx1 = PS3_GridCell(hi[0], gr->x0, gr->ix, gr->gx);
	int32 cy0 = PS3_GridCell(lo[1], gr->y0, gr->iy, gr->gy), cy1 = PS3_GridCell(hi[1], gr->y0, gr->iy, gr->gy);
	if((cx1 - cx0 + 1) * (cy1 - cy0 + 1) > 16)
		return -1;	// a big query: the scan
	const float *b = PS3_TriBoxes(m);
	int n4 = (m->numTriangles + 3) & ~3;
	const float *mnx = b, *mny = b + n4, *mnz = b + 2*n4, *mxx = b + 3*n4, *mxy = b + 4*n4, *mxz = b + 5*n4;
	int n = 0;
	uint32 tested = 0;
	bool one = cx0 == cx1 && cy0 == cy1;
	uint32 raw = 0;
	for(int32 cy = cy0; cy <= cy1; cy++)
		raw += gr->start[cy*gr->gx + cx1 + 1] - gr->start[cy*gr->gx + cx0];
	if(raw > 32768)
		return -1;	// the out buffer holds 32768 (repeats included)
	// no shared state (the lines also run on the audio worker): a triangle in
	// several cells is kept once per cell, then sorted and the repeats dropped
	for(int32 cy = cy0; cy <= cy1; cy++)
		for(int32 cx = cx0; cx <= cx1; cx++){
			uint32 c = cy*gr->gx + cx;
			for(uint32 k = gr->start[c]; k < gr->start[c+1]; k++){
				int i = gr->idx[k];
				tested++;
				// the same test as the scan's
				if(mxx[i] >= lo[0] && mnx[i] <= hi[0] && mxy[i] >= lo[1] && mny[i] <= hi[1] &&
				   mxz[i] >= lo[2] && mnz[i] <= hi[2])
					out[n++] = i;
			}
		}
	if(!one){
		// in ascending order, as the scan (the first of equal hits wins)
		for(int a = 1; a < n; a++){
			int v = out[a], j = a - 1;
			while(j >= 0 && out[j] > v){
				out[j+1] = out[j];
				j--;
			}
			out[j+1] = v;
		}
		int u = 0;
		for(int a = 0; a < n; a++)
			if(u == 0 || out[u-1] != out[a])
				out[u++] = out[a];
		n = u;
	}
	PS3_colGridQueries++;
	PS3_colGridTested += tested;
	return n;
}

const float*
PS3_TriBoxes(const CColModel *m)
{
	size_t planes = (sizeof(CColTrianglePlane) * (m->numTriangles + 1) + sizeof(void*) + 15) & ~(size_t)15;
	return (const float*)((const uint8*)m->trianglePlanes + planes);
}
#endif

CColModel::CColModel(void)
{
	boundingSphere.Set(0.0001f, CVector(0.0f, 0.0f, 0.0f));
	boundingBox.Set(CVector(0.0f, 0.0f, 0.0f), CVector(0.0f, 0.0f, 0.0f));
	numTriBBoxes = 0;
	triBBoxes = nil;
	numSpheres = 0;
	spheres = nil;
	numLines = 0;
	lines = nil;
	numBoxes = 0;
	boxes = nil;
	numTriangles = 0;
	vertices = nil;
	triangles = nil;
	trianglePlanes = nil;
	level = LEVEL_GENERIC;	// generic col slot
//	ownsCollisionVolumes = true;
}

CColModel::~CColModel(void)
{
	if(!gNASTY_NASTY_MEM_SHUTDOWN_HACK){
		RemoveTrianglePlanes();
		RemoveCollisionVolumes();
	}
}

//--LCS: no pool used, but maybe we better keep it?
void*
CColModel::operator new(size_t) throw()
{
	CColModel* node = CPools::GetColModelPool()->New();
	assert(node);
	return node;
}

void
CColModel::operator delete(void *p, size_t) throw()
{
	CPools::GetColModelPool()->Delete((CColModel*)p);
}

void
CColModel::RemoveCollisionVolumes(void)
{
#ifdef FIX_BUGS
	// why is this missing?
	if(ownsCollisionVolumes)
#endif
	if(!gUseChunkFiles){
		delete[] triBBoxes;
		delete[] spheres;
		delete[] lines;
		delete[] boxes;
		delete[] vertices;
		delete[] triangles;
	}
	CCollision::RemoveTrianglePlanes(this);
	numSpheres = 0;
	numTriBBoxes = 0;
	numLines = 0;
	numBoxes = 0;
	numTriangles = 0;
	spheres = nil;
#ifdef FIX_BUGS
	triBBoxes = nil;
#endif
	lines = nil;
	boxes = nil;
	vertices = nil;
	triangles = nil;
}

void
CColModel::CalculateTrianglePlanes(void)
{
	PUSH_MEMID(MEMID_COLLISION);

#ifdef __PS3__
	// patch 62: one block, 16-byte aligned: the planes, the link pointer's
	// slot, the triangles' boxes
	trianglePlanes = (CColTrianglePlane*)memalign(16, PS3_PlaneBytes(numTriangles));
	REGISTER_MEMPTR(&trianglePlanes);
	if(trianglePlanes == nil){
		POP_MEMID();
		return;
	}
	PS3_colPlaneBytes += PS3_PlaneBytes(numTriangles);
	for(int i = 0; i < numTriangles; i++)
		trianglePlanes[i].Set(vertices, triangles[i]);
	{
		int n4 = (numTriangles + 3) & ~3;
		float *b = (float*)PS3_TriBoxes(this);
		for(int i = 0; i < n4; i++){
			if(i < numTriangles){
				CVector va = vertices[triangles[i].a].Get();
				CVector vb = vertices[triangles[i].b].Get();
				CVector vc = vertices[triangles[i].c].Get();
				b[0*n4 + i] = Min(va.x, Min(vb.x, vc.x));
				b[1*n4 + i] = Min(va.y, Min(vb.y, vc.y));
				b[2*n4 + i] = Min(va.z, Min(vb.z, vc.z));
				b[3*n4 + i] = Max(va.x, Max(vb.x, vc.x));
				b[4*n4 + i] = Max(va.y, Max(vb.y, vc.y));
				b[5*n4 + i] = Max(va.z, Max(vb.z, vc.z));
			}else{
				// empty: never overlaps
				b[0*n4 + i] = b[1*n4 + i] = b[2*n4 + i] = 1.0e30f;
				b[3*n4 + i] = b[4*n4 + i] = b[5*n4 + i] = -1.0e30f;
			}
		}
		*PS3_TriGridSlot(this) = PS3_BuildTriGrid(b, numTriangles);	// patch 180
	}
#else
	// HACK: allocate space for one more element to stuff the link pointer into
	trianglePlanes = new CColTrianglePlane[numTriangles+1];
	REGISTER_MEMPTR(&trianglePlanes);
	for(int i = 0; i < numTriangles; i++)
		trianglePlanes[i].Set(vertices, triangles[i]);
#endif

	POP_MEMID();
}

void
CColModel::RemoveTrianglePlanes(void)
{
	if(trianglePlanes){
#ifdef __PS3__
		PS3_colPlaneBytes -= PS3_PlaneBytes(numTriangles);
		{
			PS3TriGrid *gr = *(PS3TriGrid**)PS3_TriGridSlot(this);	// patch 180
			if(gr){
				PS3_colGridBytes -= gr->bytes;
				PS3_colGrids--;
				free(gr);
			}
		}
		free(trianglePlanes);	// patch 62: memalign'd
#else
		delete[] trianglePlanes;
#endif
		trianglePlanes = nil;
	}
}

void
CColModel::SetLinkPtr(CLink<CColModel*> *lptr)
{
	assert(trianglePlanes);
	*(CLink<CColModel*>**)ALIGNPTR(&trianglePlanes[numTriangles]) = lptr;
}

CLink<CColModel*>*
CColModel::GetLinkPtr(void)
{
	assert(trianglePlanes);
	return *(CLink<CColModel*>**)ALIGNPTR(&trianglePlanes[numTriangles]);
}

void
CColModel::GetTrianglePoint(CVector &v, int i) const
{
	v = vertices[i].Get();
}

CColModel&
CColModel::operator=(const CColModel &other)
{
	int i;
	int numVerts;

	boundingSphere = other.boundingSphere;
	boundingBox = other.boundingBox;

	// copy tri bboxes
	if(other.numTriBBoxes){
		if(numTriBBoxes != other.numTriBBoxes){
			numTriBBoxes = other.numTriBBoxes;
			delete[] triBBoxes;
			triBBoxes = new CColTriBBox[numTriBBoxes];
		}
		for(i = 0; i < numTriBBoxes; i++)
			triBBoxes[i] = other.triBBoxes[i];
	}else{
		numTriBBoxes = 0;
		delete[] triBBoxes;
		triBBoxes = nil;
	}

	// copy spheres
	if(other.numSpheres){
		if(numSpheres != other.numSpheres){
			numSpheres = other.numSpheres;
			delete[] spheres;
			spheres = new CColSphere[numSpheres];
		}
		for(i = 0; i < numSpheres; i++)
			spheres[i] = other.spheres[i];
	}else{
		numSpheres = 0;
		delete[] spheres;
		spheres = nil;
	}

	// copy lines
	if(other.numLines){
		if(numLines != other.numLines){
			numLines = other.numLines;
			delete[] lines;
			lines = new CColLine[numLines];
		}
		for(i = 0; i < numLines; i++)
			lines[i] = other.lines[i];
	}else{
		numLines = 0;
		delete[] lines;
		lines = nil;
	}

	// copy boxes
	if(other.numBoxes){
		if(numBoxes != other.numBoxes){
			numBoxes = other.numBoxes;
			delete[] boxes;
			boxes = new CColBox[numBoxes];
		}
		for(i = 0; i < numBoxes; i++)
			boxes[i] = other.boxes[i];
	}else{
		numBoxes = 0;
		delete[] boxes;
		boxes = nil;
	}

	// copy mesh
	if(other.numTriangles){
		// copy vertices
		numVerts = -1;
		for(i = 0; i < other.numTriangles; i++){
			if(other.triangles[i].a > numVerts)
				numVerts = other.triangles[i].a;
			if(other.triangles[i].b > numVerts)
				numVerts = other.triangles[i].b;
			if(other.triangles[i].c > numVerts)
				numVerts = other.triangles[i].c;
		}
		numVerts++;
		delete[] vertices;
		if(numVerts){
			vertices = new CompressedVector[numVerts];
			for(i = 0; i < numVerts; i++)
				vertices[i] = other.vertices[i];
		}

		// copy triangles
		if(numTriangles != other.numTriangles){
			numTriangles = other.numTriangles;
			delete[] triangles;
			triangles = new CColTriangle[numTriangles];
		}
		for(i = 0; i < numTriangles; i++)
			triangles[i] = other.triangles[i];
	}else{
		numTriangles = 0;
		delete[] triangles;
		triangles = nil;
		delete[] vertices;
		vertices = nil;
	}
	return *this;
}

bool
CColModel::Write(base::cRelocatableChunkWriter &writer, bool allocSpace)
{
	int numVerts = -1;
	for(int i = 0; i < numTriangles; i++){
		if(triangles[i].a > numVerts)
			numVerts = triangles[i].a;
		if(triangles[i].b > numVerts)
			numVerts = triangles[i].b;
		if(triangles[i].c > numVerts)
			numVerts = triangles[i].c;
	}
	numVerts++;

	if(allocSpace)
		writer.AllocateRaw(this, sizeof(*this), 16, false, true);
	writer.AllocateRaw(spheres, sizeof(*spheres)*numSpheres, 16, false, true);
	writer.AddPatch(&spheres);
	writer.AllocateRaw(lines, sizeof(*lines)*numLines, 16, false, true);
	writer.AddPatch(&lines);
	writer.AllocateRaw(boxes, sizeof(*boxes)*numBoxes, 16, false, true);
	writer.AddPatch(&boxes);
	if(triBBoxes && numTriBBoxes != 0){
		writer.AllocateRaw(triBBoxes, sizeof(*triBBoxes)*numTriBBoxes, 16, false, true);
		writer.AddPatch(&triBBoxes);
	}else
		triBBoxes = nil;
	if(numTriangles != 0){
		writer.AllocateRaw(vertices, sizeof(*vertices)*numVerts, 2, false, true);
		writer.AddPatch(&vertices);
		writer.AllocateRaw(triangles, sizeof(*triangles)*numTriangles, 2, false, true);
		writer.AddPatch(&triangles);
		RemoveTrianglePlanes();
	}
	return 1;
}
