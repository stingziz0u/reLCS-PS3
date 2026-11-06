// spuanim.c -- res-PS3 patch 132: the peds' bones on an SPU (spuanim.h).
//
// Signal 1: the address of a SpuAnimJob; 0 ends the program. Until the PPU
// says "final" in the SpuAnimCtrl, the bones it has published are taken in
// chunks: the chunk's SpuAnimBones and their SpuAnimNodes are read, then the
// two keyframes of every node that has a blend (a 48-byte window around each:
// the keyframes are 4-byte aligned, 20 or 36 bytes long), the bones are
// worked out (spuAnimBone) and their SpuAnimOuts written back. At the end an
// event to the PPU: the job's sequence number and the SPU's busy time in
// timebase ticks (the decrementer runs at the timebase's frequency).

#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <sys/spu_thread.h>
#include <sys/spu_event.h>

#include "spuanim.h"

#define TAG_IN		1
#define TAG_KF		2
#define TAG_OUT		3
#define CHUNK		64	// bones at a time
#define MAX_CHUNK_NODES	(CHUNK * 11)	// 11 animations at most on a bone (updateData.nodes)
#define KF_WIN		48

static SpuAnimJob job __attribute__((aligned(128)));
static SpuAnimCtrl ctrl __attribute__((aligned(128)));
static SpuAnimBone bones[CHUNK] __attribute__((aligned(128)));
static SpuAnimNode nodes[MAX_CHUNK_NODES] __attribute__((aligned(128)));
static uint8_t kfWin[MAX_CHUNK_NODES][2][KF_WIN] __attribute__((aligned(128)));
static SpuAnimOut outs[CHUNK] __attribute__((aligned(128)));
static const float *kfa[MAX_CHUNK_NODES], *kfb[MAX_CHUNK_NODES];

static void
waitTag(uint32_t tag)
{
	mfc_write_tag_mask(1 << tag);
	mfc_read_tag_status_all();
}

// size: a multiple of 16
static void
dmaGetLarge(volatile void *ls, uint32_t ea, uint32_t size, uint32_t tag)
{
	uint8_t *p = (uint8_t*)ls;
	while(size > 0){
		uint32_t n = size > 16384 ? 16384 : size;
		mfc_get(p, ea, n, tag, 0, 0);
		p += n;
		ea += n;
		size -= n;
	}
}

static const float*
fetchKf(uint32_t ea, uint8_t *win)
{
	uint32_t base = ea & ~15u;
	mfc_get(win, base, KF_WIN, TAG_KF, 0, 0);	// the MFC queue stalls when full
	return (const float*)(win + (ea - base));
}

static void
doChunk(uint32_t first, uint32_t count)
{
	uint32_t i, j;
	dmaGetLarge(bones, job.bonesEa + first * sizeof(SpuAnimBone), count * sizeof(SpuAnimBone), TAG_IN);
	waitTag(TAG_IN);
	uint32_t n0 = bones[0].firstNode;
	uint32_t n1 = bones[count - 1].firstNode + bones[count - 1].numNodes;
	uint32_t nn = n1 - n0;
	if(nn > MAX_CHUNK_NODES)
		nn = MAX_CHUNK_NODES;	// can't happen (11 per bone)
	if(nn)
		dmaGetLarge(nodes, job.nodesEa + n0 * sizeof(SpuAnimNode), nn * sizeof(SpuAnimNode), TAG_IN);
	waitTag(TAG_IN);
	for(j = 0; j < nn; j++){
		if(nodes[j].kfA && nodes[j].kfB && nodes[j].blend > 0.0f){
			kfa[j] = fetchKf(nodes[j].kfA, kfWin[j][0]);
			kfb[j] = fetchKf(nodes[j].kfB, kfWin[j][1]);
		}else
			kfa[j] = kfb[j] = 0;
	}
	waitTag(TAG_KF);
	waitTag(TAG_OUT);	// the last chunk's results are out before outs is refilled
	for(i = 0; i < count; i++){
		uint32_t k = bones[i].firstNode - n0;
		if(k + bones[i].numNodes > nn){
			// out of the chunk's nodes (can't happen): the rest pose's rotation
			outs[i].q[0] = outs[i].q[1] = outs[i].q[2] = 0.0f;
			outs[i].q[3] = 1.0f;
			outs[i].t[0] = bones[i].resetPos[0];
			outs[i].t[1] = bones[i].resetPos[1];
			outs[i].t[2] = bones[i].resetPos[2];
			outs[i].t[3] = 0.0f;
			continue;
		}
		spuAnimBone(&bones[i], &nodes[k], &kfa[k], &kfb[k], &outs[i]);
	}
	mfc_put(outs, job.outEa + first * sizeof(SpuAnimOut), count * sizeof(SpuAnimOut), TAG_OUT, 0, 0);
}

int
main(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4)
{
	(void)arg1; (void)arg2; (void)arg3; (void)arg4;
	for(;;){
		uint32_t jobEa = spu_read_signal1();
		if(jobEa == SPUANIM_QUIT)
			break;
		mfc_get(&job, jobEa, sizeof(job), TAG_IN, 0, 0);
		waitTag(TAG_IN);
		spu_write_decrementer(0xFFFFFFFF);
		uint32_t busy = 0;
		uint32_t done = 0;
		for(;;){
			mfc_get(&ctrl, job.ctrlEa, 16, TAG_IN, 0, 0);
			waitTag(TAG_IN);
			uint32_t avail = ctrl.seq == job.seq ? ctrl.bones : 0;
			if(avail > SPUANIM_MAX_BONES)
				avail = SPUANIM_MAX_BONES;
			if(done < avail){
				uint32_t n = avail - done;
				if(n > CHUNK)
					n = CHUNK;
				uint32_t d0 = spu_read_decrementer();
				doChunk(done, n);
				busy += d0 - spu_read_decrementer();
				done += n;
				continue;
			}
			if(ctrl.seq == job.seq && ctrl.final)
				break;
			// nothing new yet: a short pause before asking again (~1 us)
			uint32_t w = spu_read_decrementer();
			while(w - spu_read_decrementer() < 80)
				;
		}
		waitTag(TAG_OUT);
		spu_thread_send_event(SPUANIM_SPUP, job.seq & 0xFFFFFF, busy);
	}
	spu_thread_exit(0);
	return 0;
}
