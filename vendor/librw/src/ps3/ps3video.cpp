// ps3video.cpp -- RSX and video output for the RSX renderer (stage 2).
//
// From the PS3 ports proven on hardware (Quake2PS3 / Doom64-PS3 /
// TyrQuakeCell): rsxInit once, 720p if the TV takes it, three display
// buffers flipped on vsync, the picture scaled to the TV by the transfer
// unit (rsxSetTransferScaleSurface) with a screen fit area, flip sequence
// flip / flush / wait-flip.
//
// The game renders into the camera raster (any size: the internal
// resolution); videoPresent scales it into the next display buffer.
//
// Rules learned the hard way (handoff notes of those ports):
//  - rsxInit only once; on shutdown unhook the flip handler FIRST.
//  - never read RSX memory from the CPU.
//  - rsxFinish needs a new reference value every call.
//  - the 3D unit and the transfer unit run on their own: a wait-for-idle
//    between drawing an image and scaling it (and before drawing into it
//    again).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#include <unistd.h>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"
#include "rwps3.h"

#ifdef RW_PS3_RSX
#include "ps3rsx.h"

#include <sys/systime.h>
#include <sysutil/video.h>

namespace rw {
namespace ps3 {

#define FB_COUNT	3
#define RSX_CB_SIZE	(2 * 1024 * 1024)	// command buffer
#define RSX_IO_SIZE	(16 * 1024 * 1024)	// IO mapped main memory (holds the CB)

#define LABEL_FRAME	70	// GCM labels 0-63 are the system's

#define RING_SEGMENTS	3
#define RING_SEG_SIZE	(4 * 1024 * 1024)

RW_TLS gcmContextData *rsxCtx;
gcmContextData *mainRsxCtx;	// patch 168: the ring the RSX runs (rsxCtx can be a sub-buffer)	// patch 163: one per thread (render split)
DrawStats drawStats;

static void *ioBuffer;
static bool ready;

static u32 dispW = 1280, dispH = 720, dispPitch;
static u32 *fb[FB_COUNT];
static u32 fbOffset[FB_COUNT];
static int cur;
static int clearFrames;

static volatile u32 flipQueued, flipCompleted;
static volatile u32 vblankCount;
static volatile u64 lastFlipUs;		// when the last flip happened (flip handler)
static volatile u64 lastVblankUs;
static volatile u32 vblankPeriodUs = 16683;	// measured (60 Hz; 50 Hz TVs: 20000)
static int32 frameRate = 60;
static bool flipsOnVsync = true;

static volatile u32 *frameLabel;
static uint32 frame;
static void gpuCollect(uint32 doneFrame);

static VramAlloc ring;
static uint32 ringSeg, ringHead, ringPeak;
static uint32 ringWraps;	// the segment started over (ringWrite)
static bool ringPinned;	// ringAlloc gave out part of this frame's segment

static VideoSettings settings = { 90, 1, 0 };
static float presentAspect;
static float dispAspect = 16.0f/9.0f;	// 0: the picture's own (its size); else w/h on the TV

// ---- log --------------------------------------------------------------------

void
rsxLog(const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf)-1, fmt, ap);
	va_end(ap);
	strcat(buf, "\n");
	printf("%s", buf);	// goes to the log (see the game's ps3_log.cpp)
}

// ---- frames -----------------------------------------------------------------

uint32
currentFrame(void)
{
	return frame;
}

uint32
completedFrame(void)
{
	return frameLabel ? *frameLabel : 0;
}

void
waitFrame(uint32 f)
{
	int waited = 0;
	while((int32)(completedFrame() - f) < 0){
		usleep(20);
		// from reVC-PS3-Optimised (Gamer100Dev): going on reuses memory the RSX may still
		// read; keep waiting (a log line every ~5 s), give up after ~30 s
		if(++waited % 250000 == 0){
			rsxLog("[rsx] WARNING: still waiting for the frame fence (want %u, RSX at %u)", f, completedFrame());
			if(waited >= 6*250000){
				rsxLog("[rsx] WARNING: frame fence given up");
				break;
			}
		}
	}
}

void
rsxFinishAll(void)
{
	static u32 ref = 0x1000;
	// patch 168: always the ring the RSX runs, even while a sub-buffer is
	// being recorded (a VRAM wait while a building is instanced): a finish
	// written into the sub-buffer would wait for commands not yet called
	if(mainRsxCtx == nil)
		return;
	// patch 171: not while the second thread records: the RSX stops at the
	// labels of its chunks still to come, and waiting for it here could wait
	// for a thread that waits for this one (the resource lock)
	if(splitBusy){
		splitStats.finishSkipped++;
		return;
	}
	if(++ref == 0 || ref == 0xFFFFFFFF)
		ref = 0x1000;
	rsxFinish(mainRsxCtx, ref);
}

// ---- patch 168 (render split, part 3): command sub-buffers ----------------
// The buildings' draws recorded into a buffer of their own, in the IO mapped
// memory after the ring (16 MB mapped, the ring takes 2), and called from the
// ring with a CALL; the sub-buffer ends with a RETURN. Three of them, one per
// frame in flight: a sub-buffer is reused once the RSX has finished the frame
// that called it. Here the game thread records it itself (USRDIR/rsplit.txt
// turns it on): the next part has a second thread do it.
#define SUB_COUNT 3
#define SUB_SIZE (1024*1024)
#define SUB_BASE (4*1024*1024)
static gcmContextData subCtx[SUB_COUNT];
static u32 subOffset[SUB_COUNT];
static uint32 subFrame[SUB_COUNT];
static bool subUsed[SUB_COUNT];
static int32 subCur = -1;
// patch 169: the sub-buffer's cache lines zeroed in the cache (dcbz) ahead of
// the writes. With rsplit.txt every part of the drawing that writes commands
// went 4-6 times slower (the draw command and its kick 0.17 -> 0.9-1.2 ms,
// flushCache 1.4 -> 2.5-3): the sub-buffers are 3 MB of lines never in the
// cache, and a store to a line not in the cache first reads it from memory.
// The ring's lines come round often enough to stay. dcbz takes a 128-byte
// line into the cache as zeroes without reading it.
static u32 *subZeroed;
static u32 *subChunkStart;	// patch 170: where the part not yet called starts
#define SUB_ZERO_AHEAD (32*1024)
static inline void
subZeroAhead(gcmContextData *s)
{
	u8 *want = (u8*)s->current + SUB_ZERO_AHEAD;
	if(want > (u8*)s->end)
		want = (u8*)s->end;
	u8 *z = (u8*)subZeroed;
	while(z < want){
		__asm__ volatile("dcbz 0,%0" :: "r"(z) : "memory");
		z += 128;
	}
	subZeroed = (u32*)z;
}
bool splitBuildings;
SplitStats splitStats;

