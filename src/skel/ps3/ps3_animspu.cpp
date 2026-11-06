// ps3_animspu.cpp -- res-PS3 patch 132: the peds' bones on an SPU (see
// ps3/spu/spuanim.h for the whole of it).
//
// CWorld::Process: PS3_AnimSpuBegin before the animation loop (the SPU starts
// waiting for bones), FrameUpdateCallBackSkinned hands every bone that isn't
// the root to PS3_AnimSpuCollect, RpAnimBlendClumpUpdateAnimations publishes
// the clump's bones when it's done with them (PS3_AnimSpuPublish), and
// PS3_AnimSpuEnd after the loop waits for the SPU and puts the results in the
// RpHAnim frames, before any ProcessControl. USRDIR/nospuanim.txt: as before,
// on the PPU. If the SPU doesn't answer, the PPU works the bones out from the
// same data (and does everything itself from then on).

#include "common.h"
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <sys/systime.h>

#include "ps3_platform.h"
#include "AnimBlendAssociation.h"
#include "AnimBlendNode.h"
#include "AnimBlendSequence.h"
#include "AnimBlendClumpData.h"
#include "RpAnimBlend.h"
#include "../../../ps3/spu/spuanim.h"

extern "C" const uint8 spuanim_elf[];	// the SPU program (Makefile, .incbin)

static_assert(sizeof(SpuAnimNode) == 32, "SpuAnimNode layout");
static_assert(sizeof(SpuAnimBone) == 32, "SpuAnimBone layout");
static_assert(sizeof(SpuAnimOut) == 32, "SpuAnimOut layout");
static_assert(sizeof(SpuAnimJob) == 128, "SpuAnimJob layout");
static_assert(offsetof(KeyFrameTrans, translation) == SPUANIM_KF_TRANS, "KeyFrameTrans layout");
static_assert(CAnimBlendSequence::KF_ROT == SPUANIM_ROT && CAnimBlendSequence::KF_TRANS == SPUANIM_TRANS, "sequence types");
static_assert(AnimBlendFrameData::IGNORE_ROTATION == SPUANIM_IGNORE_ROT &&
              AnimBlendFrameData::IGNORE_TRANSLATION == SPUANIM_IGNORE_TRANS, "frame flags");

bool gPS3AnimCollect;		// inside the animation loop with the SPU on
static int sState;		// 0 not started, 1 on, 2 off
static PS3SpuTask sTask;
static uint32 sSeq;
static SpuAnimJob sJob __attribute__((aligned(128)));
static volatile SpuAnimCtrl sCtrl __attribute__((aligned(128)));
static SpuAnimBone *sBones;
static SpuAnimNode *sNodes;
static SpuAnimOut *sOuts;
static RpHAnimStdInterpFrame **sXforms;	// the bones' frames (PPU only)
static uint32 sNumBones, sNumNodes;

// [spuanim] every 10 s
static uint32 stFrames, stBones, stNodes, stFull, stPpuFrames;
static uint64 stWaitUs, stSpuTicks, stApplyTicks, stLast;

static void
Start(void)
{
	sState = 2;
	FILE *f = fopen(PS3_USRDIR "/nospuanim.txt", "rb");
	if(f){
		fclose(f);
		PS3_Log("[spuanim] nospuanim.txt: the bones stay on the PPU");
		return;
	}
	sBones = (SpuAnimBone*)memalign(128, SPUANIM_MAX_BONES * sizeof(SpuAnimBone));
	sNodes = (SpuAnimNode*)memalign(128, SPUANIM_MAX_NODES * sizeof(SpuAnimNode));
	sOuts = (SpuAnimOut*)memalign(128, SPUANIM_MAX_BONES * sizeof(SpuAnimOut));
	sXforms = (RpHAnimStdInterpFrame**)malloc(SPUANIM_MAX_BONES * sizeof(RpHAnimStdInterpFrame*));
	if(!sBones || !sNodes || !sOuts || !sXforms){
		PS3_Log("[spuanim] no memory: the bones stay on the PPU");
		return;
	}
	if(!PS3_SpuTaskStart(&sTask, spuanim_elf, "resanim", SPUANIM_SPUP)){
		PS3_Log("[spuanim] the bones stay on the PPU");
		return;
	}
	PS3_Logf("[spuanim] the peds' bones on the SPU (up to %d bones, %d animation nodes a frame)",
	         SPUANIM_MAX_BONES, SPUANIM_MAX_NODES);
	sState = 1;
}

