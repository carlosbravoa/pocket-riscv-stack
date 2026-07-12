/* console shim: picolibc has no sys/uio.h; Wolf4SDL includes it but the
 * pakfs-backed FILE layer never calls readv/writev. */
#ifndef RVSTACK_SYS_UIO_H
#define RVSTACK_SYS_UIO_H
#include <stddef.h>
struct iovec { void *iov_base; size_t iov_len; };
#endif
