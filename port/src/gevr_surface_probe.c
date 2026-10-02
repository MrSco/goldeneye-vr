#include "gevr_surface_probe.h"
#include "fs.h"
#include "system.h"

int gevrSurfaceProbeEnabled(void)
{
    static u64 nextCheck;
    static int enabled;
    u64 now = sysGetMicroseconds();
    if (now >= nextCheck)
    {
        FILE *f = fopen(fsFullPath("$S/gevr_surfaceprobe.txt"), "r");
        int nextEnabled = f != NULL;
        if (f != NULL) fclose(f);
        nextCheck = now + 1000000;
        if (nextEnabled != enabled)
        {
            enabled = nextEnabled;
            sysLogPrintf(LOG_NOTE, "surface30: capture %s", enabled ? "enabled" : "disabled");
        }
    }
    return enabled;
}