void
PS3_AnimSpuBegin(void)
{
	gPS3AnimCollect = false;
	if(sState == 0)
		Start();
	if(sState != 1 || !sTask.ready)
		return;
	sNumBones = sNumNodes = 0;
	sSeq++;
	sCtrl.bones = 0;
	sCtrl.final = 0;
	sCtrl.seq = sSeq;
	sJob.ctrlEa = (uint32)(uintptr_t)&sCtrl;
	sJob.bonesEa = (uint32)(uintptr_t)sBones;
	sJob.nodesEa = (uint32)(uintptr_t)sNodes;
	sJob.outEa = (uint32)(uintptr_t)sOuts;
	sJob.seq = sSeq;
	if(!PS3_SpuTaskKick(&sTask, (uint32)(uintptr_t)&sJob))
		return;
	gPS3AnimCollect = true;
}

// FrameUpdateCallBackSkinned for a bone without velocity extraction: the
// nodes' advance and the bone's data for the SPU. false: no room, the caller
// does the bone itself (nothing was changed).
bool
PS3_AnimSpuCollect(AnimBlendFrameData *frame, AnimBlendFrameUpdateData *updateData)
{
	CAnimBlendNode **node;
	int n = 0;
	for(node = updateData->nodes; *node; node++)
		n++;
	if(sNumBones >= SPUANIM_MAX_BONES || sNumNodes + n > SPUANIM_MAX_NODES){
		stFull++;
		return false;
	}

	float totalBlendAmount = 0.0f;
	if(updateData->foobar)
		for(node = updateData->nodes; *node; node++)
			if((*node)->sequence && (*node)->association->IsPartial())
				totalBlendAmount += (*node)->association->blendAmount;

	SpuAnimBone *b = &sBones[sNumBones];
	b->firstNode = sNumNodes;
	uint32 count = 0;
	for(node = updateData->nodes; *node; node++){
		CAnimBlendNode *nd = *node;
		if(nd->sequence){
			// CAnimBlendNode::Update up to the interpolation
			if(nd->association->IsRunning()){
				nd->remainingTime -= nd->association->timeStep;
				if(nd->remainingTime <= 0.0f)
					nd->NextKeyFrame();
			}
			SpuAnimNode *s = &sNodes[sNumNodes + count++];
			float blend = nd->association->GetBlendAmount(1.0f - totalBlendAmount);
			s->type = nd->sequence->type & (SPUANIM_ROT | SPUANIM_TRANS);
			s->blend = blend;
			s->remaining = nd->remainingTime;
			s->theta = nd->theta;
			s->invSin = nd->invSin;
			if(blend > 0.0f && nd->sequence->numFrames > 0){
				int a = nd->frameA < 0 ? 0 : nd->frameA;
				int bb = nd->frameB < 0 ? 0 : nd->frameB;
				s->kfA = (uint32)(uintptr_t)nd->sequence->GetKeyFrame(a);
				s->kfB = (uint32)(uintptr_t)nd->sequence->GetKeyFrame(bb);
			}else
				s->kfA = s->kfB = 0;
		}
		++*node;
	}
	b->numNodes = count;
	b->flags = frame->flag & (SPUANIM_IGNORE_ROT | SPUANIM_IGNORE_TRANS);
	b->resetPos[0] = frame->resetPos.x;
	b->resetPos[1] = frame->resetPos.y;
	b->resetPos[2] = frame->resetPos.z;
	sXforms[sNumBones] = frame->hanimFrame;
	sNumNodes += count;
	sNumBones++;
	return true;
}

