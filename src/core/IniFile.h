#pragma once

#define DEFAULT_MAX_NUMBER_OF_PEDS 25.0f
#define DEFAULT_MAX_NUMBER_OF_PEDS_INTERIOR 40.0f
#define DEFAULT_MAX_NUMBER_OF_CARS 30.0f
#ifdef __PS3__
// patch 33: the ped and car density of a rampage, whatever the sliders say
// (unless they are higher): half of their 3.4
#define PS3_FRENZY_DENSITY 1.7f
#endif

class CIniFile
{
public:
	static void LoadIniFile();

	static float PedNumberMultiplier;
	static float CarNumberMultiplier;
};
