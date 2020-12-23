#include <errno.h>
#include <fcntl.h>
#ifdef __linux__
#include <poll.h>
#endif
#include <sys/types.h>
#include <unistd.h>

#ifdef __linux__
static int
hydro_random_block_on_dev_random(void)
{
    struct pollfd pfd;
    int           fd;
    int           pret;

    fd = open("/dev/random", O_RDONLY);
    if (fd == -1) {
        return 0;
    }
    pfd.fd      = fd;
    pfd.events  = POLLIN;
    pfd.revents = 0;
    do {
        pret = poll(&pfd, 1, -1);
    } while (pret < 0 && (errno == EINTR || errno == EAGAIN));
    if (pret != 1) {
        (void) close(fd);
        errno = EIO;
        return -1;
    }
    return close(fd);
}
#endif

static ssize_t
hydro_random_safe_read(const int fd, void *const buf_, size_t len)
{
    unsigned char *buf = (unsigned char *) buf_;
    ssize_t        readnb;

    do {
        while ((readnb = read(fd, buf, len)) < (ssize_t) 0 && (errno == EINTR || errno == EAGAIN)) {
        }
        if (readnb < (ssize_t) 0) {
            return readnb;
        }
        if (readnb == (ssize_t) 0) {
            break;
        }
        len -= (size_t) readnb;
        buf += readnb;
    } while (len > (ssize_t) 0);

    return (ssize_t)(buf - (unsigned char *) buf_);
}

static int
hydro_random_init(void)
{
    uint8_t tmp[gimli_BLOCKBYTES + 8];
    int     fd;
    int     ret = -1;

#ifdef __linux__
    if (hydro_random_block_on_dev_random() != 0) {
        return -1;
    }
#endif
    do {
        fd = open("/dev/urandom", O_RDONLY);
        if (fd == -1 && errno != EINTR) {
            return -1;
        }
    } while (fd == -1);
    if (hydro_random_safe_read(fd, tmp, sizeof tmp) == (ssize_t) sizeof tmp) {
        memcpy(hydro_random_context.state, tmp, gimli_BLOCKBYTES);
        memcpy(&hydro_random_context.counter, tmp + gimli_BLOCKBYTES, 8);
        hydro_memzero(tmp, sizeof tmp);
        ret = 0;
    }
    ret |= close(fd);

    return ret;
}