// a clump's bones are written: the SPU can take them
void
PS3_AnimSpuPublish(void)
{
	if(!gPS3AnimCollect)
		return;
	__asm__ volatile("lwsync" ::: "memory");
	sCtrl.bones = sNumBones;
}

// the PPU works the bones out itself (the SPU didn't answer)
static void
PpuBones(void)
{
	const float *kfa[16], *kfb[16];
	for(uint32 i = 0; i < sNumBones; i++){
		SpuAnimBone *b = &sBones[i];
		SpuAnimNode *nd = &sNodes[b->firstNode];
		uint32 k = b->numNodes > 16 ? 16 : b->numNodes;
		SpuAnimBone bc = *b;
		bc.numNodes = k;
		for(uint32 j = 0; j < k; j++){
			kfa[j] = (const float*)(uintptr_t)nd[j].kfA;
			kfb[j] = (const float*)(uintptr_t)nd[j].kfB;
		}
		spuAnimBone(&bc, nd, kfa, kfb, &sOuts[i]);
	}
}

void
PS3_AnimSpuEnd(void)
{
	if(!gPS3AnimCollect)
		return;
	gPS3AnimCollect = false;
	__asm__ volatile("lwsync" ::: "memory");
	sCtrl.bones = sNumBones;
	__asm__ volatile("lwsync" ::: "memory");
	sCtrl.final = 1;
	__asm__ volatile("sync" ::: "memory");

	uint64 waitUs = 0;
	uint32 busy = 0;
	bool ok = PS3_SpuTaskWait(&sTask, sSeq, 30 * 1000, &waitUs);
	if(ok)
		busy = sTask.lastData1;
	else{
		PS3_Logf("[spuanim] the SPU didn't answer: %u bones of this frame on the PPU, and all of them from now on", sNumBones);
		sState = 2;
		PpuBones();
		stPpuFrames++;
	}

	uint64 t0 = PS3_Ticks();
	for(uint32 i = 0; i < sNumBones; i++){
		RpHAnimStdInterpFrame *x = sXforms[i];
		const SpuAnimOut *o = &sOuts[i];
		uint16 fl = sBones[i].flags;
		if(!(fl & SPUANIM_IGNORE_ROT)){
			x->q.imag.x = o->q[0];
			x->q.imag.y = o->q[1];
			x->q.imag.z = o->q[2];
			x->q.real = o->q[3];
		}
		if(!(fl & SPUANIM_IGNORE_TRANS)){
			x->t.x = o->t[0];
			x->t.y = o->t[1];
			x->t.z = o->t[2];
		}
	}
	stApplyTicks += PS3_Ticks() - t0;

	stFrames++;
	stBones += sNumBones;
	stNodes += sNumNodes;
	stWaitUs += waitUs;
	stSpuTicks += busy;
	uint64 now = sysGetSystemTime();
	if(stLast == 0)
		stLast = now;
	if(now - stLast >= 10000000ull && stFrames){
		double tbMs = 1000.0 / (double)sysGetTimebaseFrequency();
		PS3_Logf("[spuanim] per frame: %u bones, %u animation nodes; the SPU worked %.2f ms, the PPU waited %.2f ms for it at the end, "
		         "results copied in %.3f ms; %u bones done by the PPU (no room), %u frames done by the PPU (the SPU didn't answer)",
		         stBones / stFrames, stNodes / stFrames, stSpuTicks * tbMs / stFrames, stWaitUs / 1000.0 / stFrames,
		         stApplyTicks * tbMs / stFrames, stFull, stPpuFrames);
		stFrames = stBones = stNodes = stFull = stPpuFrames = 0;
		stWaitUs = stSpuTicks = stApplyTicks = 0;
		stLast = now;
	}
}
