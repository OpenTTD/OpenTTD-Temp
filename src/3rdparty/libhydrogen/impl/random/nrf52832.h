// Important: The SoftDevice *must* be activated to enable reading from the RNG
// http://infocenter.nordicsemi.com/index.jsp?topic=%2Fcom.nordic.infocenter.nrf52832.ps.v1.1%2Frng.html

#include <nrf_soc.h>

static int
hydro_random_init(void)
{
    const char       ctx[hydro_hash_CONTEXTBYTES] = { 'h', 'y', 'd', 'r', 'o', 'P', 'R', 'G' };
    hydro_hash_state st;
    const uint8_t    total_bytes     = 32;
    uint8_t          remaining_bytes = total_bytes;
    uint8_t          available_bytes;
    uint8_t          rand_buffer[32];

    hydro_hash_init(&st, ctx, NULL);

    for (;;) {
        if (sd_rand_application_bytes_available_get(&available_bytes) != NRF_SUCCESS) {
            return -1;
        }
        if (available_bytes > 0) {
            if (available_bytes > remaining_bytes) {
                available_bytes = remaining_bytes;
            }
            if (sd_rand_application_vector_get(rand_buffer, available_bytes) != NRF_SUCCESS) {
                return -1;
            }
            hydro_hash_update(&st, rand_buffer, total_bytes);
            remaining_bytes -= available_bytes;
        }
        if (remaining_bytes <= 0) {
            break;
        }
        delay(10);
    }
    hydro_hash_final(&st, hydro_random_context.state, sizeof hydro_random_context.state);
    hydro_random_context.counter = ~LOAD64_LE(hydro_random_context.state);

    return 0;
}
