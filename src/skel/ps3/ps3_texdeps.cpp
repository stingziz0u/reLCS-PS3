// ps3_texdeps.cpp -- buildings whose textures live in another TXD (res-PS3 patch 83)
//
// Ren's findtex.txt (ps3/tools/findtex.py over his gta3.img): 243 models ask
// for textures their own TXD doesn't have. The cars' xv_* come from GENERIC
// (the fallback of 3.236), but buildings and LODs take theirs from other
// TXDs of the image: lod_land097gnd's concretemanky_lod is in
// islandlodsubcom/suburb2, Metalox64 of the docks' sheds in factory and a
// dozen more, dirtywhite, cover7_64, black_64, the *_nul models of "changeme"
// with everything elsewhere... On the console those meshes had no texture
// (a dummy): flat colour, the black blocks of Ren's rain videos.
//
// Here, once (and cached in USRDIR/texdeps.bin while the image doesn't
// change): the texture names of every TXD of the image, and for every
// building model the names its DFF asks for that are in neither its TXD nor
// GENERIC. Each such model gets up to PS3_TEXDEPS_MAX more TXDs (the ones that
// cover most of its missing names, the smallest first on a tie) that load
// with it and stay referenced while it is loaded, like its own TXD; the
// texture lookup then tries them after its TXD and GENERIC.

#include "common.h"
#include "ps3_platform.h"
#include "Streaming.h"
#include "ModelInfo.h"
#include "TxdStore.h"
#include "KeyGen.h"
#include "CdStream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/systime.h>

#define PS3_TEXDEPS_MAX 4
#define TEXDEPS_MAGIC 0x54584450	// "TXDP"
#define TEXDEPS_VERSION 1
#define TEXDEPS_PATH PS3_USRDIR "/texdeps.bin"

struct TexDep {
	int16 model;
	int16 n;
	int16 slot[PS3_TEXDEPS_MAX];
};

static TexDep *gDeps;	// indexed by CBaseModelInfo::m_unk[0] - 1
static int32 gNumDeps;
static int32 gCurrentModel = -1;	// the model being converted (ConvertBufferToObject)

static TexDep*
DepsOf(int32 model)
{
	if(gDeps == nil || model < 0 || model >= MODELINFOSIZE)
		return nil;
	CBaseModelInfo *mi = CModelInfo::GetModelInfo(model);
	if(mi == nil || mi->GetPS3TexDeps() == 0 || (int32)mi->GetPS3TexDeps() > gNumDeps)
		return nil;
	return &gDeps[mi->GetPS3TexDeps() - 1];
}

static TexDep*
DepsOfInfo(CBaseModelInfo *mi)
{
	if(gDeps == nil || mi == nil || mi->GetPS3TexDeps() == 0 || (int32)mi->GetPS3TexDeps() > gNumDeps)
		return nil;
	return &gDeps[mi->GetPS3TexDeps() - 1];
}

// ---- reading the image ------------------------------------------------------------

static FILE *gImg[MAX_CDIMAGES];

static FILE*
ImageFile(int32 img)
{
	if(img < 0 || img >= MAX_CDIMAGES || img == PS3_MODS_IMAGE)
		return nil;
	if(gImg[img])
		return gImg[img];
	const char *name = CdStreamGetImageName(img);
	char abs[1024];
	if(name == nil || !PS3_ResolvePath(name, abs, sizeof(abs)))
		return nil;
	gImg[img] = fopen(abs, "rb");
	return gImg[img];
}

// [off, off+len) of a stream entry's data (its image and sectors)
static bool
ReadEntry(int32 streamId, uint32 at, void *buf, uint32 len)
{
	uint32 posn, size;
	if(!CStreaming::ms_aInfoForModel[streamId].GetCdPosnAndSize(posn, size))
		return false;
	if((uint64)at + len > (uint64)size*CDSTREAM_SECTOR_SIZE)
		return false;
	FILE *f = ImageFile(posn >> 24);
	if(f == nil)
		return false;
	if(fseek(f, (long)(posn & 0xFFFFFF)*CDSTREAM_SECTOR_SIZE + at, SEEK_SET) != 0)
		return false;
	return fread(buf, 1, len, f) == len;
}

