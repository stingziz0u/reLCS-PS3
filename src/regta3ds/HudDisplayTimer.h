#pragma once

// Unclipped, pause-aware elapsed time is local to the HUD. Never change CTimer's step:
// physics, scripts and mission countdowns must continue to use the logic clock.
namespace HudDisplayTimer
{
#ifdef _3DS
class Clock
{
	bool valid = false;
	unsigned lastFrame = 0, lastTime = 0, elapsed = 0;

public:
	unsigned Sample(unsigned frame, unsigned now, unsigned fallback, bool paused)
	{
		if(valid && frame == lastFrame && now == lastTime) return elapsed;
		const unsigned delta = now - lastTime;
		// A loaded save/reset or a long interval without HUD rendering starts a
		// fresh baseline. Ordinary skipped frames retain their full game time.
		elapsed = paused ? 0 : (valid && delta <= 1000 ? delta : fallback);
		lastFrame = frame;
		lastTime = now;
		valid = true;
		return elapsed;
	}
};
// Simulation clamps each long logic frame to 60ms. Display lifetimes must
// include the remainder too, or stereo load stretches the original duration.
inline unsigned
Sample()
{
	static Clock clock;
	return clock.Sample(CTimer::GetFrameCounter(), CTimer::GetTimeInMillisecondsNonClipped(), CTimer::GetTimeStepInMilliseconds(), CTimer::GetIsPaused());
}
inline unsigned
GetTimeStepInMilliseconds()
{
	return Sample();
}
inline float
GetTimeStep()
{
	return Sample() * 0.05f;
}
#else
inline void
Sample()
{
}
inline unsigned
GetTimeStepInMilliseconds()
{
	return CTimer::GetTimeStepInMilliseconds();
}
inline float
GetTimeStep()
{
	return CTimer::GetTimeStep();
}
#endif
} // namespace HudDisplayTimer
