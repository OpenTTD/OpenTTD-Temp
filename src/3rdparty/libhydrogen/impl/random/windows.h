#include <windows.h>
#define RtlGenRandom SystemFunction036
#if defined(__cplusplus)
extern "C"
#endif
    BOOLEAN NTAPI
    RtlGenRandom(PVOID RandomBuffer, ULONG RandomBufferLength);
#pragma comment(lib, "advapi32.lib")

static int
hydro_random_init(void)
{
    if (!RtlGenRandom((PVOID) hydro_random_context.state,
                      (ULONG) sizeof hydro_random_context.state)) {
        return -1;
    }
    hydro_random_context.counter = ~LOAD64_LE(hydro_random_context.state);

    return 0;
}
