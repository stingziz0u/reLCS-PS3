#pragma once
// librw backend for the PS3 (RSX through PSL1GHT's GCM, no GL).
//
// Rasters live in main memory in a layout the RSX can read after a copy:
//   - uncompressed textures: 32 bit, bytes R,G,B,A (like gl3's RGBA8888)
//   - DXT1/3/5: the blocks exactly as they come from the PC TXDs
// Palettized and 16/24 bit images are expanded to 32 bit on the way in.

// patch 132: RenderScene / RenderEffects as a whole, timebase ticks (main.cpp)
extern "C" unsigned long long gPS3RSceneTb, gPS3REffectsTb;

namespace rw {

#ifdef RW_PS3
struct EngineOpenParams
{
	int width, height;
	const char *windowtitle;
};
#endif

namespace ps3 {

void registerPlatformPlugins(void);

extern Device renderdevice;

// pipelines (stage 2; empty in stage 1)
void initSkin(void);
void initMatFX(void);
void *destroyNativeData(void *object, int32, int32);

// Same vertex layouts as gl3, so the game's Im2D/Im3D code needs no changes.
struct Im3DVertex
{
	V3d     position;
	uint8   r, g, b, a;
	float32 u, v;

	void setX(float32 x) { this->position.x = x; }
	void setY(float32 y) { this->position.y = y; }
	void setZ(float32 z) { this->position.z = z; }
	void setColor(uint8 r, uint8 g, uint8 b, uint8 a) {
		this->r = r; this->g = g; this->b = b; this->a = a; }
	void setU(float32 u) { this->u = u; }
	void setV(float32 v) { this->v = v; }

	float getX(void) { return this->position.x; }
	float getY(void) { return this->position.y; }
	float getZ(void) { return this->position.z; }
	RGBA getColor(void) { return makeRGBA(this->r, this->g, this->b, this->a); }
	float getU(void) { return this->u; }
	float getV(void) { return this->v; }
};

struct Im2DVertex
{
	float32 x, y, z, w;
	uint8   r, g, b, a;
	float32 u, v;

	void setScreenX(float32 x) { this->x = x; }
	void setScreenY(float32 y) { this->y = y; }
	void setScreenZ(float32 z) { this->z = z; }
	void setCameraZ(float32 z) { this->w = z; }
	void setRecipCameraZ(float32 recipz) { this->w = 1.0f/recipz; }
	void setColor(uint8 r, uint8 g, uint8 b, uint8 a) {
		this->r = r; this->g = g; this->b = b; this->a = a; }
	void setU(float32 u, float recipz) { this->u = u; }
	void setV(float32 v, float recipz) { this->v = v; }

	float getScreenX(void) { return this->x; }
	float getScreenY(void) { return this->y; }
	float getScreenZ(void) { return this->z; }
	float getCameraZ(void) { return this->w; }
	float getRecipCameraZ(void) { return 1.0f/this->w; }
	RGBA getColor(void) { return makeRGBA(this->r, this->g, this->b, this->a); }
	float getU(void) { return this->u; }
	float getV(void) { return this->v; }
};

// Native raster

enum Ps3TexFormat
{
	PS3_TEX_NONE = 0,
	PS3_TEX_RGBA8,		// 32 bit, bytes R G B A
	PS3_TEX_DXT1,
	PS3_TEX_DXT3,
	PS3_TEX_DXT5
};

struct Ps3Raster
{
	// stage 1 (headless): all levels in main memory
	uint8  *texels;
	uint32  texelSize;
	uint32  levelOffset[16];	// of each level, in texels or in VRAM
	uint8   texFormat;	// Ps3TexFormat (of the data the game locks)
	uint8   bpp;		// bytes per pixel (uncompressed only)
	int8    numLevels;
	bool    isCompressed;
	bool    hasAlpha;
	bool    autogenMipmap;
	uint8   filterMode;
	uint8   addressU;
	uint8   addressV;
	uint32  serial;		// bumped when the texels change