static uint32 RdU32(const uint8 *p) { return p[0] | p[1]<<8 | p[2]<<16 | (uint32)p[3]<<24; }	// RW is little-endian

static uint32
NameKey(const char *s, int32 max)
{
	char tmp[33];
	int32 i;
	for(i = 0; i < max && i < 32 && s[i]; i++)
		tmp[i] = s[i];
	tmp[i] = '\0';
	return CKeyGen::GetUppercaseKey(tmp);
}

// the texture names of a TXD: Texture Dictionary (0x16) > Struct, then
// Texture Natives (0x15) > Struct: platform, filter, name[32] (D3D8/D3D9)
static int32
TxdKeys(int32 slot, uint32 *keys, int32 maxKeys, int32 *badPlatform)
{
	int32 sid = slot + STREAM_OFFSET_TXD;
	uint32 posn, size;
	if(!CStreaming::ms_aInfoForModel[sid].GetCdPosnAndSize(posn, size) || (posn >> 24) == PS3_MODS_IMAGE)
		return 0;
	uint32 end = size*CDSTREAM_SECTOR_SIZE;
	uint8 h[12+12+8+32];
	if(!ReadEntry(sid, 0, h, 24) || RdU32(h) != 0x16 || RdU32(h+12) != 1)
		return 0;
	end = Min(end, 12 + RdU32(h+4));
	uint32 at = 24 + RdU32(h+16);	// past the dictionary's struct
	int32 n = 0;
	while(at + sizeof(h) <= end && n < maxKeys){
		if(!ReadEntry(sid, at, h, sizeof(h)))
			break;
		uint32 type = RdU32(h), len = RdU32(h+4);
		if(type == 0x15 && RdU32(h+12) == 1){
			uint32 platform = RdU32(h+24);
			if(platform == 8 || platform == 9)
				keys[n++] = NameKey((const char*)h+32, 32);
			else
				(*badPlatform)++;
		}
		at += 12 + len;
	}
	return n;
}

// the texture names a DFF asks for: a Texture chunk (6) holds a Struct (1) of
// 4 bytes and then a String (2) with the name (the same scan as findtex.py)
static char gDffNames[1024][33];
static int32
DffKeys(const uint8 *buf, uint32 len, uint32 *keys, int32 maxKeys)
{
	int32 n = 0;
	if(maxKeys > (int32)ARRAY_SIZE(gDffNames))
		maxKeys = ARRAY_SIZE(gDffNames);
	for(uint32 i = 0; i + 40 <= len && n < maxKeys; i += 4){
		if(RdU32(buf+i) != 6 || RdU32(buf+i+12) != 1 || RdU32(buf+i+16) != 4 || RdU32(buf+i+28) != 2)
			continue;
		uint32 s = RdU32(buf+i+32);
		if(s == 0 || s > 64 || i + 40 + s > len || buf[i+40] == 0)
			continue;
		uint32 k = NameKey((const char*)buf+i+40, s);
		bool dup = false;
		for(int32 j = 0; j < n; j++)
			if(keys[j] == k){ dup = true; break; }
		if(!dup){
			uint32 c = Min(s, 32u);
			memcpy(gDffNames[n], buf+i+40, c);
			gDffNames[n][c] = '\0';
			keys[n++] = k;
		}
	}
	return n;
}

// ---- the index ---------------------------------------------------------------------

struct KeySlot { uint32 key; int16 slot; };
static int
CmpKeySlot(const void *a, const void *b)
{
	uint32 x = ((const KeySlot*)a)->key, y = ((const KeySlot*)b)->key;
	return x < y ? -1 : x > y ? 1 : ((const KeySlot*)a)->slot - ((const KeySlot*)b)->slot;
}

