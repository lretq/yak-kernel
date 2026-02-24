/* 
Port of the Boron "firework test"

Copyright 2023-2024 iProgramInCpp

Redistribution and use in source and binary forms, with or
without  modification,  are permitted  provided  that  the
following conditions are met:

1. Redistributions  of  source code  must retain  the above
copyright notice, this list of conditions and the following
disclaimer.

2. Redistributions in binary form must  reproduce the above
copyright notice, this list of conditions and the following
disclaimer  in  the  documentation  and/or  other materials
provided with the distribution.

3. Neither the name of the copyright holder nor the names of
its contributors may be used to endorse or  promote products
derived  from this  software without specific  prior written
permission.

THIS  SOFTWARE  IS  PROVIDED BY THE  COPYRIGHT HOLDERS  AND
CONTRIBUTORS  “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED  TO, THE  IMPLIED  WARRANTIES  OF
MERCHANTABILITY  AND  FITNESS  FOR  A PARTICULAR PURPOSE ARE
DISCLAIMED.   IN  NO EVENT  SHALL  THE  COPYRIGHT HOLDER  OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE,  DATA,  OR PROFITS;   OR BUSINESS INTERRUPTION)
HOWEVER CAUSED  AND ON ANY  THEORY OF LIABILITY,  WHETHER IN
CONTRACT,  STRICT LIABILITY,  OR TORT  (INCLUDING NEGLIGENCE
OR OTHERWISE)  ARISING IN  ANY WAY  OUT OF  THE USE OF  THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <yak/heap.h>
#include <yak/hint.h>
#include <yak/sched.h>
#include <yak/timer.h>

#define NO_RETURN [[gnu::noreturn]]
#define ASM asm volatile
#define ASSERT assert
#define UNUSED [[maybe_unused]]

// 2023... It was a year of giant changes for me. I'm sorry that I couldn't
// get a fully stable demo (it seems to be reasonably stable on 4 cores but
// cracks on 32 cores...), but yeah, it is what it is, I'll fix it in 2024.

// For now, this is the last line of code of 2023.

// ####### GRAPHICS BACKEND #######

#define BACKGROUND_COLOR 0x09090F

uint8_t *PixBuff;
int PixWidth, PixHeight, PixPitch, PixBPP;

void PlotPixel(uint32_t Color, int X, int Y)
{
	if (X < 0 || Y < 0 || X >= PixWidth || Y >= PixHeight)
		return;

	uint8_t *Place = PixBuff + PixPitch * Y + X * PixBPP / 8;

	switch (PixBPP / 8) {
	case 1:
		*Place = (uint8_t)(Color | (Color >> 8) | (Color >> 16));
		break;
	case 2:
		*((uint16_t *)Place) = Color | (Color >> 16);
		break;
	case 3:
		Place[0] = (uint8_t)Color;
		Place[1] = (uint8_t)(Color >> 8);
		Place[2] = (uint8_t)(Color >> 16);
		break;
	case 4:
		*((uint32_t *)Place) = Color;
		break;
	default:
		ASSERT(!"Oh no!");
	}
}

void FillScreen(uint32_t Color)
{
	for (int Y = 0; Y < PixHeight; Y++) {
		switch (PixBPP / 8) {
		case 1: {
			uint8_t Value =
				(uint8_t)(Color | (Color >> 8) | (Color >> 16));
			uint8_t *Place = PixBuff + PixPitch * Y;
			for (int X = 0; X < PixWidth; X++) {
				*Place = Value;
				Place++;
			}
			break;
		}
		case 2: {
			uint16_t Value = (uint16_t)(Color | (Color >> 16));
			uint16_t *Place = (uint16_t *)(PixBuff + PixPitch * Y);
			for (int X = 0; X < PixWidth; X++) {
				*Place = Value;
				Place++;
			}
			break;
		}
		case 3: {
			uint8_t *Place = PixBuff + PixPitch * Y;
			for (int X = 0; X < PixWidth; X++) {
				Place[0] = (uint8_t)Color;
				Place[1] = (uint8_t)(Color >> 8);
				Place[2] = (uint8_t)(Color >> 16);
				Place += 3;
			}
			break;
		}
		case 4: {
			uint32_t *Place = (uint32_t *)(PixBuff + PixPitch * Y);
			for (int X = 0; X < PixWidth; X++) {
				*Place = Color;
				Place++;
			}
			break;
		}
		default:
			ASSERT(!"Oh no!");
		}
	}
}

#include <limine.h>

extern struct limine_framebuffer limine_fb0_copy;
extern size_t kinfo_height_start;

#include <yak/log.h>

// Initializes the graphics backend.
void Init()
{
	struct limine_framebuffer *FrameBuffer = &limine_fb0_copy;

	PixBuff = FrameBuffer->address;
	PixWidth = FrameBuffer->width;
	PixHeight = kinfo_height_start;
	PixPitch = FrameBuffer->pitch;
	PixBPP = FrameBuffer->bpp;
}

// ####### UTILITY LIBRARY #######

uint64_t ReadTsc()
{
	uint64_t low, high;

	// note: The rdtsc instruction is specified to zero out the top 32 bits of rax and rdx.
	ASM("rdtsc" : "=a"(low), "=d"(high));

	// So something like this is fine.
	return high << 32 | low;
}

unsigned RandTscBased()
{
	uint64_t Tsc = ReadTsc();
	return ((uint32_t)Tsc ^ (uint32_t)(Tsc >> 32));
}

int g_randGen = 0x9521af17;
__no_prof __no_san static inline int Rand()
{
	g_randGen += (int)0xe120fc15;
	uint64_t tmp = (uint64_t)g_randGen * 0x4a39b70d;
	uint32_t m1 = (tmp >> 32) ^ tmp;
	tmp = (uint64_t)m1 * 0x12fad5c9;
	uint32_t m2 = (tmp >> 32) ^ tmp;
	return m2 & 0x7FFFFFFF; //make it always positive.

	//;
}

typedef int64_t FixedPoint;

#define RAND_MAX 0x7FFFFFFF

#define FIXED_POINT 16 // fixedvalue = realvalue * 2^FIXED_POINT

#define FP_TO_INT(FixedPoint) ((int)((FixedPoint) >> FIXED_POINT))
#define INT_TO_FP(Int) ((FixedPoint)(Int) << FIXED_POINT)

#define MUL_FP_FP(Fp1, Fp2) (((Fp1) * (Fp2)) >> FIXED_POINT)

// Gets a random representing between 0. and 1. in fixed point.
__no_prof __no_san static inline FixedPoint RandFP()
{
	return Rand() % (1 << FIXED_POINT);
}
__no_prof __no_san static inline FixedPoint RandFPSign()
{
	if (Rand() % 2)
		return -RandFP();
	return RandFP();
}

#include "sintab.h"

__no_prof __no_san static inline FixedPoint Sin(int Angle)
{
	return INT_TO_FP(SinTable[Angle % 65536]) / 32768;
}

__no_prof __no_san static inline FixedPoint Cos(int Angle)
{
	return Sin(Angle + 16384);
}

// ####### FIREWORK IMPLEMENTATION #######

typedef struct _FIREWORK_DATA {
	int m_x, m_y;
	uint32_t m_color;
	FixedPoint m_actX, m_actY; // fixed point
	FixedPoint m_velX, m_velY; // fixed point
	int m_explosionRange;
} FIREWORK_DATA, *PFIREWORK_DATA;

__no_prof __no_san static inline uint32_t GetRandomColor()
{
	return (Rand() + 0x808080) & 0xFFFFFF;
}

void SpawnParticle(PFIREWORK_DATA Data);

#define DELTA_SEC Delay / 1000

void PerformDelay(int MS, UNUSED void *a)
{
	ksleep(MSTIME(MS));
}

NO_RETURN void KeTerminateThread(UNUSED int code)
{
	sched_exit_self();
}

NO_RETURN void T_Particle(void *Parameter)
{
	PFIREWORK_DATA ParentData = Parameter;
	FIREWORK_DATA Data;
	memset(&Data, 0, sizeof Data);

	// Inherit position information
	Data.m_x = ParentData->m_x;
	Data.m_y = ParentData->m_y;
	Data.m_actX = ParentData->m_actX;
	Data.m_actY = ParentData->m_actY;
	int ExplosionRange = ParentData->m_explosionRange;

	kfree(ParentData, sizeof(FIREWORK_DATA));

	int Angle = Rand() % 65536;
	Data.m_velX = MUL_FP_FP(Cos(Angle), RandFPSign()) * ExplosionRange;
	Data.m_velY = MUL_FP_FP(Sin(Angle), RandFPSign()) * ExplosionRange;

	int ExpireIn = 2000 + Rand() % 1000;
	Data.m_color = GetRandomColor();

	int T = 0;
	for (int i = 0; i < ExpireIn;) {
		PlotPixel(Data.m_color, Data.m_x, Data.m_y);

		int Delay = 16 + (T != 0);
		PerformDelay(Delay, NULL);
		i += Delay;
		T++;
		if (T == 3)
			T = 0;

		PlotPixel(BACKGROUND_COLOR, Data.m_x, Data.m_y);

		// Update the particle
		Data.m_actX += Data.m_velX * DELTA_SEC;
		Data.m_actY += Data.m_velY * DELTA_SEC;

		Data.m_x = FP_TO_INT(Data.m_actX);
		Data.m_y = FP_TO_INT(Data.m_actY);

		// Gravity
		Data.m_velY += INT_TO_FP(10) * DELTA_SEC;
	}

	// Done!
	KeTerminateThread(0);
}

typedef struct timer KTIMER;
#define KeInitializeTimer(tptr) timer_init(tptr)

NO_RETURN void T_Explodeable(UNUSED void *Parameter)
{
	KTIMER Timer;
	KeInitializeTimer(&Timer);

	FIREWORK_DATA Data;
	memset(&Data, 0, sizeof Data);

	int OffsetX = PixWidth * 400 / 1024;

	// This is a fire, so it doesn't have a base.
	Data.m_x = PixWidth / 2;
	Data.m_y = PixHeight - 1;
	Data.m_actX = INT_TO_FP(Data.m_x);
	Data.m_actY = INT_TO_FP(Data.m_y);
	Data.m_velY = -INT_TO_FP(400 + Rand() % 400);
	Data.m_velX = OffsetX * RandFPSign();
	Data.m_color = GetRandomColor();
	Data.m_explosionRange = Rand() % 100 + 100;

	int ExpireIn = 500 + Rand() % 500;
	int T = 0;
	for (int i = 0; i < ExpireIn;) {
		PlotPixel(Data.m_color, Data.m_x, Data.m_y);

		int Delay = 16 + (T != 0);
		PerformDelay(Delay, NULL);
		i += Delay;
		T++;
		if (T == 3)
			T = 0;

		PlotPixel(BACKGROUND_COLOR, Data.m_x, Data.m_y);

		// Update the particle
		Data.m_actX += Data.m_velX * DELTA_SEC;
		Data.m_actY += Data.m_velY * DELTA_SEC;

		Data.m_x = FP_TO_INT(Data.m_actX);
		Data.m_y = FP_TO_INT(Data.m_actY);

		// Gravity
		Data.m_velY += INT_TO_FP(10) * DELTA_SEC;
	}

	// Explode it!
	// This spawns many, many threads! Cause why not, right?!
	int PartCount = Rand() % 100 + 100;

	for (int i = 0; i < PartCount; i++) {
		PFIREWORK_DATA DataClone = kmalloc(sizeof(FIREWORK_DATA));
		if (!DataClone)
			break;

		*DataClone = Data;
		SpawnParticle(DataClone);
	}

	KeTerminateThread(0);
}

void SpawnParticle(PFIREWORK_DATA Data)
{
	kernel_thread_create("particle", SCHED_PRIO_TIME_SHARE, T_Particle,
			     Data, 1, NULL);
}

void SpawnExplodeable()
{
	kernel_thread_create("particle", SCHED_PRIO_TIME_SHARE, T_Explodeable,
			     NULL, 1, NULL);
}

// ####### MAIN PROGRAM #######

void PerformFireworksTest()
{
	uint64_t end_time = uptime() + BIGTIME(1, 1, 1, 1);

	Init();
	g_randGen ^= RandTscBased();

	FillScreen(BACKGROUND_COLOR);

	// The main thread occupies itself with spawning explodeables
	// from time to time, to keep things interesting.
	KTIMER Timer;
	KeInitializeTimer(&Timer);

	while (1) {
		if (uptime() > end_time)
			break;
		for (int i = 0; i < 3; i++) {
			int SpawnCount = Rand() % 2 + 1;

			for (int i = 0; i < SpawnCount; i++) {
				SpawnExplodeable();
			}

			PerformDelay(3000 + Rand() % 2000, NULL);
			//PerformDelay(400 + Rand() % 2000, NULL);
		}
		//PerformDelay(500 + Rand() % 5000, NULL);
	}

	sched_exit_self();
}