	// stage 2 (RSX): A8R8G8B8 in VRAM, swizzled (power of two sizes, all
	// levels back to back) or linear (anything else, render targets)
	uint8  *vram;		// CPU address: write only
	uint32  vramOffset;	// RSX offset
	uint32  vramSize;
	void   *vramBlock;
	uint32  pitch;		// linear layout only
	uint16  validLevels;	// bit n: level n has been uploaded
	bool    swizzled;
	int8    lockedLevel;
	uint8  *staging;	// the locked level, in main memory
	int8    tile;		// camera / depth buffer: tile region + 1 (0 linear)
	bool    zcomp, zcull;
	uint8   vramFmt;	// PS3_VRAM_*: how the texels are kept in VRAM
	uint8   vramDXT;	// 1 / 5: an RGBA texture compressed into DXT for VRAM (it didn't fit as it was)
	bool    forcedMips;	// the MIPMAPS option added the mip levels
	int8    pendingJobs;	// levels queued for the texture thread (stage 2)
	uint8   lockSrcFmt;	// patch 64: what the locked level holds (0: RGBA8; else a PC
				// format the texture thread turns into RGBA8, readTrueColorD3D8)
	// hardware probe (USRDIR/probe.txt): a texture described as is, its
	// data written by the probe (format with the SWZ/LIN bit, location)
	uint8   probeFmt;	// 0: not a probe texture
	uint8   probeLoc;
	uint8   probeLevels;
	uint32  probeOffset, probePitch;
	// patch 154: rasterBindInfo's answer kept, with what it was worked out from
	// (it was filled again for every draw: ~2400 a frame)
	uint8   bindTex[32];	// a gcmTexture (ps3raster.cpp checks it fits)
	int32   bindLevels;
	bool    bindValid;
	uint16  bindValidLevels;
	uint8   bindFmt, bindDXT;
	bool    bindSwz;
	int8    bindNumLevels;
	uint8  *bindVram;
	uint32  bindVramOffset, bindPitch;
};

// VRAM formats of a texture (stage 2). DXT1 (most of VC's PC textures) is
// kept in 16 bits instead of A8R8G8B8: half the VRAM. Its colours are 5:6:5
// anyway; the 1-bit alpha of punch-through DXT1 fits A1R5G5B5
enum {
	PS3_VRAM_ARGB8 = 0,
	PS3_VRAM_RGB565,	// opaque DXT1 (alpha read as 1)
	PS3_VRAM_ARGB1555,	// DXT1 with transparent texels
	PS3_VRAM_DXT,		// patch 41: the DXT1/3/5 blocks as they are (power of two sizes)
};

// Video settings of the RSX renderer (stage 2)
struct VideoSettings
{
	int32 screenFit;	// % of the TV used by the picture (overscan), 50..100
	int32 bilinear;		// scaling filter to the TV
	int32 reserved;
};
void setVideoSettings(const VideoSettings *s);
void setPresentAspect(float aspect);	// 0: the picture's own shape
// RSX timers (stage 2): marks 2*i / 2*i+1 time the GPU work between them
// (timer i), read later; gpuStatsTake hands the sums over and resets them
enum { GPU_MAX_TIMERS = 30, GPU_MARK_FRAME_START = 62, GPU_MARK_FRAME_END = 63 };
void gpuMark(int32 id);
extern bool gpuTimersOn;	// default off (the timestamp reports hang the RSX)
extern bool additiveKill;	// additive draws kill the fragments that add nothing (default on)
extern bool dxt1As16;		// DXT1 textures in 16 bits in VRAM (default on; USRDIR/dxt32.txt turns it off)
extern bool dxtNative;		// DXT textures kept as DXT in VRAM, sampled by the RSX (default on; USRDIR/nodxt.txt turns it off)
extern bool vmxMips;		// mip levels made with AltiVec (default on; USRDIR/novmx.txt turns it off)
extern bool texAsync;		// textures converted on a second PPU thread (default on; USRDIR/notexthread.txt turns it off)
extern bool txdDirect;		// patch 64: true colour PC textures read straight into PS3 rasters (default on; USRDIR/notxddirect.txt turns it off)
// texels the texture thread still has to convert (the game's streaming
// stops loading more in a frame while this is high); 0 in stage 1
uint32 textureBacklog(void);
// every queued texture converted and usable
void textureJobsSync(void);
// the finished ones usable, without waiting (the others wait for a draw)
void textureJobsCollect(void);
// Graphics menu (the game sets these every frame):
extern bool forceMipmaps;	// textures loaded without mip levels get them (made on the CPU)
// What the game says about the textures it is loading now (set around a
// TXD load, back to 0 after it): 2D ones (the map's radar tiles) need no
// made mip levels, and big RGBA ones are kept as DXT (an HD map is 64
// 512x512 tiles: 64 MB in RGBA8, 8 in DXT1)
enum { PS3_LOAD_NOMIPS = 1, PS3_LOAD_DXT = 2 };
extern uint32 loadHints;
extern int32 texFilterMode;	// 3D textures: 0 the game's filter, 1 bilinear, 2 trilinear
extern int32 texAniso;		// 3D textures: anisotropic filtering, 1 (off), 2, 4, 8, 16
extern bool fxaaOn;		// FXAA on the camera's picture before it is shown
// res-PS3 patch 27: LCS's lighting and gamma
extern bool leedsBiasOn;	// patch 66: a Leeds model's meshes after the first drawn a little in front (default off; USRDIR/leedsbias.txt turns it on)
extern float leedsBiasFactor, leedsBiasUnits;	// their polygon offset (USRDIR/leedsbias.txt: "factor units")
extern bool leedsLighting;	// the Leeds world pipeline draws as LCS (false: as the default pipeline)
extern bool leedsUnlitLift;	// patch 100: buildings without prelight get the time cycle ambient (USRDIR/nounlitlift.txt turns it off)
void liftAmbientToWorld(float32 k);
extern bool windFix;	// patch 119: meshes drawn with the windings of the geometry's triangles (USRDIR/nowindfix.txt turns it off)
extern uint32 windFixGeos, windFixTris;	// geometries and triangles turned so far
extern bool leedsEmissionFloor;	// patch 101: Leeds emission at least 0.2 x the time cycle ambient (USRDIR/noemissfloor.txt turns it off)
extern uint32 leedsDraws;	// patch 33: Leeds models drawn this frame (diagnostics)
extern bool leedsWantSample;	// log the next one drawn
// res-PS3 patch 93: the road shadow decals (models with "shad" in the name;
// patch 94: and every mesh whose texture's name has "shad" in it).
// The game sets decalCur around their Render; decalMode is the test mode
// (R3 + Select). Patch 96: 0 as is, 1 depth bias, 2 no depth test,
// 3 ground overlays hidden, 4 "shad" hidden, 5 opaque, 6 every building's
// meshes after the first hidden, 7 LODs within 100 m hidden (the game). decalStats: what
// they were drawn with since the last reset.
extern int32 decalMode;
extern bool decalCur;
extern bool decalRoad;	// patch 95: the road overlays (meshes after the first, alpha texture)
extern Material *aimHideMaterial;
extern bool forceNoCull;
extern bool diffOn;	// patch 123: test mode 17, materials drawn without texture reported
extern void (*diffMatHook)(Material *m, int why);	// why 1: no texture, 2: texture not uploaded yet
extern int32 leedsDebug;	// patch 121: test mode 16, D-pad left: 0 as is, 1 the prelight ignored (flat), 2 the prelight alone
void leedsDebugLights(void);	// res-PS3 patch 117: test mode 16, D-pad up: no backface culling at all	// res-PS3 patch 114: test mode 15, the mesh under the screen centre hidden
extern bool overlayStep, lodBehind;	// patch 147: patch 146's modes 21 and 22 (patch 150: the steps off by default, the LOD push on)
void addOverlayMaterial(const char *name);	// patch 151: a texture name whose meshes go a step in front of their wall
extern bool decalStepOk;
// patch 155: the Leeds atomics of one geometry drawn together (ps3pipe.cpp)
void leedsBatchBegin(void);
void leedsBatchEnd(void);
void leedsBatchFlush(void);
extern int32 leedsBatchN;
extern bool leedsBatchEnabled;
extern uint32 leedsBatchStats[3];	// patch 149: this building may take the overlay steps (not draw last, additive, no z-write)
extern bool decalLod;	// patch 146: a LOD (big building) is being drawn (test mode 22)
extern bool decalBuilding;	// patch 96: a building is being drawn
struct DecalStats
{
	uint32 meshes, textured, texAlpha, vertexAlpha, blendOn, ztestOn, zwriteOn, fogOn, gsAlphaTest, cullNone;
	int32 srcBlend, dstBlend, alphaFunc, alphaRef, gsAlphaRef;
	int32 matAlpha;
	char tex[96];
};
extern DecalStats decalStats;
// patch 46: monotonic counters for the performance test (perf.txt):
// read twice, the difference is what happened in between
struct PerfCounters
{
	uint64 frames, cpuUs, waitUs;	// presents; CPU time and RSX wait per frame (ps3video)
	uint64 renderTb, flushTb;	// timebase ticks in the atomic pipelines (render) and in flushCache
	uint64 draws, texBinds, vpSwitches, fpSwitches, consts;
	uint64 texThreadUs;	// the texture thread's busy time (ps3raster)
};
extern PerfCounters perfCounters;
// patch 132: RenderScene's breakdown, [rscene] every 10 s after the [rsx]
// lines (with the log on: rsceneOn). Timebase ticks in each part of the atomic
// pipelines; RenderScene and RenderEffects as a whole (main.cpp) for the
// rest, the game's own work around them.
enum { RS_INSTANCE, RS_LTM, RS_WORLDMAT, RS_LIGHTS, RS_ATTRIBS, RS_PROGRAMS, RS_SKIN,
       RS_MATERIAL, RS_TEXTURE, RS_STATES, RS_DRAW, RS_MATFXMESH,
       // patch 133: inside flushCache (everywhere)
       RS_F_INVALIDATE, RS_F_STATES, RS_F_SAMPLERS, RS_F_PROGRAMS, RS_F_DERIVED, RS_F_UPLOAD,
       RS_NUM };
enum { RSP_DEFAULT, RSP_LEEDS, RSP_SKIN, RSP_MATFX, RSP_NUM };
struct RSceneStats
{
	uint64 tb[RS_NUM];
	uint64 pipeTb, flushTb, cmdTb;	// all the pipelines; inside RS_DRAW: flushCache, the draw command and its kick
	uint32 atomics[RSP_NUM], meshes[RSP_NUM];
	uint32 lights;			// lights set (directionals + locals)
	uint32 flushes, invalidates;	// patch 133
	uint32 texBinds, texDistinct, sampRegsSkipped;	// patch 151
	uint32 sampledFrames;	// patch 153: the frames timed (one in four)
};
extern uint32 rsceneFrame;	// patch 151: bumped once a frame (ps3video.cpp)
extern RSceneStats rscene;
extern bool rsceneOn;
static inline uint64 rsTb(void) { uint64 tb; __asm__ volatile("mftb %0" : "=r"(tb)); return tb; }
#define RS_T(v) uint64 v = rsceneOn ? rsTb() : 0
#define RS_ADD(k, v) do { if(rsceneOn){ uint64 rsn_ = rsTb(); rscene.tb[k] += rsn_ - (v); (v) = rsn_; } } while(0)
extern bool optLeedsNoDirLights;
// patch 50: space in this frame's vertex ring for vertices written by an SPU
// (ringAlloc: CPU address, nil when the segment is full; *offset for the
// draw) and Im2D quads drawn from there (4 Im2DVertex each, 0-1-2-3)
uint8 *ringAlloc(uint32 size, uint32 *offset);
void   im2DDrawQuadsAt(uint32 offset, int32 numQuads);	// patch 46: the Leeds program gets no directional lights (it reads none)
rw::ObjPipeline *leedsWorldPipeline(void);	// for the buildings' atomics (CSimpleModelInfo::SetAtomic)
void setWorldAmbient(float32 r, float32 g, float32 b);	// the time cycle ambient, 0..1, once a frame
void applyGamma(Raster *camRaster, float32 exponent);	// the picture to the power exponent (1/gamma)
extern bool rsxTiling;		// camera buffers tiled + depth compressed + ZCULL (default on; set before the first camera)
extern bool zcullOn;		// ZCULL in use (default on; the autotest compares)
extern int32 blendAlphaRef;	// >0: alpha test ref for alpha blended draws without Z write (smoke...); 0 the game's
void setFlipVsync(bool on);	// false: flips on hsync, no 60 fps cap (benchmarks; tears)
void setFrameRate(int32 fps);	// 30: every picture stays two vblanks (the game's own rate); 60: one
void gpuStatsTake(uint64 *ns, uint64 *frameNs, uint32 *samples);
void getVideoSettings(VideoSettings *s);

// The internal resolutions offered as video modes (the RSX scales the
// picture to the TV)
int32 getNumVideoModes(void);
void getVideoModeSize(int32 mode, int32 *width, int32 *height);

void allocateDXT(Raster *raster, int32 dxt, int32 numLevels, bool32 hasAlpha);

#ifdef RW_PS3_RSX
// logs a texture as it is in VRAM (size, format, levels, alpha/colour stats)
void debugRaster(Raster *raster, const char *tag);
// logs a few of the Im2D draws that sample this raster: vertices, render
// states, sampler, and whether its VRAM changed since this call
void debugWatchRaster(Raster *raster, const char *tag);
// draws known patterns offscreen, reads them back, logs what the RSX did
void selfTest(const char *tag, Raster *font);
// USRDIR/probe.txt: native DXT textures, textures in main memory and the
// PPU's AltiVec, tried on the hardware and logged ([probe] lines)
void hwProbe(void);
void probeDecodeDXT(int32 dxt, uint8 *dst, int32 w, int32 h, const uint8 *src);
// VRAM almost full (what is waiting to be freed counts as free):
// the game's streaming throws out unused models while this is true
bool vramTight(void);
bool vramCritical(void);	// less than ~18 MB of VRAM left
extern uint32 vramEvictions;
// d3d8.cpp: an 8/4 bit palettized PC texture straight into VRAM. false: not
// handled (the stream is untouched); true: read, *out is the raster or nil
bool readPalettedD3D8(Stream *stream, int32 width, int32 height, int32 format, int32 numLevels, Raster **out);	// models the streaming threw out for VRAM
// patch 64: the same for a true colour PC texture (8888, 888, 1555, 555): its
// levels go as they are into the PS3 raster, the texture thread turns them
// into RGBA8 (librw's way: D3D8 raster, Image, two passes per texel and two
// alpha scans on the game thread). false: not handled, the stream untouched
bool readTrueColorD3D8(Stream *stream, int32 width, int32 height, int32 format, int32 numLevels, Raster **out);
#endif

Texture *readNativeTexture(Stream *stream);
void writeNativeTexture(Texture *tex, Stream *stream);
uint32 getSizeNativeTexture(Texture *tex);

extern int32 nativeRasterOffset;
void registerNativeRaster(void);
#define GETPS3RASTEREXT(raster) PLUGINOFFSET(rw::ps3::Ps3Raster, raster, rw::ps3::nativeRasterOffset)

// Statistics for the log (stage 1 and the memory lines of the game)
struct RasterStats
{
	int32  numTextures;
	uint32 textureBytes;
	int32  numDXT;
	int32  numExpanded;	// palettized / 16 / 24 bit turned into RGBA8
	int32  noVram;		// textures left without storage: VRAM was full
	int32  packedDXT;	// RGBA textures compressed into DXT because VRAM was full
};
extern RasterStats rasterStats;

// Where the time of turning a PC texture into a PS3 one goes (microseconds,
// reset every 10 s by the [tex] log line)
enum {
	TEXPROF_CONVERT,	// librw: D3D raster -> Image (palette expanded)
	TEXPROF_FROMIMAGE,	// Image -> RGBA8 staging
	TEXPROF_SWIZZLE,	// RGBA8 -> A8R8G8B8 swizzled, in main memory
	TEXPROF_VRAM,		// copy into VRAM
	TEXPROF_MIPS,		// mip levels made on the CPU
	TEXPROF_READBACK,	// VRAM read back by the CPU (slow)
	TEXPROF_INDEXED,	// palettized D3D8 textures, fast path, all of it
	TEXPROF_DXT,		// DXT blocks decoded on the CPU
	TEXPROF_ENCODE,		// mip levels compressed to DXT (native DXT with the MIPMAPS option)
	TEXPROF_NUM
};
struct TexProfile {
	uint64 us[TEXPROF_NUM];
	uint32 textures;	// converted textures
	uint32 levels;		// levels uploaded
	uint64 texels;		// texels uploaded
	uint32 readbacks;
	uint32 indexed;		// textures that took the palettized fast path
	uint32 dxtLevels;	// levels kept as DXT (copied, no conversion)
	uint32 encLevels;	// of them, compressed here (mip levels made on the CPU)
	uint32 vmxLevels;	// mip levels made with AltiVec
	// the game's streaming: whole conversions (CStreaming::ConvertBufferToObject
	// and FinishLoadingLargeFile), what the frame actually waits for
	uint64 txdUs, modelUs, otherUs, worstUs;
	uint32 txds, models, others;
	// models: the DFF parse (librw) vs the game's setup after it, and the
	// instancing into VRAM, which happens at the first draw (in the frame)
	uint64 dffParseUs, dffSetupUs, instUs, instWorstUs;
	uint32 instCount;
	char worstName[24];	// the slowest object of the period
	// patch 59: the stages as the game thread spent them (the texture thread's
	// share left out), and the slowest TXD of the period by stage
	uint64 gameUs[TEXPROF_NUM];
	uint64 worstTxdUs, worstTxdStage[TEXPROF_NUM];
	char worstTxdName[24];
	// textures a model wants and its TXD doesn't have: looked for as image
	// files (once per name now, see imageMissKnown)
	uint32 imgLookups;
	uint64 imgLookupUs;
	// the texture thread: levels it converted, levels the game thread took
	// over while waiting at the frame's start, and how long it waited
	uint32 asyncLevels, helpedLevels;
	uint64 syncWaitUs;
	// patch 64: true colour PC textures read straight (readTrueColorD3D8),
	// the game thread's time for them (reading, alpha check, raster setup)
	uint32 direct;
	uint64 directUs;
};
// names of textures that aren't image files either (texture.cpp)
bool imageMissKnown(const char *name, const char *mask);
void imageMissRemember(const char *name, const char *mask);
// every texture a model asked for that neither its TXD nor an image file
// had (left out: drawn white), and a callback the game's check
// (ps3_autotest.cpp) sets to log each one with the model loading
extern uint32 texMissTotal;
extern void (*texMissHook)(const char *name, const char *mask);
inline void texMissed(const char *name, const char *mask) { texMissTotal++; if(texMissHook) texMissHook(name, mask); }
extern TexProfile texProfile;
void texProfAdd(int stage, uint64 us);	// patch 59: texProfile.us, and gameUs on the game thread
void texProfMarkGameThread(void);	// the calling thread is the game's (Streaming.cpp)
uint64 texProfNow(void);	// microseconds

}
}
