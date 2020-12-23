// Note: All particle platforms except for the Spark Core have a HW RNG.  Only allow building on
// supported platforms for now. PLATFORM_ID definitions:
// https://github.com/particle-iot/device-os/blob/mesh-develop/hal/shared/platforms.h

#include "Particle.h"

static int
hydro_random_init(void)
{
    const char       ctx[hydro_hash_CONTEXTBYTES] = { 'h', 'y', 'd', 'r', 'o', 'P', 'R', 'G' };
    hydro_hash_state st;
    uint16_t         ebits = 0;

    hydro_hash_init(&st, ctx, NULL);

    while (ebits < 256) {
        uint32_t r = HAL_RNG_GetRandomNumber();
        hydro_hash_update(&st, (const uint32_t *) &r, sizeof r);
        ebits += 32;
    }

    hydro_hash_final(&st, hydro_random_context.state, sizeof hydro_random_context.state);
    hydro_random_context.counter = ~LOAD64_LE(hydro_random_context.state);

    return 0;
}