static uint32
DirSignature(void)
{
	uint32 h = 2166136261u;
	for(int32 i = 0; i < STREAM_OFFSET_COL; i++){
		uint32 posn = CStreaming::ms_aInfoForModel[i].m_position, size = CStreaming::ms_aInfoForModel[i].m_size;
		h = (h ^ posn) * 16777619u;
		h = (h ^ size) * 16777619u;
	}
	return h;
}

static bool
LoadCache(uint32 sig)
{
	FILE *f = fopen(TEXDEPS_PATH, "rb");
	if(f == nil)
		return false;
	uint32 hdr[4];
	bool ok = fread(hdr, sizeof(hdr), 1, f) == 1 && hdr[0] == TEXDEPS_MAGIC && hdr[1] == TEXDEPS_VERSION &&
	          hdr[2] == sig && hdr[3] <= MODELINFOSIZE;
	if(ok){
		gNumDeps = hdr[3];
		gDeps = (TexDep*)malloc(sizeof(TexDep)*(gNumDeps ? gNumDeps : 1));
		ok = gDeps && fread(gDeps, sizeof(TexDep), gNumDeps, f) == (size_t)gNumDeps;
	}
	fclose(f);
	if(!ok){
		free(gDeps);
		gDeps = nil;
		gNumDeps = 0;
	}
	return ok;
}

static void
SaveCache(uint32 sig)
{
	FILE *f = fopen(TEXDEPS_PATH, "wb");
	if(f == nil)
		return;
	uint32 hdr[4] = { TEXDEPS_MAGIC, TEXDEPS_VERSION, sig, (uint32)gNumDeps };
	fwrite(hdr, sizeof(hdr), 1, f);
	fwrite(gDeps, sizeof(TexDep), gNumDeps, f);
	fclose(f);
}

