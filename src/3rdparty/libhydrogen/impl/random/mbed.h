#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>

#if defined(MBEDTLS_ENTROPY_C)

static int
hydro_random_init(void)
{
    mbedtls_entropy_context entropy;
    uint16_t                pos = 0;

    mbedtls_entropy_init(&entropy);

    // Pull data directly out of the entropy pool for the state, as it's small enough.
    if (mbedtls_entropy_func(&entropy, (uint8_t *) &hydro_random_context.counter,
                             sizeof hydro_random_context.counter) != 0) {
        return -1;
    }
    // mbedtls_entropy_func can't provide more than MBEDTLS_ENTROPY_BLOCK_SIZE in one go.
    // This constant depends of mbedTLS configuration (whether the PRNG is backed by SHA256/SHA512
    // at this time) Therefore, if necessary, we get entropy multiple times.

    do {
        const uint8_t dataLeftToConsume = gimli_BLOCKBYTES - pos;
        const uint8_t currentChunkSize  = (dataLeftToConsume > MBEDTLS_ENTROPY_BLOCK_SIZE)
                                             ? MBEDTLS_ENTROPY_BLOCK_SIZE
                                             : dataLeftToConsume;

        // Forces mbedTLS to fetch fresh entropy, then get some to feed libhydrogen.
        if (mbedtls_entropy_gather(&entropy) != 0 ||
            mbedtls_entropy_func(&entropy, &hydro_random_context.state[pos], currentChunkSize) !=
                0) {
            return -1;
        }
        pos += MBEDTLS_ENTROPY_BLOCK_SIZE;
    } while (pos < gimli_BLOCKBYTES);

    mbedtls_entropy_free(&entropy);

    return 0;
}
#else
# error Need an entropy source
#endif
