static TLS struct {
    _hydro_attr_aligned_(16) uint8_t state[gimli_BLOCKBYTES];
    uint64_t counter;
    uint8_t  initialized;
    uint8_t  available;
} hydro_random_context;

#if defined(AVR) && !defined(__unix__)
# include "random/avr.h"
#elif (defined(ESP32) || defined(ESP8266)) && !defined(__unix__)
# include "random/esp32.h"
#elif defined(PARTICLE) && defined(PLATFORM_ID) && PLATFORM_ID > 2 && !defined(__unix__)
# include "random/particle.h"
#elif (defined(NRF52832_XXAA) || defined(NRF52832_XXAB)) && !defined(__unix__)
# include "random/nrf52832.h"
#elif defined(_WIN32)
# include "random/windows.h"
#elif defined(__wasi__)
# include "random/wasi.h"
#elif defined(__unix__)
# include "random/unix.h"
#elif defined(TARGET_LIKE_MBED)
# include "random/mbed.h"
#elif defined(RIOT_VERSION)
# include "random/riot.h"
#else
#error Unsupported platform
#endif

static void
hydro_random_check_initialized(void)
{
    if (hydro_random_context.initialized == 0) {
        if (hydro_random_init() != 0) {
            abort();
        }
        gimli_core_u8(hydro_random_context.state, 0);
        hydro_random_ratchet();
        hydro_random_context.initialized = 1;
    }
}

void
hydro_random_ratchet(void)
{
    mem_zero(hydro_random_context.state, gimli_RATE);
    STORE64_LE(hydro_random_context.state, hydro_random_context.counter);
    hydro_random_context.counter++;
    gimli_core_u8(hydro_random_context.state, 0);
    hydro_random_context.available = gimli_RATE;
}

uint32_t
hydro_random_u32(void)
{
    uint32_t v;

    hydro_random_check_initialized();
    if (hydro_random_context.available < 4) {
        hydro_random_ratchet();
    }
    memcpy(&v, &hydro_random_context.state[gimli_RATE - hydro_random_context.available], 4);
    hydro_random_context.available -= 4;

    return v;
}

uint32_t
hydro_random_uniform(const uint32_t upper_bound)
{
    uint32_t min;
    uint32_t r;

    if (upper_bound < 2U) {
        return 0;
    }
    min = (1U + ~upper_bound) % upper_bound; /* = 2**32 mod upper_bound */
    do {
        r = hydro_random_u32();
    } while (r < min);
    /* r is now clamped to a set whose size mod upper_bound == 0
     * the worst case (2**31+1) requires 2 attempts on average */

    return r % upper_bound;
}

void
hydro_random_buf(void *out, size_t out_len)
{
    uint8_t *p = (uint8_t *) out;
    size_t   i;
    size_t   leftover;

    hydro_random_check_initialized();
    for (i = 0; i < out_len / gimli_RATE; i++) {
        gimli_core_u8(hydro_random_context.state, 0);
        memcpy(p + i * gimli_RATE, hydro_random_context.state, gimli_RATE);
    }
    leftover = out_len % gimli_RATE;
    if (leftover != 0) {
        gimli_core_u8(hydro_random_context.state, 0);
        mem_cpy(p + i * gimli_RATE, hydro_random_context.state, leftover);
    }
    hydro_random_ratchet();
}

void
hydro_random_buf_deterministic(void *out, size_t out_len,
                               const uint8_t seed[hydro_random_SEEDBYTES])
{
    static const uint8_t             prefix[] = { 7, 'd', 'r', 'b', 'g', '2', '5', '6' };
    _hydro_attr_aligned_(16) uint8_t state[gimli_BLOCKBYTES];
    uint8_t *                        p = (uint8_t *) out;
    size_t                           i;
    size_t                           leftover;

    mem_zero(state, gimli_BLOCKBYTES);
    COMPILER_ASSERT(sizeof prefix + 8 <= gimli_RATE);
    memcpy(state, prefix, sizeof prefix);
    STORE64_LE(state + sizeof prefix, (uint64_t) out_len);
    gimli_core_u8(state, 1);
    COMPILER_ASSERT(hydro_random_SEEDBYTES == gimli_RATE * 2);
    mem_xor(state, seed, gimli_RATE);
    gimli_core_u8(state, 2);
    mem_xor(state, seed + gimli_RATE, gimli_RATE);
    gimli_core_u8(state, 2);
    for (i = 0; i < out_len / gimli_RATE; i++) {
        gimli_core_u8(state, 0);
        memcpy(p + i * gimli_RATE, state, gimli_RATE);
    }
    leftover = out_len % gimli_RATE;
    if (leftover != 0) {
        gimli_core_u8(state, 0);
        mem_cpy(p + i * gimli_RATE, state, leftover);
    }
}

void
hydro_random_reseed(void)
{
    hydro_random_context.initialized = 0;
    hydro_random_check_initialized();
}