static void
Build(void)
{
	uint64 t0 = sysGetSystemTime();
	// every TXD's names, as (key, slot) sorted by key
	int32 cap = 65536, num = 0, badPlatform = 0, numTxds = 0;
	KeySlot *ks = (KeySlot*)malloc(sizeof(KeySlot)*cap);
	static uint32 keys[1024];
	for(int32 slot = 0; slot < TXDSTORESIZE && ks; slot++){
		if(CTxdStore::GetSlot(slot) == nil)
			continue;
		int32 n = TxdKeys(slot, keys, ARRAY_SIZE(keys), &badPlatform);
		if(n)
			numTxds++;
		for(int32 i = 0; i < n; i++){
			if(num == cap){
				cap *= 2;
				KeySlot *k2 = (KeySlot*)realloc(ks, sizeof(KeySlot)*cap);
				if(k2 == nil)
					break;
				ks = k2;
			}
			ks[num].key = keys[i];
			ks[num].slot = slot;
			num++;
		}
	}
	if(ks == nil)
		return;
	qsort(ks, num, sizeof(KeySlot), CmpKeySlot);
	uint64 t1 = sysGetSystemTime();

	rw::TexDictionary *generic = nil;
	int32 gslot = CTxdStore::FindTxdSlot("generic");
	if(gslot >= 0 && CTxdStore::GetSlot(gslot))
		generic = CTxdStore::GetSlot(gslot)->texDict;

	gDeps = (TexDep*)malloc(sizeof(TexDep)*MODELINFOSIZE);
	gNumDeps = 0;
	uint32 bufSize = 0;
	uint8 *buf = nil;
	int32 modelsRead = 0, missingNames = 0, foundNames = 0, nowhere = 0;
	static uint32 want[512];
	for(int32 id = 0; id < MODELINFOSIZE && gDeps; id++){
		CBaseModelInfo *mi = CModelInfo::GetModelInfo(id);
		if(mi == nil || !mi->IsBuilding() || mi->GetTxdSlot() < 0)
			continue;
		uint32 posn, size;
		if(!CStreaming::ms_aInfoForModel[id].GetCdPosnAndSize(posn, size) || (posn >> 24) == PS3_MODS_IMAGE)
			continue;
		uint32 len = size*CDSTREAM_SECTOR_SIZE;
		if(len > 4*1024*1024)
			continue;
		if(len > bufSize){
			free(buf);
			bufSize = len;
			buf = (uint8*)malloc(bufSize);
			if(buf == nil){
				bufSize = 0;
				continue;
			}
		}
		if(!ReadEntry(id, 0, buf, len))
			continue;
		modelsRead++;
		int32 own = mi->GetTxdSlot();
		int32 n = DffKeys(buf, len, keys, ARRAY_SIZE(keys));
		// the names neither its TXD nor GENERIC has
		int32 nw = 0;
		for(int32 i = 0; i < n; i++){
			// first entry with this key
			int32 lo = 0, hi = num;
			while(lo < hi){ int32 m = (lo+hi)/2; if(ks[m].key < keys[i]) lo = m+1; else hi = m; }
			bool inOwn = false, anywhere = false;
			for(int32 j = lo; j < num && ks[j].key == keys[i]; j++){
				anywhere = true;
				if(ks[j].slot == own){ inOwn = true; break; }
			}
			if(inOwn)
				continue;
			if(generic && generic->find(gDffNames[i]))
				continue;
			missingNames++;
			if(!anywhere){
				nowhere++;
				continue;
			}
			if(nw < (int32)ARRAY_SIZE(want))
				want[nw++] = keys[i];
		}
		if(nw == 0)
			continue;
		// greedy: the TXD that covers most of the rest, the smallest on a tie
		TexDep d;
		d.model = id;
		d.n = 0;
		bool covered[512] = { false };
		while(d.n < PS3_TEXDEPS_MAX){
			int32 bestSlot = -1, bestCount = 0;
			uint32 bestSize = 0;
			for(int32 i = 0; i < nw; i++){
				if(covered[i])
					continue;
				int32 lo = 0, hi = num;
				while(lo < hi){ int32 m = (lo+hi)/2; if(ks[m].key < want[i]) lo = m+1; else hi = m; }
				for(int32 j = lo; j < num && ks[j].key == want[i]; j++){
					int32 s = ks[j].slot, count = 0;
					// how many of the uncovered names this TXD has
					for(int32 q = 0; q < nw; q++){
						if(covered[q])
							continue;
						int32 lo2 = 0, hi2 = num;
						while(lo2 < hi2){ int32 m = (lo2+hi2)/2; if(ks[m].key < want[q]) lo2 = m+1; else hi2 = m; }
						for(int32 r = lo2; r < num && ks[r].key == want[q]; r++)
							if(ks[r].slot == s){ count++; break; }
					}
					uint32 ps, sz = 0;
					CStreaming::ms_aInfoForModel[s + STREAM_OFFSET_TXD].GetCdPosnAndSize(ps, sz);
					if(count > bestCount || (count == bestCount && sz < bestSize)){
						bestSlot = s;
						bestCount = count;
						bestSize = sz;
					}
				}
			}
			if(bestSlot < 0)
				break;
			d.slot[d.n++] = bestSlot;
			for(int32 q = 0; q < nw; q++){
				if(covered[q])
					continue;
				int32 lo2 = 0, hi2 = num;
				while(lo2 < hi2){ int32 m = (lo2+hi2)/2; if(ks[m].key < want[q]) lo2 = m+1; else hi2 = m; }
				for(int32 r = lo2; r < num && ks[r].key == want[q]; r++)
					if(ks[r].slot == bestSlot){ covered[q] = true; foundNames++; break; }
			}
		}
		if(d.n)
			gDeps[gNumDeps++] = d;
	}
	free(buf);
	free(ks);
	for(int32 i = 0; i < MAX_CDIMAGES; i++)
		if(gImg[i]){
			fclose(gImg[i]);
			gImg[i] = nil;
		}
	uint64 t2 = sysGetSystemTime();
	PS3_Logf("[texdeps] built: %d TXDs (%d texture names, %d not D3D8/9), %d building models read; "
	         "%d names in neither their TXD nor generic: %d found in other TXDs, %d in none; %d models get more TXDs "
	         "(%.1f s for the TXDs, %.1f s for the models)",
	         numTxds, num, badPlatform, modelsRead, missingNames, foundNames, nowhere, gNumDeps,
	         (t1 - t0) / 1e6, (t2 - t1) / 1e6);
}