// ---- patch 171 (render split, part 4): the second thread -------------------
}
}
#include <lv2/mutex.h>
#include <ppu-asm.h>
namespace rw {
namespace ps3 {
bool splitThread;
bool splitDumpNext = true;	// patch 174: the first threaded frame dumped
volatile bool splitBusy;
void (*splitJoinHook)(void);	// patch 172: Renderer.cpp's PS3_SplitJoin
RW_TLS bool onSplitWorker;
static sys_lwmutex_t resMutex;
static bool resMutexMade;

void
resLock(void)
{
	if(resMutexMade)
		sysLwMutexLock(&resMutex, 0);
}

void
resUnlock(void)
{
	if(resMutexMade)
		sysLwMutexUnlock(&resMutex);
}

// The ring gets, where the buildings go, SPLIT_CHUNKS slots: "wait for label
// n to be this frame's number, then CALL chunk n". The second thread records
// the buildings into the chunks (48 KB each, in the frame's sub-buffer),
// ends each with a RETURN and writes its label; the chunks it doesn't need
// it ends empty. The RSX draws them as they come; the game thread goes on.
#define SPLIT_CHUNKS 20
#define SPLIT_CHUNK_SIZE (48*1024)	// 20 x 48 KB: 960 KB of the 1 MB
#define SPLIT_LABEL0 80
static gcmContextData workerCtx;
static gcmContextData *workerPrevCtx;	// patch 174: the thread's buffer before (the game thread's own in the self test)
static uint32 chunkBytes[SPLIT_CHUNKS];	// patch 174: for the first frame's dump
static u32 *slotsBegin, *slotsEnd;
static int32 workerSub = -1, workerChunk;
static uint32 workerSeq;
static volatile u32 *splitLabel[SPLIT_CHUNKS];

static bool
splitMakeLock(void)
{
	if(resMutexMade)
		return true;
	sys_lwmutex_attr_t la;
	memset(&la, 0, sizeof(la));
	la.attr_protocol = SYS_LWMUTEX_PROTOCOL_PRIO;
	la.attr_recursive = SYS_LWMUTEX_ATTR_RECURSIVE;
	strncpy(la.name, "rwres", sizeof(la.name)-1);
	if(sysLwMutexCreate(&resMutex, &la) != 0)
		return false;
	for(int32 i = 0; i < SPLIT_CHUNKS; i++){
		splitLabel[i] = (volatile u32*)gcmGetLabelAddress(SPLIT_LABEL0 + i);
		*splitLabel[i] = 0;
	}
	resMutexMade = true;
	return true;
}

// game thread: the slots into the ring. The sub-buffer to use (-1: none,
// draw on this thread) and the value the labels must get
int32
splitEmitSlots(uint32 *seq)
{
	if(ioBuffer == nil || mainRsxCtx == nil || rsxCtx != mainRsxCtx || subCur >= 0 || !splitMakeLock())
		return -1;
	int32 k = frame % SUB_COUNT;
	if(subUsed[k] && subFrame[k] == frame){	// patch 172: once a frame (see subBufferBegin)
		splitStats.sameFrame++;
		return -1;
	}
	if(subUsed[k] && (int32)(completedFrame() - subFrame[k]) < 0){
		uint64 t0 = sysGetSystemTime();
		waitFrame(subFrame[k]);
		splitStats.waitUs += (uint32)(sysGetSystemTime() - t0);
	}
	u32 *mem = (u32*)((uint8*)ioBuffer + SUB_BASE + k*SUB_SIZE);
	if(subOffset[k] == 0 && rsxAddressToOffset(mem, &subOffset[k]) != 0){
		subOffset[k] = 0;
		return -1;
	}
	*seq = frame + 1;	// never 0: the labels start at 0
	rsxFlushBuffer(mainRsxCtx);	// what came before: the RSX on it now
	slotsBegin = mainRsxCtx->current;
	for(int32 i = 0; i < SPLIT_CHUNKS; i++){
		rsxSetWaitLabel(mainRsxCtx, SPLIT_LABEL0 + i, *seq);
		rsxSetCallCommand(mainRsxCtx, subOffset[k] + i*SPLIT_CHUNK_SIZE);
	}
	slotsEnd = mainRsxCtx->current;
	memset(chunkBytes, 0, sizeof(chunkBytes));
	subUsed[k] = true;
	subFrame[k] = frame;
	splitStats.calls++;
	return k;
}

static s32 workerChunkFull(gcmContextData *ctx, u32 count);

static void
workerOpenChunk(void)
{
	u32 *mem = (u32*)((uint8*)ioBuffer + SUB_BASE + workerSub*SUB_SIZE + workerChunk*SPLIT_CHUNK_SIZE);
	workerCtx.begin = mem;
	workerCtx.current = mem;
	workerCtx.end = mem + SPLIT_CHUNK_SIZE/4 - 16;
	// patch 173: a chunk filled in the middle of a building goes on in the
	// next one. Patch 171 gave it the ring's callback ("never reached"): a
	// big LOD (IslandLODInd and its like) went past the 24 KB kept free, the
	// ring's callback put a JUMP to the ring's start in the chunk and turned
	// it into the ring: the RSX ran off into memory with nothing in it (GET
	// at 0x01000000, the end of the IO memory) and the frame never came
	workerCtx.callback = (gcmContextCallback)__get_opd32(workerChunkFull);
	subZeroed = mem;
	subZeroAhead(&workerCtx);
}

static void
workerCloseChunk(void)
{
	rsxSetReturnCommand(&workerCtx);
	chunkBytes[workerChunk] = (uint32)(workerCtx.current - workerCtx.begin) * 4;
	splitStats.bytes += (uint32)(workerCtx.current - workerCtx.begin) * 4;
	__asm__ volatile("sync" ::: "memory");	// the commands in memory before the label lets the RSX in
	*splitLabel[workerChunk] = workerSeq;
	splitStats.chunks++;
}

// the chunk's callback (the command macros call it when the chunk is full):
// the chunk ended (RETURN written by hand: the end keeps 16 words for it),
// its label written, the next one opened. -1 (the commands left out) when
// there is no next one
static s32
workerChunkFull(gcmContextData *ctx, u32 count)
{
	(void)ctx;
	if(workerChunk + 1 >= SPLIT_CHUNKS){
		splitStats.overflow++;
		return -1;
	}
	if(splitStats.midChunk++ < 4)
		rsxLog("[split] a chunk filled in the middle of a building (%u words asked): on in the next one", count);
	*workerCtx.current++ = 0x00020000;	// RETURN
	chunkBytes[workerChunk] = (uint32)(workerCtx.current - workerCtx.begin) * 4;
	splitStats.bytes += (uint32)(workerCtx.current - workerCtx.begin) * 4;
	__asm__ volatile("sync" ::: "memory");
	*splitLabel[workerChunk] = workerSeq;
	splitStats.chunks++;
	workerChunk++;
	workerOpenChunk();
	return 0;
}

// second thread: start recording into sub-buffer k
void
splitWorkerBegin(int32 k, uint32 seq)
{
	onSplitWorker = true;
	workerSub = k;
	workerSeq = seq;
	workerChunk = 0;
	workerOpenChunk();
	workerPrevCtx = rsxCtx;
	rsxCtx = &workerCtx;
}

// patch 174: the first threaded frame's slots in the ring and its chunks as
// written (the freeze: the RSX's GET at 0x01000000 with every label written)
void
splitDumpFrame(void)
{
	char buf[700];
	int len = 0;
	for(u32 *p = slotsBegin; p && p < slotsEnd && p < slotsBegin + 20 && len < 600; p++)
		len += snprintf(buf + len, sizeof(buf) - len, " %08x", *p);
	u32 so = 0;
	if(slotsBegin)
		rsxAddressToOffset(slotsBegin, &so);
	rsxLog("[split] the slots at 0x%08x, %u words; the first 20:%s", so, (uint32)(slotsEnd - slotsBegin), buf);
	len = 0;
	for(int32 i = 0; i < SPLIT_CHUNKS && len < 600; i++){
		u32 *mem = (u32*)((uint8*)ioBuffer + SUB_BASE + workerSub*SUB_SIZE + i*SPLIT_CHUNK_SIZE);
		uint32 n = chunkBytes[i] / 4;
		len += snprintf(buf + len, sizeof(buf) - len, " %d:%uB first %08x last %08x |", i, chunkBytes[i], mem[0],
		                n ? mem[n-1] : mem[0]);
	}
	rsxLog("[split] the chunks:%s", buf);
}

// the room left in the chunk; when it's low the next chunk (false: none left)
bool
splitWorkerRoom(void)
{
	subZeroAhead(&workerCtx);
	if((uint32)(workerCtx.end - workerCtx.current) * 4 >= 24*1024)	// a building takes up to ~15 KB
		return true;
	if(workerChunk + 1 >= SPLIT_CHUNKS)
		return false;
	workerCloseChunk();
	workerChunk++;
	workerOpenChunk();
	return true;
}

// patch 172: for the [hang] dump: where the second thread and the RSX are
void
splitHangReport(void)
{
	gcmControlRegister *cr = gcmGetControlRegister();
	u32 mainOff = 0;
	if(mainRsxCtx)
		rsxAddressToOffset(mainRsxCtx->current, &mainOff);
	rsxLog("[hang] split: busy %d, the second thread on sub-buffer %d chunk %d (label value %u), frame %u done %u; "
	       "RSX get 0x%08x put 0x%08x, the ring's current 0x%08x, sub-buffers at 0x%08x 0x%08x 0x%08x",
	       (int)splitBusy, (int)workerSub, (int)workerChunk, workerSeq, frame, completedFrame(),
	       cr ? cr->get : 0, cr ? cr->put : 0, mainOff, subOffset[0], subOffset[1], subOffset[2]);
	if(resMutexMade){
		char buf[300];
		int len = 0;
		for(int32 i = 0; i < SPLIT_CHUNKS && len < 280; i++)
			len += snprintf(buf + len, sizeof(buf) - len, " %u", *splitLabel[i]);
		rsxLog("[hang] split: chunk labels:%s", buf);
	}
	// patch 175: the commands where the RSX stopped (GET in the IO memory:
	// the ring or a sub-buffer), 32 words before it and 8 after
	if(cr && ioBuffer && cr->get < RSX_IO_SIZE){
		u32 g = cr->get & ~3u;
		u32 from = g >= 128 ? g - 128 : 0;
		for(int part = 0; part < 2; part++){
			char buf[400];
			int len = 0;
			for(u32 o = from + part*80; o < from + part*80 + 80 && o + 4 <= RSX_IO_SIZE; o += 4)
				len += snprintf(buf + len, sizeof(buf) - len, "%s%08x", o == g ? " >" : " ", *(u32*)((u8*)ioBuffer + o));
			rsxLog("[hang] split: the RSX at 0x%08x; from 0x%08x:%s", g, from + part*80, buf);
		}
	}
}

// the current chunk ended and every chunk not used ended empty
void
splitWorkerEnd(void)
{
	workerCloseChunk();
	for(int32 i = workerChunk + 1; i < SPLIT_CHUNKS; i++){
		u32 *mem = (u32*)((uint8*)ioBuffer + SUB_BASE + workerSub*SUB_SIZE + i*SPLIT_CHUNK_SIZE);
		gcmContextData c;
		c.begin = c.current = mem;
		c.end = mem + 16;
		c.callback = mainRsxCtx->callback;
		rsxSetReturnCommand(&c);
	}
	__asm__ volatile("sync" ::: "memory");
	for(int32 i = workerChunk + 1; i < SPLIT_CHUNKS; i++)
		*splitLabel[i] = workerSeq;
	uint32 used = (uint32)(workerChunk + 1);
	if(used > splitStats.peak)
		splitStats.peak = used;
	rsxCtx = workerPrevCtx;	// patch 174: nil on the second thread, the ring in the self test
	if(splitDumpNext){
		splitDumpNext = false;
		splitDumpFrame();
	}
	workerSub = -1;
	onSplitWorker = false;
}

bool
subBufferBegin(void)
{
	if(subCur >= 0 || ioBuffer == nil || mainRsxCtx == nil || rsxCtx != mainRsxCtx)
		return false;
	int32 k = frame % SUB_COUNT;
	// patch 172: once a frame. A second time in the same frame (the loading
	// screen draws the scene twice before it presents) waited for this very
	// frame to be done, which it can't be before it is presented: the freeze
	// at "Loading game" with rsplitthread.txt. The ring then.
	if(subUsed[k] && subFrame[k] == frame){
		splitStats.sameFrame++;
		return false;
	}
	if(subUsed[k] && (int32)(completedFrame() - subFrame[k]) < 0){
		uint64 t0 = sysGetSystemTime();
		waitFrame(subFrame[k]);
		splitStats.waitUs += (uint32)(sysGetSystemTime() - t0);
	}
	u32 *mem = (u32*)((uint8*)ioBuffer + SUB_BASE + k*SUB_SIZE);
	if(subOffset[k] == 0 && rsxAddressToOffset(mem, &subOffset[k]) != 0){
		subOffset[k] = 0;
		return false;
	}
	subCtx[k].begin = mem;
	subCtx[k].current = mem;
	subCtx[k].end = mem + SUB_SIZE/4 - 16;
	subCtx[k].callback = mainRsxCtx->callback;	// never reached: subBufferRoom() stops the recording first
	rsxFlushBuffer(mainRsxCtx);	// patch 170: the RSX on what came before while the buildings are recorded
	subCur = k;
	subChunkStart = mem;
	subZeroed = mem;	// 1 MB aligned: a whole number of lines
	subZeroAhead(&subCtx[k]);
	rsxCtx = &subCtx[k];
	return true;
}

uint32
subBufferRoom(void)
{
	if(subCur < 0)
		return ~0u;
	subZeroAhead(&subCtx[subCur]);	// patch 169: called before each building
	return (uint32)(subCtx[subCur].end - subCtx[subCur].current) * 4;
}

// patch 170: what was recorded since the last call, called from the ring
// now and the ring kicked, so the RSX draws the buildings as they come, as
// it does with the ring's 32-draw kicks. Patch 168/169 called the whole
// sub-buffer once at the end: the RSX sat idle while the buildings were
// recorded and was 2+ ms behind after, and what waits for it (VRAM blocks
// of the models instanced as they fade in) waited that much ("fading in"
// 0.4-0.8 ms -> 2.5-3.4 with rsplit.txt).
static void
subCallChunk(gcmContextData *s)
{
	if(s->current == subChunkStart)
		return;
	rsxSetReturnCommand(s);
	__asm__ volatile("sync" ::: "memory");	// the commands in memory before the RSX can reach the CALL
	rsxSetCallCommand(mainRsxCtx, subOffset[subCur] + (uint32)((u8*)subChunkStart - (u8*)s->begin));
	rsxFlushBuffer(mainRsxCtx);
	subChunkStart = s->current;
	splitStats.chunks++;
}

void
subBufferChunk(void)
{
	if(subCur >= 0)
		subCallChunk(&subCtx[subCur]);
}

void
subBufferEnd(void)
{
	if(subCur < 0)
		return;
	gcmContextData *s = &subCtx[subCur];
	subCallChunk(s);
	uint32 bytes = (uint32)(s->current - s->begin) * 4;
	splitStats.bytes += bytes;
	if(bytes > splitStats.peak)
		splitStats.peak = bytes;
	splitStats.calls++;
	rsxCtx = mainRsxCtx;
	subUsed[subCur] = true;
	subFrame[subCur] = frame;
	subCur = -1;
}

static void
flipHandler(const u32 head)
{
	(void)head;
	lastFlipUs = sysGetSystemTime();
	flipCompleted++;
}

static void
vblankHandler(const u32 head)
{
	(void)head;
	u64 now = sysGetSystemTime();
	if(lastVblankUs){
		u64 dt = now - lastVblankUs;
		if(dt > 12000 && dt < 25000)	// a sane period: smoothed
			vblankPeriodUs = (vblankPeriodUs*7 + (u32)dt) / 8;
	}
	lastVblankUs = now;
	vblankCount++;
}

// 30 fps: each picture stays two vblanks. The frame's drawing is sent to
// the RSX first (it draws while the CPU waits), then the flip is queued
// right after the vblank before its target, so it happens on the target:
// two vblanks after the last one (or the next vblank, if the frame is late).
// Patch 30 waited from the flip handler's time with the frame still in the
// command buffer: the RSX only started drawing after the wait and missed
// the vblank, 15 fps.
static u32 flipTargetVblank;

static void
paceFlip(void)
{
	if(frameRate >= 60 || !flipsOnVsync)
		return;
	rsxFlushBuffer(rsxCtx);
	u32 now = vblankCount;
	u32 target = flipTargetVblank + 2;
	if((int32)(target - now) < 1)
		target = now + 1;	// late (or the first frame): the next vblank
	// at most ~50 ms (no vblank handler: no pacing rather than a hang)
	for(int waited = 0; (int32)(vblankCount - (target - 1)) < 0 && waited < 250; waited++)
		usleep(200);
	flipTargetVblank = target;
}

void
setFrameRate(int32 fps)
{
	fps = fps <= 30 ? 30 : 60;
	if(fps != frameRate)
		rsxLog("[rsx] frame rate %d", fps);
	frameRate = fps;
}

// Blocks while both display buffers that aren't being built are in flight.
static void
waitFlips(void)
{
	int waited = 0;
	while((int)(flipQueued - flipCompleted) > FB_COUNT - 2){
		usleep(100);
		// from reVC-PS3-Optimised (Gamer100Dev): as the frame fence, a display buffer
		// still on the screen isn't drawn over; a log line every ~2 s, ~30 s at most
		if(++waited % 20000 == 0){
			rsxLog("[rsx] WARNING: still waiting for a flip (%u queued, %u done)", flipQueued, flipCompleted);
			if(waited >= 15*20000){
				rsxLog("[rsx] WARNING: flip fence given up, force-sync");
				flipCompleted = flipQueued;
				break;
			}
		}
	}
}

// ---- settings ---------------------------------------------------------------

// The game's aspect ratio option: the picture was drawn for this shape, so
// it is shown with this shape (black bars) instead of filling a 16:9 TV
void
setPresentAspect(float aspect)
{
	if(aspect < 0.5f || aspect > 4.0f)
		aspect = 0.0f;
	if(aspect != presentAspect)
		clearFrames = FB_COUNT;
	presentAspect = aspect;
}

// Benchmarks: flips on hsync, so the frame rate is what the CPU and the RSX
// give (above 60, with tearing) instead of the vsync's cap
void
setFlipVsync(bool on)
{
	static int have = 1;
	if(!ready || have == (on ? 1 : 0))
		return;
	have = on ? 1 : 0;
	flipsOnVsync = on;
	// no flip queued while the mode changes
	rsxFinishAll();
	for(int waited = 0; flipQueued != flipCompleted && waited < 1000; waited++)
		usleep(100);
	gcmSetFlipMode(on ? GCM_FLIP_VSYNC : GCM_FLIP_HSYNC);
	rsxLog("[rsx] flips on %s", on ? "vsync" : "hsync (no cap)");
}

void
setVideoSettings(const VideoSettings *s)
{
	VideoSettings n = *s;
	if(n.screenFit < 50) n.screenFit = 50;
	if(n.screenFit > 100) n.screenFit = 100;
	if(n.screenFit != settings.screenFit)
		clearFrames = FB_COUNT;
	settings = n;
}

void
getVideoSettings(VideoSettings *s)
{
	*s = settings;
}

int
videoWidth(void)
{
	return dispW;
}

int
videoHeight(void)
{
	return dispH;
}

// ---- init -------------------------------------------------------------------

bool
videoInit(void)
{
	s32 ret;
	s32 vidRes = VIDEO_RESOLUTION_720;
	u8 vidAspect = VIDEO_ASPECT_16_9;
	videoResolution res;
	videoConfiguration vconfig;
	videoState state;
	int i, waited;

	if(ready)
		return true;

	rsxLog("[rsx] init");
	ioBuffer = memalign(1024*1024, RSX_IO_SIZE);
	if(ioBuffer == nil){
		rsxLog("[rsx] FATAL: memalign failed for the %d MB IO buffer", RSX_IO_SIZE>>20);
		return false;
	}
	ret = rsxInit(&rsxCtx, RSX_CB_SIZE, RSX_IO_SIZE, ioBuffer);
	if(ret != 0 || rsxCtx == nil){
		rsxLog("[rsx] FATAL: rsxInit failed (%d)", (int)ret);
		return false;
	}
	mainRsxCtx = rsxCtx;	// patch 168

	// 720p if the TV takes it, otherwise whatever it is set to
	if(!videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_16_9, 0)){
		videoGetState(0, 0, &state);
		vidRes = state.displayMode.resolution;
		vidAspect = state.displayMode.aspect;
		rsxLog("[rsx] 720p not available, using the TV's mode (%d)", (int)vidRes);
	}
	if(videoGetResolution(vidRes, &res) != 0){
		rsxLog("[rsx] FATAL: videoGetResolution failed");
		return false;
	}
	dispW = res.width;
	dispH = res.height;
	dispPitch = dispW*4;
	// shape of the TV picture (720x480 can be either)
	dispAspect = vidAspect == VIDEO_ASPECT_4_3 ? 4.0f/3.0f :
	             vidAspect == VIDEO_ASPECT_16_9 ? 16.0f/9.0f : (float)dispW / (float)dispH;

