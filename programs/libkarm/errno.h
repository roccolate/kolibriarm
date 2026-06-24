// libkarm/errno.h
//
// Named error codes returned by KolibriARM syscalls. These mirror the
// frozen syscall-layer error table in kernel/syscall.c. The kernel
// does not return KLI_NOMEM today (syscall-level allocation failures
// use KLI_INVAL or KLI_NOENT), but the constant is reserved for
// future use and matches the value documented in SYSCALLS.md.
//
// `kli_isok` and `kli_again` exist because several syscalls
// (non-blocking read, IPC recv/send, window event poll) return
// ERR_AGAIN as a normal control-flow value, not a hard error.
// Wrappers and helpers must distinguish "try again" from "give up".

#ifndef KOLIBRIARM_PROGRAMS_LIBKARM_ERRNO_H
#define KOLIBRIARM_PROGRAMS_LIBKARM_ERRNO_H

#define KLI_NOENT  (-3L)
#define KLI_NOMEM  (-2L)
#define KLI_BADF   (-5L)
#define KLI_INVAL  (-7L)
#define KLI_AGAIN  (-11L)

static inline int kli_isok(long ret) {
    return ret >= 0;
}

static inline int kli_again(long ret) {
    return ret == KLI_AGAIN;
}

#endif
