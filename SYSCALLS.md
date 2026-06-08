# Syscall Reference

KolibriARM defines its own syscall ABI. There is no POSIX compatibility.

## Calling Convention

```
Instruction:  svc #0
Syscall #:    x8
Arguments:    x0, x1, x2, x3, x4, x5  (up to 6)
Return value: x0  (negative = error code)
```

All registers except x0 are preserved across a syscall.

---

## Process

| # | Name | Args | Returns | Description |
|---|------|------|---------|-------------|
| 1 | `sys_exit` | `x0=code` | — | Terminate current process |
| 2 | `sys_yield` | — | — | Voluntarily yield CPU slice |
| 3 | `sys_getpid` | — | PID | Return current process ID |
| 4 | `sys_fork` | — | PID / 0 | Clone current process |
| 5 | `sys_exec` | `x0=path_ptr, x1=argv_ptr` | — | Replace process image |
| 6 | `sys_wait` | `x0=pid` | exit code | Wait for child process |
| 7 | `sys_kill` | `x0=pid, x1=signal` | 0/-1 | Send signal to process |
| 8 | `sys_sleep` | `x0=ms` | — | Sleep for N milliseconds |

---

## Memory

| # | Name | Args | Returns | Description |
|---|------|------|---------|-------------|
| 20 | `sys_mmap` | `x0=hint, x1=size, x2=flags` | vaddr | Map anonymous memory |
| 21 | `sys_munmap` | `x0=vaddr, x1=size` | 0/-1 | Unmap memory region |
| 22 | `sys_mprotect` | `x0=vaddr, x1=size, x2=prot` | 0/-1 | Change page protection |

**mmap flags:**
```
0x01  PROT_READ
0x02  PROT_WRITE
0x04  PROT_EXEC
0x10  MAP_SHARED    (with IPC partner)
0x20  MAP_FIXED     (use hint exactly)
```

---

## I/O and Files

| # | Name | Args | Returns | Description |
|---|------|------|---------|-------------|
| 40 | `sys_open` | `x0=path, x1=flags` | fd / -1 | Open file |
| 41 | `sys_close` | `x0=fd` | 0/-1 | Close file descriptor |
| 42 | `sys_read` | `x0=fd, x1=buf, x2=len` | bytes read | Read from file |
| 43 | `sys_write` | `x0=fd, x1=buf, x2=len` | bytes written | Write to file |
| 44 | `sys_seek` | `x0=fd, x1=offset, x2=whence` | new pos | Seek within file |
| 45 | `sys_stat` | `x0=path, x1=stat_ptr` | 0/-1 | Get file metadata |
| 46 | `sys_readdir` | `x0=path, x1=buf, x2=len` | count | List directory |
| 47 | `sys_mkdir` | `x0=path` | 0/-1 | Create directory |
| 48 | `sys_unlink` | `x0=path` | 0/-1 | Delete file |
| 49 | `sys_rename` | `x0=old, x1=new` | 0/-1 | Rename/move file |

**Standard file descriptors:**
```
0  stdin   (keyboard)
1  stdout  (display / UART in debug mode)
2  stderr  (UART always)
```

**open flags:**
```
0x00  O_RDONLY
0x01  O_WRONLY
0x02  O_RDWR
0x40  O_CREAT
0x200 O_TRUNC
0x400 O_APPEND
```

---

## GUI / Window System

| # | Name | Args | Returns | Description |
|---|------|------|---------|-------------|
| 60 | `sys_window_create` | `x0=x, x1=y, x2=w, x3=h, x4=title_ptr` | wid | Create a window |
| 61 | `sys_window_destroy` | `x0=wid` | 0/-1 | Destroy window |
| 62 | `sys_window_move` | `x0=wid, x1=x, x2=y` | 0/-1 | Move window |
| 63 | `sys_window_resize` | `x0=wid, x1=w, x2=h` | 0/-1 | Resize window |
| 64 | `sys_window_flush` | `x0=wid, x1=dirty_rect_ptr` | — | Flush to screen |
| 65 | `sys_window_get_buf` | `x0=wid` | vaddr | Get drawing buffer address |
| 66 | `sys_event_poll` | `x0=event_ptr` | 0/1 | Poll next input event |
| 67 | `sys_event_wait` | `x0=event_ptr` | — | Block until event |

**Event structure:**
```c
typedef struct {
    uint32_t type;      // EVENT_KEY, EVENT_MOUSE, EVENT_RESIZE, EVENT_CLOSE
    uint32_t wid;       // target window
    union {
        struct { uint32_t keycode; uint32_t modifiers; } key;
        struct { int32_t x, y; uint32_t buttons; } mouse;
        struct { uint32_t w, h; } resize;
    };
} event_t;
```

---

## IPC

| # | Name | Args | Returns | Description |
|---|------|------|---------|-------------|
| 80 | `sys_msg_send` | `x0=pid, x1=buf, x2=len` | 0/-1 | Send message to process |
| 81 | `sys_msg_recv` | `x0=buf, x1=maxlen` | len/-1 | Receive next message |
| 82 | `sys_shm_create` | `x0=size` | shmid | Create shared memory region |
| 83 | `sys_shm_map` | `x0=shmid` | vaddr | Map shared region into this process |
| 84 | `sys_shm_unmap` | `x0=shmid` | 0/-1 | Unmap shared region |
| 85 | `sys_shm_destroy` | `x0=shmid` | 0/-1 | Destroy shared region |

---

## System Info

| # | Name | Args | Returns | Description |
|---|------|------|---------|-------------|
| 100 | `sys_uptime` | — | ms | Milliseconds since boot |
| 101 | `sys_meminfo` | `x0=info_ptr` | — | Fill memory stats struct |
| 102 | `sys_cpuinfo` | `x0=info_ptr` | — | Fill CPU info struct |
| 103 | `sys_proclist` | `x0=buf, x1=maxcount` | count | List running processes |

---

## Error Codes

| Code | Name | Meaning |
|------|------|---------|
| -1 | `ERR_GENERIC` | Unspecified error |
| -2 | `ERR_NOMEM` | Out of memory |
| -3 | `ERR_NOENT` | File or resource not found |
| -4 | `ERR_PERM` | Permission denied |
| -5 | `ERR_BADF` | Bad file descriptor |
| -6 | `ERR_BUSY` | Resource busy |
| -7 | `ERR_INVAL` | Invalid argument |
| -8 | `ERR_OVERFLOW` | Buffer or value overflow |
| -9 | `ERR_TIMEOUT` | Operation timed out |
| -10 | `ERR_EXIST` | Resource already exists |

---

## Usage Example (C, userland)

```c
// Write "hello" to stdout using sys_write
static inline long syscall(long n, long a0, long a1, long a2) {
    register long x8 asm("x8") = n;
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    asm volatile("svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2)
        : "memory");
    return x0;
}

void _start(void) {
    const char msg[] = "hello from userland\n";
    syscall(43, 1, (long)msg, sizeof(msg) - 1);  // sys_write(stdout, msg, len)
    syscall(1, 0, 0, 0);                           // sys_exit(0)
}
```
