// spuanim.h -- res-PS3 patch 132: the peds' bones on an SPU, shared by the
// PPU (src/skel/ps3/ps3_animspu.cpp) and the SPU program (ps3/spu/spuanim.c).
//
// CWorld::Process animates every moving entity before any of them runs its
// ProcessControl. For the skinned clumps drawn this frame, the PPU still does
// what needs the game's data (the blend amounts, the keyframe advance, the
// root bone with its velocity extraction) and, for every other bone, writes
// one SpuAnimBone and its SpuAnimNodes (one per animation playing on it:
// where its two keyframes are, how far between them, the blend). The SPU is
// started when the loop starts and works through the bones as the PPU
// publishes them (SpuAnimCtrl.bones), while the PPU goes on with the next
// peds: per node it fetches the two keyframes, interpolates (lerp, slerp),
// blends, sums the nodes, normalises, and writes the bone's quaternion and
// translation (SpuAnimOut). When the loop ends the PPU says "final", waits
// for the SPU's event and copies the results into the bones' RpHAnim frames
// before the first ProcessControl. spuAnimBone below is FrameUpdateCallBack-
// Skinned's maths (with patch 88), for both sides: the PPU runs it itself
// when the SPU doesn't answer.

#ifndef SPUANIM_H
#define SPUANIM_H

#include <stdint.h>
#include <math.h>

#define SPUANIM_SPUP		14	// SPU thread user event port
#define SPUANIM_QUIT		0
#define SPUANIM_MAX_BONES	3072
#define SPUANIM_MAX_NODES	12288
#define SPUANIM_KF_TRANS	20	// offsetof(KeyFrameTrans, translation)

// SpuAnimNode.type: the sequence's (CAnimBlendSequence KF_ROT, KF_TRANS)
#define SPUANIM_ROT		1
#define SPUANIM_TRANS		2
// SpuAnimBone.flags: AnimBlendFrameData's
#define SPUANIM_IGNORE_ROT	2
#define SPUANIM_IGNORE_TRANS	4

typedef struct SpuAnimNode
{
	uint32_t kfA, kfB;	// the keyframes (KeyFrame / KeyFrameTrans), 0: blend <= 0, nothing to fetch
	float remaining;	// CAnimBlendNode::remainingTime after the advance
	float blend;		// association->GetBlendAmount(1 - the partial ones' blend)
	float theta, invSin;	// CAnimBlendNode's, for the slerp
	uint32_t type;		// SPUANIM_ROT | SPUANIM_TRANS
	uint32_t pad;
} __attribute__((aligned(16))) SpuAnimNode;	// 32 bytes

typedef struct SpuAnimBone
{
	uint32_t firstNode;
	uint16_t numNodes, flags;
	float resetPos[3];
	uint32_t pad[3];
} __attribute__((aligned(16))) SpuAnimBone;	// 32 bytes

typedef struct SpuAnimOut
{
	float q[4];		// x, y, z, w
	float t[4];
} __attribute__((aligned(16))) SpuAnimOut;	// 32 bytes

// written by the PPU while the SPU works, read by the SPU (16 bytes)
typedef struct SpuAnimCtrl
{
	uint32_t bones;		// bones (and their nodes) ready
	uint32_t final;		// no more this frame
	uint32_t seq;
	uint32_t pad;
} __attribute__((aligned(128))) SpuAnimCtrl;

typedef struct SpuAnimJob
{
	uint32_t ctrlEa, bonesEa, nodesEa, outEa;
	uint32_t seq;
	uint32_t pad[27];
} __attribute__((aligned(128))) SpuAnimJob;	// 128 bytes