	memset(&vconfig, 0, sizeof(vconfig));
	vconfig.resolution = vidRes;
	vconfig.format = VIDEO_BUFFER_FORMAT_XRGB;
	vconfig.pitch = dispPitch;
	vconfig.aspect = vidAspect;
	if(videoConfigure(0, &vconfig, NULL, 0) != 0){
		rsxLog("[rsx] FATAL: videoConfigure failed");
		return false;
	}
	waited = 0;
	do{
		usleep(10000);
		if(videoGetState(0, 0, &state) != 0)
			break;
	}while(state.state == 3 && ++waited < 300);

	gcmSetFlipMode(GCM_FLIP_VSYNC);

	for(i = 0; i < FB_COUNT; i++){
		fb[i] = (u32*)rsxMemalign(64, dispPitch*dispH);
		if(fb[i] == nil){
			rsxLog("[rsx] FATAL: rsxMemalign failed for display buffer %d", i);
			return false;
		}
		memset(fb[i], 0, dispPitch*dispH);
		vramFlush(fb[i], dispPitch*dispH);
		rsxAddressToOffset(fb[i], &fbOffset[i]);
		gcmSetDisplayBuffer(i, fbOffset[i], dispPitch, dispW, dispH);
	}
	cur = 0;
	flipQueued = flipCompleted = 0;
	clearFrames = FB_COUNT;
	gcmSetFlipHandler(flipHandler);
	gcmSetVBlankHandler(vblankHandler);

