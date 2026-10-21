#pragma once

namespace EffectBudget3DS
{
// Runtime admission budgets only. Keep storage, cleanup and existing effects
// intact when switching modes; never use this for gameplay pools.
inline int
Limit(int capacity)
{
#ifdef _3DS
	if(rw::c3d::stereoControlsActive()) return capacity > 1 ? capacity / 2 : capacity;
#endif
	return capacity;
}
} // namespace EffectBudget3DS