// ---- interface --------------------------------------------------------------------

// CStreaming::Init, after the CD directory (the IDEs are read, the TXD slots exist)
void
PS3_TexDepsInit(void)
{
	if(FILE *off = fopen(PS3_USRDIR "/notexdeps.txt", "rb")){
		fclose(off);
		PS3_Logf("[texdeps] OFF (notexdeps.txt): a building's textures only from its TXD and generic");
		return;
	}
	uint32 sig = DirSignature();
	bool cached = LoadCache(sig);
	if(!cached){
		Build();
		if(gDeps)
			SaveCache(sig);
	}
	int32 attached = 0;
	for(int32 i = 0; i < gNumDeps; i++){
		CBaseModelInfo *mi = CModelInfo::GetModelInfo(gDeps[i].model);
		if(mi == nil)
			continue;
		mi->SetPS3TexDeps(i + 1);
		attached++;
	}
	PS3_Logf("[texdeps] %d building models take textures from other TXDs (%s)", attached,
	         cached ? "from texdeps.bin" : "worked out now, saved in texdeps.bin");
	for(int32 i = 0; i < gNumDeps && i < 12; i++){
		CBaseModelInfo *mi = CModelInfo::GetModelInfo(gDeps[i].model);
		char line[256];
		int32 len = snprintf(line, sizeof(line), "[texdeps]   %s (TXD %s) also:", mi ? mi->GetModelName() : "?",
		                     mi ? CTxdStore::GetTxdName(mi->GetTxdSlot()) : "?");
		for(int32 j = 0; j < gDeps[i].n && len < (int32)sizeof(line) - 24; j++)
			len += snprintf(line + len, sizeof(line) - len, " %s", CTxdStore::GetTxdName(gDeps[i].slot[j]));
		PS3_Log(line);
	}
}

int32
PS3_TexDepsCount(int32 model, const int16 **slots)
{
	TexDep *d = DepsOf(model);
	if(d == nil)
		return 0;
	*slots = d->slot;
	return d->n;
}

// CBaseModelInfo::AddTexDictionaryRef / RemoveTexDictionaryRef
void
PS3_TexDepsAddRefs(CBaseModelInfo *mi)
{
	TexDep *d = DepsOfInfo(mi);
	if(d)
		for(int32 i = 0; i < d->n; i++)
			CTxdStore::AddRef(d->slot[i]);
}

void
PS3_TexDepsRemoveRefs(CBaseModelInfo *mi)
{
	TexDep *d = DepsOfInfo(mi);
	if(d)
		for(int32 i = 0; i < d->n; i++)
			CTxdStore::RemoveRef(d->slot[i]);
}

void
PS3_TexDepsSetCurrent(int32 model)
{
	gCurrentModel = model;
}

// the texture lookup, after the model's TXD and GENERIC
rw::Texture*
PS3_TexDepsFind(const char *name)
{
	TexDep *d = DepsOf(gCurrentModel);
	if(d == nil)
		return nil;
	for(int32 i = 0; i < d->n; i++){
		TxdDef *def = CTxdStore::GetSlot(d->slot[i]);
		if(def && def->texDict){
			rw::Texture *t = def->texDict->find(name);
			if(t)
				return t;
		}
	}
	return nil;
}

// is this TXD one a requested model needs besides its own
bool
PS3_TexDepsUses(int32 model, int32 txd)
{
	TexDep *d = DepsOf(model);
	if(d)
		for(int32 i = 0; i < d->n; i++)
			if(d->slot[i] == txd)
				return true;
	return false;
}
