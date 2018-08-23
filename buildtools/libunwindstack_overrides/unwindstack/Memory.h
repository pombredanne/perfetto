#ifndef BUILDTOOLS_LIBUNWINDSTACK_OVERRIDES_UNWINDSTACK_MEMORY_H_
#define BUILDTOOLS_LIBUNWINDSTACK_OVERRIDES_UNWINDSTACK_MEMORY_H_

#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>

#include "../../android-core/libunwindstack/include/unwindstack/Memory.h"

#if __ANDROID_API__ < 26
// Android API levels less than 26, which Perfetto builds as currently, do not
// expose this function. libunwindstack depends on it, but we do not
// use that functionality.
extern ssize_t process_vm_readv(pid_t pid,
                                const struct iovec* local_iov,
                                unsigned long liovcnt,
                                const struct iovec* remote_iov,
                                unsigned long riovcnt,
                                unsigned long flags);
#endif

#endif  // BUILDTOOLS_LIBUNWINDSTACK_OVERRIDES_UNWINDSTACK_MEMORY_H_
