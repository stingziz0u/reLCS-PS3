// spuparticles.h -- res-PS3 patch 50: the particle job shared by the PPU
// (src/renderer/Particle.cpp) and the SPU program (ps3/spu/spuparticles.c).
//
// CParticle::Render walks the particle lists on the PPU as before (render
// states, texture frames, the random sizes) but, instead of projecting every
// particle and building its sprite, writes one SpuPartSprite per particle and
// cuts them into batches (a batch: sprites drawn with the same states). The
// SPU projects them (CSprite::CalcScreenCoors), drops the ones off screen or
// behind the camera, builds the visible ones as Im2D quads (CSprite's
// RenderBufferedOneXLUSprite / _Rotate_Dimension maths) straight into the
// frame's vertex ring, and says per batch where its quads went and how many.

#ifndef SPUPARTICLES_H
#define SPUPARTICLES_H

#include <stdint.h>

#define SPUPART_MAX_SPRITES	1024
#define SPUPART_MAX_BATCHES	1024
#define SPUPART_QUAD_BYTES	112	// 4 Im2DVertex of 28 bytes
#define SPUPART_SPUP		13	// SPU thread user event port
#define SPUPART_QUIT		0

enum
{
	SPUPART_PLAIN = 0,	// RenderBufferedOneXLUSprite (clipped to the screen)
	SPUPART_ROTATE,		// RenderBufferedOneXLUSprite_Rotate_Dimension
	SPUPART_SPEEDTRAIL,	// rotated along its screen motion, stretched by it
};
#define SPUPART_MIN12		1	// w and h at least 12 (PARTICLE_FLYERS)

typedef struct SpuPartSprite
{
	float x, y, z;		// world position
	float wMul, hMul;	// the sprite's w = wMul * (screen w + addW), h the same (+ trail)
	float addW, addH;
	float rot;		// ROTATE: radians
	float px, py, pz;	// SPEEDTRAIL: the position a step back
	float trailMul;		// SPEEDTRAIL: psystem->m_fTrailLengthMultiplier
	uint8_t r, g, b, a;	// with the intensity
	uint16_t kind, flags;
	uint32_t pad[2];
} __attribute__((aligned(16))) SpuPartSprite;	// 64 bytes

typedef struct SpuPartBatch
{
	uint32_t first, count;		// in: sprites
	uint32_t outFirst, outCount;	// out: quads in the output
} __attribute__((aligned(16))) SpuPartBatch;

typedef struct SpuPartJob
{
	uint64_t spritesEa;	// SpuPartSprite[numSprites]
	uint64_t batchesEa;	// SpuPartBatch[numBatches], written back
	uint64_t outEa;		// quads (SPUPART_QUAD_BYTES each), 16-byte aligned
	uint32_t numSprites, numBatches;
	float view[12];		// TheCamera.m_viewMatrix: right, forward, up, position
	float nearClip, farClip;	// CDraw
	float screenW, screenH;
	float kw, kh;		// screen w = kw / z, h = kh / z (CalcScreenCoors)
	float near2D, far2D;	// CSprite's 2D near/far screen z
	uint32_t seq;
	uint32_t numQuads;	// out: quads written
	uint32_t pad[2];
} __attribute__((aligned(128))) SpuPartJob;

#endif
