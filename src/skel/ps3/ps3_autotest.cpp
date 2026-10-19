// ps3_autotest.cpp -- the automatic test of reVC-PS3 (USRDIR/autotest.txt)
// walks Vice City: its missions, waypoints and levels. Not ported to Liberty
// City Stories yet; these are the entry points the rest of the port calls.

#include "common.h"
#include "ps3_platform.h"

void PS3_AutotestFrame(void) {}
int  PS3_AutotestWantsLoad(void) { return 0; }
void PS3_AutotestLoaded(void) {}