	frameLabel = (volatile u32*)gcmGetLabelAddress(LABEL_FRAME);
	*frameLabel = 0;
	frame = 1;

	if(!vramInit())
		return false;
	if(!vramAlloc(&ring, RING_SEGMENTS*RING_SEG_SIZE, "vertex ring")){
		rsxLog("[rsx] FATAL: no VRAM for the vertex ring");
		return false;
	}
	ringSeg = frame % RING_SEGMENTS;
	ringHead = 0;

	ready = true;
	rsxLog("[rsx] ready: TV %ux%u, %d display buffers, ring %d x %d MB",
	       dispW, dispH, FB_COUNT, RING_SEGMENTS, RING_SEG_SIZE>>20);
	return true;
}

void
videoShutdown(void)
{
	int i;
	if(!ready)
		return;
	rsxLog("[rsx] shutdown");
	// ORDER MATTERS: unhook the handlers first
	gcmSetFlipHandler(NULL);
	gcmSetVBlankHandler(NULL);
	rsxFinishAll();
	vramShutdown();
	for(i = 0; i < FB_COUNT; i++)
		if(fb[i]){
			rsxFree(fb[i]);
			fb[i] = nil;
		}
	ready = false;
}

// ---- ring -------------------------------------------------------------------

uint32
ringWrite(const void *data, uint32 size)
{
	if(!ready || ring.ptr == nil)
		return 0xFFFFFFFF;
	if(ringHead + size > RING_SEG_SIZE){
		// from reVC-PS3-Optimised (Gamer100Dev): a full segment is started over once the
		// RSX has drawn what's in it, instead of dropping the draws. Not when an
		// SPU fills part of it this frame (ringAlloc): that draw comes later
		if(!ringPinned && size <= RING_SEG_SIZE){
			if(ringWraps < 8)
				rsxLog("[rsx] vertex ring segment full (%u + %u bytes): started over after the RSX finished", ringHead, size);
			ringWraps++;
			rsxFinishAll();
			ringHead = 0;
		}else{
			static int warned;
			if(!warned){
				rsxLog("[rsx] WARNING: vertex ring segment full (%u + %u bytes), draws dropped",
				       ringHead, size);
				warned = 1;
			}
			drawStats.dropped++;
			return 0xFFFFFFFF;
		}
	}
	uint32 at = ringSeg*RING_SEG_SIZE + ringHead;
	memcpy(ring.ptr + at, data, size);
	vramFlush(ring.ptr + at, size);
	vertexCacheTouched();
	// own cache lines (128 bytes): a draw's fetch never holds the start of
	// the next one's data from before it was written
	ringHead = (ringHead + size + 127) & ~127;
	drawStats.ringBytes += size;
	return ring.offset + at;
}

// patch 50: room for vertices an SPU writes (by DMA, 16-byte aligned); the
// draw (im2DDrawQuadsAt) invalidates the vertex cache
uint8*
ringAlloc(uint32 size, uint32 *offset)
{
	if(!ready || ring.ptr == nil || size == 0)
		return nil;
	if(ringHead + size > RING_SEG_SIZE){
		drawStats.dropped++;
		return nil;
	}
	ringPinned = true;
	uint32 at = ringSeg*RING_SEG_SIZE + ringHead;
	ringHead = (ringHead + size + 127) & ~127;
	drawStats.ringBytes += size;
	*offset = ring.offset + at;
	return ring.ptr + at;
}

// ---- frames -----------------------------------------------------------------

void
videoBeginFrame(void)
{
	// the ring segment of this frame was last used RING_SEGMENTS frames ago
	if(frame > RING_SEGMENTS){
		waitFrame(frame - RING_SEGMENTS);
		gpuCollect(frame - RING_SEGMENTS);	// done: its timestamps are there
	}
	ringSeg = frame % RING_SEGMENTS;
	ringPinned = false;
	if(ringHead > ringPeak)
		ringPeak = ringHead;
	ringHead = 0;
	vramProcessFrees();
	gpuMark(GPU_MARK_FRAME_START);
}

// ---- GPU timers ---------------------------------------------------------------
// RSX timestamps (report slots) around main.cpp's frame phases: how long the
// RSX itself spends on the scene, the effects, the 2D... (the CPU timers only
// say how long the PPU took to send it). Read RING_SEGMENTS frames later,
// when the frame is surely done.

#define TS_BASE		1024	// report slots 1024..1279 (nothing else uses them)
#define TS_PER_FRAME	64
#define TS_FRAMES	4
static uint64 tsMask[TS_FRAMES];
bool gpuTimersOn;	// off unless the autotest turns them on (see below)
static uint64 gpuNs[GPU_MAX_TIMERS];
static uint64 gpuFrameNs;
static uint32 gpuSamples;

void
gpuMark(int32 id)
{
	if(!ready || !gpuTimersOn || id < 0 || id >= TS_PER_FRAME)
		return;
	uint32 f = frame % TS_FRAMES;
	// NOT rsxSetTimeStamp: PSL1GHT's writes its data word one slot too far
	// (CURRENTP[2]), the RSX reads garbage and hangs (patch 25's black
	// screen). A report of type 0x10 is the same command, written right.
	rsxSetReport(rsxCtx, 0x10, TS_BASE + f*TS_PER_FRAME + id);
	tsMask[f] |= 1ull << id;
}

static void
gpuCollect(uint32 doneFrame)
{
	uint32 f = doneFrame % TS_FRAMES;
	uint64 m = tsMask[f];
	tsMask[f] = 0;
	if(m == 0)
		return;
	uint32 base = TS_BASE + f*TS_PER_FRAME;
	for(int32 i = 0; i < GPU_MAX_TIMERS; i++)
		if(((m >> (2*i)) & 3) == 3){
			uint64 a = gcmGetTimeStamp(base + 2*i), b = gcmGetTimeStamp(base + 2*i + 1);
			if(b > a && b - a < 1000000000ull)
				gpuNs[i] += b - a;
		}
	if(((m >> GPU_MARK_FRAME_START) & 1) && ((m >> GPU_MARK_FRAME_END) & 1)){
		uint64 a = gcmGetTimeStamp(base + GPU_MARK_FRAME_START), b = gcmGetTimeStamp(base + GPU_MARK_FRAME_END);
		if(b > a && b - a < 1000000000ull){
			gpuFrameNs += b - a;
			gpuSamples++;
		}
	}
}

void
gpuStatsTake(uint64 *ns, uint64 *frameNs, uint32 *samples)
{
	for(int32 i = 0; i < GPU_MAX_TIMERS; i++){
		ns[i] = gpuNs[i];
		gpuNs[i] = 0;
	}
	*frameNs = gpuFrameNs;
	*samples = gpuSamples;
	gpuFrameNs = 0;
	gpuSamples = 0;
}

// ---- present ----------------------------------------------------------------

static u64 statStart;
static u32 statFrames;
static u64 statWaitUs, statCpuUs;
static u64 frameStartUs;
static u64 lastPresentUs, worstFrameUs;
static u32 slowFrames, verySlowFrames;	// over 20 ms (a missed vsync), over 50 ms (a hitch)

static void
logStats(void)
{
	u64 now = sysGetSystemTime();
	if(lastPresentUs){
		u64 dt = now - lastPresentUs;
		if(dt > worstFrameUs) worstFrameUs = dt;
		if(dt > 20000) slowFrames++;
		if(dt > 50000) verySlowFrames++;
	}
	lastPresentUs = now;
	rsceneFrame++;	// patch 151
	if(statStart == 0){
		statStart = now;
		return;
	}
	statFrames++;
	if(now - statStart < 10000000ull)
		return;
	double ms = (now - statStart) / 1000.0 / statFrames;
	uint32 used, total, blocks;
	vramStats(&used, &total, &blocks);
	DrawStats &s = drawStats;
	uint32 n = statFrames;
	rsxLog("[rsx] %.1f fps: frame %.1f ms (CPU %.1f, waiting for the RSX %.1f); per frame %u draws, "
	       "%u verts, %u indices, VP %u FP %u tex %u, %u consts, ring %u KB",
	       1000.0/ms, ms, statCpuUs/1000.0/n, statWaitUs/1000.0/n,
	       s.draws/n, s.verts/n, s.indices/n, s.vpSwitches/n, s.fpSwitches/n, s.texBinds/n,
	       s.constUploads/n, s.ringBytes/1024/n);
	rsxLog("[rsx]   VRAM %u/%u MB in %u blocks; textures uploaded %u KB/s; instanced %u; dropped %u; ring peak %u KB; "
	       "streaming evictions %u; textures without VRAM %d; kept as DXT (VRAM full) %d",
	       used>>20, total>>20, blocks, (uint32)(s.texUploadBytes/1024/10), s.instanced, s.dropped, ringPeak>>10,
	       vramEvictions, rasterStats.noVram, rasterStats.packedDXT);
	rsxLog("[rsx]   frames over 20 ms: %u, over 50 ms: %u, worst %.1f ms",
	       slowFrames, verySlowFrames, worstFrameUs/1000.0);
	if(splitBuildings){
		// patch 168
		if(splitStats.threadFrames)
			rsxLog("[split] the buildings on the second thread: %u frames, %u KB a frame in %.1f chunks (peak %u of %d), "
			       "%u buildings left over for the game thread; it recorded %.2f ms a frame and the game thread waited %.2f ms for it; "
			       "%u RSX waits skipped; %.2f ms a frame waiting for a sub-buffer; %u times a second one in a frame (drawn in the ring)",
			       splitStats.threadFrames, splitStats.bytes / 1024 / splitStats.threadFrames,
			       (double)splitStats.chunks / splitStats.threadFrames, splitStats.peak, SPLIT_CHUNKS, splitStats.leftover,
			       splitStats.workUs / 1000.0 / splitStats.threadFrames, splitStats.joinUs / 1000.0 / splitStats.threadFrames,
			       splitStats.finishSkipped, splitStats.waitUs / 1000.0 / n, splitStats.sameFrame);
		if(splitStats.midChunk || splitStats.overflow)
			rsxLog("[split]   chunks filled in the middle of a building: %u; with no chunk left (commands lost): %u",
			       splitStats.midChunk, splitStats.overflow);
		else
		rsxLog("[split] the buildings in a sub-buffer: %u frames, %u KB a frame (peak %u KB of %u) in %.1f calls a frame, %u stopped full, "
		       "%.2f ms a frame waiting for one to be free",
		       splitStats.calls, splitStats.calls ? splitStats.bytes / 1024 / splitStats.calls : 0, splitStats.peak / 1024,
		       SUB_SIZE / 1024, splitStats.calls ? (double)splitStats.chunks / splitStats.calls : 0.0, splitStats.full,
		       splitStats.waitUs / 1000.0 / n);
		memset(&splitStats, 0, sizeof(splitStats));
	}
	{
		// patch 132: RenderScene's breakdown
		RSceneStats &r = rscene;
		double k = 1000.0 / (double)sysGetTimebaseFrequency() / n;
		// patch 153: the pipelines are timed one frame in four (rsceneOn,
		// main.cpp): their figures are per timed frame
		uint32 ns = r.sampledFrames ? r.sampledFrames : n;
		double ks = 1000.0 / (double)sysGetTimebaseFrequency() / ns;
		uint32 at = 0, me = 0;
		for(int i = 0; i < RSP_NUM; i++){
			at += r.atomics[i];
			me += r.meshes[i];
		}
		double sc = gPS3RSceneTb*k, fx = gPS3REffectsTb*k, pipe = r.pipeTb*ks;
		rsxLog("[rscene] per frame: RenderScene %.2f ms + RenderEffects %.2f ms; the atomic pipelines %.2f ms of them "
		       "(%u atomics, %u meshes, %.2f us an atomic), the rest %.2f ms (the game's lists, sorting and callbacks, Im3D/Im2D)",
		       sc, fx, pipe, at/ns, me/ns, at ? r.pipeTb*ks*ns*1000.0/at : 0.0, sc + fx - pipe);
		rsxLog("[rscene]   in the pipelines (ms): instance %.2f, frame LTM %.2f, world matrix %.2f, lights %.2f (%u a frame), "
		       "vertex arrays %.2f, programs %.2f, skin bones %.2f, materials %.2f, textures %.2f, states %.2f, "
		       "draws %.2f (the draw command and its kick %.2f), car meshes (matfx, all of it) %.2f; flushCache everywhere %.2f",
		       r.tb[RS_INSTANCE]*ks, r.tb[RS_LTM]*ks, r.tb[RS_WORLDMAT]*ks, r.tb[RS_LIGHTS]*ks, r.lights/ns,
		       r.tb[RS_ATTRIBS]*ks, r.tb[RS_PROGRAMS]*ks, r.tb[RS_SKIN]*ks, r.tb[RS_MATERIAL]*ks, r.tb[RS_TEXTURE]*ks,
		       r.tb[RS_STATES]*ks, r.tb[RS_DRAW]*ks, r.cmdTb*ks, r.tb[RS_MATFXMESH]*ks, r.flushTb*ks);
		rsxLog("[rscene]   atomics / meshes a frame: buildings (Leeds) %u / %u, default %u / %u, skinned %u / %u, cars (matfx) %u / %u",
		       r.atomics[RSP_LEEDS]/ns, r.meshes[RSP_LEEDS]/ns, r.atomics[RSP_DEFAULT]/ns, r.meshes[RSP_DEFAULT]/ns,
		       r.atomics[RSP_SKIN]/ns, r.meshes[RSP_SKIN]/ns, r.atomics[RSP_MATFX]/ns, r.meshes[RSP_MATFX]/ns);
		// patch 133: flushCache's parts (everywhere: the pipelines and Im2D/Im3D)
		rsxLog("[rscene]   flushCache (ms): %u a frame, the vertex cache invalidated %u times (with a sync) %.2f, "
		       "render states %.2f, texture samplers %.2f, programs %.2f, matrices / lights / fog worked out %.2f, "
		       "constants uploaded %.2f (%u registers a frame); timed %u frames of %u",
		       r.flushes/ns, r.invalidates/ns, r.tb[RS_F_INVALIDATE]*ks, r.tb[RS_F_STATES]*ks, r.tb[RS_F_SAMPLERS]*ks,
		       r.tb[RS_F_PROGRAMS]*ks, r.tb[RS_F_DERIVED]*ks, r.tb[RS_F_UPLOAD]*ks, drawStats.constUploads/n, ns, n);
		rsxLog("[rscene]   textures: %u bound a frame, %u different ones (sorted by texture, about that many binds); "
		       "%u sampler register writes left out (unchanged)",
		       r.texBinds/ns, r.texDistinct/ns, r.sampRegsSkipped/ns);	// patch 151
		rsxLog("[rscene]   lean flushes (patch 157): %u a frame, %u of them had to be whole",
		       (leanFlushStats[0] + leanFlushStats[1])/n, leanFlushStats[1]/n);
		leanFlushStats[0] = leanFlushStats[1] = 0;
		memset(&rscene, 0, sizeof(rscene));
		gPS3RSceneTb = gPS3REffectsTb = 0;
	}
	if(windFixGeos)
		rsxLog("[rsx]   windings (patch 119): %u triangles turned in %u geometries so far", windFixTris, windFixGeos);
	TexProfile &t = texProfile;
	if(t.txds || t.models || t.others)
		rsxLog("[tex]   streaming: %u TXDs %.1f ms, %u models %.1f ms (DFF parse %.1f, setup %.1f), %u other %.1f ms; "
		       "slowest %.1f ms (%s); instanced %u in %.1f ms (slowest %.1f ms)",
		       t.txds, t.txdUs/1000.0, t.models, t.modelUs/1000.0, t.dffParseUs/1000.0, t.dffSetupUs/1000.0,
		       t.others, t.otherUs/1000.0, t.worstUs/1000.0, t.worstName,
		       t.instCount, t.instUs/1000.0, t.instWorstUs/1000.0);
	if(t.txds){
		// patch 59: what the game thread itself spent (all the TXDs), and the
		// slowest TXD by stage; "read" is the rest (the stream parse, the
		// raster setup, librw's own work)
		uint64 st = 0;
		for(int i = 0; i < TEXPROF_NUM; i++)
			st += t.worstTxdStage[i];
		rsxLog("[tex]   TXDs on the game thread: convert %.1f, to RGBA8 %.1f, swizzle %.1f, VRAM %.1f, mipmaps %.1f, "
		       "palettized %.1f, DXT %.1f ms | slowest TXD %s %.1f ms: convert %.1f, to RGBA8 %.1f, swizzle %.1f, "
		       "VRAM %.1f, mipmaps %.1f, palettized %.1f, DXT %.1f, read/rest %.1f",
		       t.gameUs[TEXPROF_CONVERT]/1000.0, t.gameUs[TEXPROF_FROMIMAGE]/1000.0, t.gameUs[TEXPROF_SWIZZLE]/1000.0,
		       t.gameUs[TEXPROF_VRAM]/1000.0, t.gameUs[TEXPROF_MIPS]/1000.0, t.gameUs[TEXPROF_INDEXED]/1000.0,
		       t.gameUs[TEXPROF_DXT]/1000.0, t.worstTxdName, t.worstTxdUs/1000.0,
		       t.worstTxdStage[TEXPROF_CONVERT]/1000.0, t.worstTxdStage[TEXPROF_FROMIMAGE]/1000.0,
		       t.worstTxdStage[TEXPROF_SWIZZLE]/1000.0, t.worstTxdStage[TEXPROF_VRAM]/1000.0,
		       t.worstTxdStage[TEXPROF_MIPS]/1000.0, t.worstTxdStage[TEXPROF_INDEXED]/1000.0,
		       t.worstTxdStage[TEXPROF_DXT]/1000.0, (t.worstTxdUs > st ? t.worstTxdUs - st : 0)/1000.0);
	}
	if(t.imgLookups)
		rsxLog("[tex]   textures not in their TXD, looked for as image files: %u names, %.1f ms (once per name)",
		       t.imgLookups, t.imgLookupUs/1000.0);
	if(t.levels || t.textures || t.readbacks || t.indexed)
		rsxLog("[tex]   %u levels, %u Ktexels; palettized fast path: %u textures %.1f ms; "
		       "others: %u converted %.1f ms, to RGBA8 %.1f, swizzle %.1f, VRAM write %.1f, mipmaps %.1f, DXT decode %.1f, readback %.1f (%u)",
		       t.levels, (uint32)(t.texels/1000), t.indexed, t.us[TEXPROF_INDEXED]/1000.0,
		       t.textures, t.us[TEXPROF_CONVERT]/1000.0, t.us[TEXPROF_FROMIMAGE]/1000.0, t.us[TEXPROF_SWIZZLE]/1000.0,
		       t.us[TEXPROF_VRAM]/1000.0, t.us[TEXPROF_MIPS]/1000.0, t.us[TEXPROF_DXT]/1000.0, t.us[TEXPROF_READBACK]/1000.0, t.readbacks);
	if(t.direct)
		rsxLog("[tex]   true colour fast path (patch 64): %u textures, %.1f ms on the game thread (to RGBA8 on the texture thread)",
		       t.direct, t.directUs/1000.0);
	if(t.dxtLevels || t.vmxLevels)
		rsxLog("[tex]   native DXT: %u levels (%u compressed here, %.1f ms); mip levels with AltiVec: %u",
		       t.dxtLevels, t.encLevels, t.us[TEXPROF_ENCODE]/1000.0, t.vmxLevels);
	if(t.asyncLevels || t.helpedLevels)
		rsxLog("[tex]   texture thread: %u levels; the game thread finished %u for a draw, %.1f ms in all; backlog now %u Ktexels",
		       t.asyncLevels, t.helpedLevels, t.syncWaitUs/1000.0, textureBacklog()/1000);
	memset(&texProfile, 0, sizeof(texProfile));
	slowFrames = verySlowFrames = 0;
	worstFrameUs = 0;
	memset(&drawStats, 0, sizeof(drawStats));
	ringPeak = 0;
	statStart = now;
	statFrames = 0;
	statWaitUs = statCpuUs = 0;
}

void
videoPresent(Raster *raster, bool vsync)
{
	u32 srcOffset, srcPitch;
	gcmTransferScale scale;
	gcmTransferSurface surface;
	int availW, availH, outW, outH, offX, offY, srcW, srcH;

	if(!ready)
		return;
	// patch 172: the buildings' second thread done before the frame goes
	// (a frame drawn outside the game's Idle, the loading screens: patch 171
	// only waited after RenderEffects)
	if(splitJoinHook)
		splitJoinHook();

	u64 t0 = sysGetSystemTime();
	if(frameStartUs)
		statCpuUs += t0 - frameStartUs;

	if(raster == nil || !rasterSurface(raster, &srcOffset, &srcPitch)){
		// nothing to show: just end the frame
		rsxSetWriteBackendLabel(rsxCtx, LABEL_FRAME, frame);
		rsxFlushBuffer(rsxCtx);
		frame++;
		videoBeginFrame();
		frameStartUs = sysGetSystemTime();
		return;
	}
	srcW = raster->width;
	srcH = raster->height;

	waitFlips();
	u64 t1 = sysGetSystemTime();
	statWaitUs += t1 - t0;
	// patch 46: the perf test's counters (they never reset)
	{
		static DrawStats snap;
		perfCounters.frames++;
		if(frameStartUs)
			perfCounters.cpuUs += t0 - frameStartUs;
		perfCounters.waitUs += t1 - t0;
		DrawStats &d = drawStats;
		if(d.draws < snap.draws)	// logStats reset them
			memset(&snap, 0, sizeof(snap));
		perfCounters.draws += d.draws - snap.draws;
		perfCounters.texBinds += d.texBinds - snap.texBinds;
		perfCounters.vpSwitches += d.vpSwitches - snap.vpSwitches;
		perfCounters.fpSwitches += d.fpSwitches - snap.fpSwitches;
		perfCounters.consts += d.constUploads - snap.constUploads;
		snap = d;
	}

	// keep the picture's aspect, inside the screen fit area, centered
	availW = (int)dispW * settings.screenFit / 100;
	availH = (int)dispH * settings.screenFit / 100;
	if(presentAspect > 0.0f){
		// the TV's pixels are square at 16:9 and 4:3 (1280x720, 720x480 is
		// shown 4:3 or 16:9: dispAspect says which)
		float pixelAspect = dispAspect / ((float)dispW / (float)dispH);
		outH = availH;
		outW = (int)(outH * presentAspect / pixelAspect + 0.5f);
		if(outW > availW){
			outW = availW;
			outH = (int)(outW * pixelAspect / presentAspect + 0.5f);
		}
	}else{
		outH = availH;
		outW = outH * srcW / srcH;
		if(outW > availW){
			outW = availW;
			outH = outW * srcH / srcW;
		}
	}
	outW &= ~1;
	outH &= ~1;
	offX = ((int)dispW - outW) / 2;
	offY = ((int)dispH - outH) / 2;

	if(clearFrames > 0){
		// black borders (only after a fit change: the CPU writes VRAM)
		memset(fb[cur], 0, dispPitch*dispH);
		vramFlush(fb[cur], dispPitch*dispH);
		clearFrames--;
	}

	memset(&scale, 0, sizeof(scale));
	scale.conversion = GCM_TRANSFER_CONVERSION_TRUNCATE;
	scale.format = GCM_TRANSFER_SCALE_FORMAT_A8R8G8B8;
	scale.operation = GCM_TRANSFER_OPERATION_SRCCOPY;
	scale.clipX = offX;
	scale.clipY = offY;
	scale.clipW = outW;
	scale.clipH = outH;
	scale.outX = offX;
	scale.outY = offY;
	scale.outW = outW;
	scale.outH = outH;
	scale.ratioX = rsxGetFixedSint32((float)srcW / (float)outW);
	scale.ratioY = rsxGetFixedSint32((float)srcH / (float)outH);
	scale.inW = srcW;
	scale.inH = srcH;
	scale.pitch = srcPitch;
	scale.origin = GCM_TRANSFER_ORIGIN_CORNER;
	scale.interp = settings.bilinear ? GCM_TRANSFER_INTERPOLATOR_LINEAR : GCM_TRANSFER_INTERPOLATOR_NEAREST;
	scale.offset = srcOffset;
	scale.inX = 0;
	scale.inY = 0;

	memset(&surface, 0, sizeof(surface));
	surface.format = GCM_TRANSFER_SURFACE_FORMAT_A8R8G8B8;
	surface.pitch = dispPitch;
	surface.offset = fbOffset[cur];

	__asm__ volatile("sync" ::: "memory");
	// the 3D unit must be done with the image before the scaler reads it,
	// and the scaler done before the next frame draws into it again
	rsxSetWaitForIdle(rsxCtx);
	rsxSetTransferScaleSurface(rsxCtx, &scale, &surface);
	rsxSetWaitForIdle(rsxCtx);

	// no gcmSetWaitFlip: the RSX goes on with the next frame while this
	// flip waits for the vsync (waitFlips keeps the CPU from reusing a
	// display buffer that is still on screen or queued)
	gpuMark(GPU_MARK_FRAME_END);
	{
		u64 p0 = sysGetSystemTime();
		paceFlip();
		statWaitUs += sysGetSystemTime() - p0;
	}
	gcmSetFlip(rsxCtx, cur);
	// the frame fence: written once everything before it is done
	rsxSetWriteBackendLabel(rsxCtx, LABEL_FRAME, frame);
	rsxFlushBuffer(rsxCtx);

	flipQueued++;
	if(flipQueued <= 3)
		rsxLog("[rsx] frame %u presented: %dx%d -> %dx%d at %d,%d", flipQueued, srcW, srcH, outW, outH, offX, offY);
	cur = (cur + 1) % FB_COUNT;
	frame++;

	(void)vsync;	// flips are always on vsync (no tearing)

	logStats();
	videoBeginFrame();
	frameStartUs = sysGetSystemTime();
}

}
}

#endif
