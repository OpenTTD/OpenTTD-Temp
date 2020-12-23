#include <unistd.h>

static int
hydro_random_init(void)
{
    if (getentropy(hydro_random_context.state, sizeof hydro_random_context.state) != 0) {
        return -1;
    }
    hydro_random_context.counter = ~LOAD64_LE(hydro_random_context.state);

    return 0;
}