// CQuaternion::Slerp(q1, q2, theta, invSin, t), times blend
static inline void
spuAnimSlerp(float *o, const float *q1, const float *q2, float theta, float invSin, float t, float blend)
{
	if(theta == 0.0f){
		o[0] = q2[0]*blend; o[1] = q2[1]*blend; o[2] = q2[2]*blend; o[3] = q2[3]*blend;
		return;
	}
	float w1, w2;
	if(theta > 3.14159265f/2.0f){
		theta = 3.14159265f - theta;
		w1 = sinf((1.0f - t) * theta) * invSin;
		w2 = -sinf(t * theta) * invSin;
	}else{
		w1 = sinf((1.0f - t) * theta) * invSin;
		w2 = sinf(t * theta) * invSin;
	}
	o[0] = (w1*q1[0] + w2*q2[0])*blend;
	o[1] = (w1*q1[1] + w2*q2[1])*blend;
	o[2] = (w1*q1[2] + w2*q2[2])*blend;
	o[3] = (w1*q1[3] + w2*q2[3])*blend;
}

// one bone: FrameUpdateCallBackSkinned after its nodes' advance. kfa[i],
// kfb[i]: node i's keyframes (rotation x y z w, deltaTime, then the
// translation at SPUANIM_KF_TRANS bytes), nil when node i has none.
static inline void
spuAnimBone(const SpuAnimBone *b, const SpuAnimNode *nodes, const float *const *kfa, const float *const *kfb, SpuAnimOut *out)
{
	float px = 0.0f, py = 0.0f, pz = 0.0f;
	float rot[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float transBlend = 0.0f;
	uint32_t i;
	for(i = 0; i < b->numNodes; i++){
		const SpuAnimNode *n = &nodes[i];
		const float *A = kfa[i], *B = kfb[i];
		float q[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		float vx = 0.0f, vy = 0.0f, vz = 0.0f;
		if(n->blend > 0.0f && A && B){
			float dtA = A[4];
			float t = dtA == 0.0f ? 0.0f : (dtA - n->remaining)/dtA;
			if(n->type & SPUANIM_TRANS){
				const float *ta = A + SPUANIM_KF_TRANS/4, *tb = B + SPUANIM_KF_TRANS/4;
				vx = (tb[0] + t*(ta[0] - tb[0])) * n->blend;
				vy = (tb[1] + t*(ta[1] - tb[1])) * n->blend;
				vz = (tb[2] + t*(ta[2] - tb[2])) * n->blend;
			}
			if(n->type & SPUANIM_ROT)
				spuAnimSlerp(q, B, A, n->theta, n->invSin, t, n->blend);
		}
		if(n->type & SPUANIM_TRANS){
			px += vx; py += vy; pz += vz;
			transBlend += n->blend;
		}
		float dot = rot[0]*q[0] + rot[1]*q[1] + rot[2]*q[2] + rot[3]*q[3];
		if(dot < 0.0f){
			rot[0] -= q[0]; rot[1] -= q[1]; rot[2] -= q[2]; rot[3] -= q[3];
		}else{
			rot[0] += q[0]; rot[1] += q[1]; rot[2] += q[2]; rot[3] += q[3];
		}
	}
	// CQuaternion::Normalise
	float sq = rot[0]*rot[0] + rot[1]*rot[1] + rot[2]*rot[2] + rot[3]*rot[3];
	if(sq == 0.0f)
		rot[3] = 1.0f;
	else{
		float inv = 1.0f/sqrtf(sq);
		rot[0] *= inv; rot[1] *= inv; rot[2] *= inv; rot[3] *= inv;
	}
	out->q[0] = rot[0]; out->q[1] = rot[1]; out->q[2] = rot[2]; out->q[3] = rot[3];
	// patch 88: the weighted sum plus the rest pose for the weight left
	if(transBlend > 1.0f){
		float inv = 1.0f/transBlend;
		px *= inv; py *= inv; pz *= inv;
		transBlend = 1.0f;
	}
	out->t[0] = px + (1.0f - transBlend)*b->resetPos[0];
	out->t[1] = py + (1.0f - transBlend)*b->resetPos[1];
	out->t[2] = pz + (1.0f - transBlend)*b->resetPos[2];
	out->t[3] = 0.0f;
}

#endif
