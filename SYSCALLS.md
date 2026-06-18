# Syscall Reference

KolibriARM defines its own syscall ABI. There is no POSIX compatibility.

This document separates the syscalls implemented by the current kernel from
numbers reserved for the planned ABI.

## Calling Convention

```
Instruction:  svc #0
Syscall #:    x8
Arguments:    x0, x1, x2, x3, x4, x5  (up to 6)
Return value: x0  (negative = error code)
```

For a syscall that returns to the same process, all registers except `x0` are
preserved. `x0` carries the return value.

---

## Implemented Now

These numbers are handled by `kernel/syscall.c` today. Unknown syscall numbers
return `ERR_INVAL`.

### Process

| # | Name | Args | Returns | Description |
|---|------|------|---------|-------------|
| 1 | `sys_exit` | `x0=code` | — | Terminate current process |
| 2 | `sys_yield` | — | — | Voluntarily yield CPU slice |
| 3 | `sys_getpid` | — | PID | Return current process ID |
| 4 | `sys_spawn` | `x0=path_ptr, x1=entry_index` | PID / error | Spawn a flat image entry from VFS |
| 6 | `sys_wait` | `x0=pid` | exit code / error | Reclaim an exited process |
| 7 | `sys_kill` | `x0=pid` | 0 / error | Terminate another process |

Notes:
- `sys_exit` marks the process as exited and switches to the next runnable EL0
  process when one exists.
- `sys_yield` switches to the next runnable EL0 process when one exists.
- `sys_spawn` is the first loader hook exposed to EL0. It currently loads a
  flat image through VFS, starts the selected entry as a READY process, and
  reclaims exited demo processes before allocating a slot.
- `sys_wait` is non-blocking today: it succeeds only when the target process is
  already `ZOMBIE`; otherwise it returns `ERR_AGAIN`.
- `sys_kill` marks another process exited with code `0x80`. It currently
  rejects killing the calling process and already-exited processes.

### Memory

| # | Name | Args | Returns | Description |
|---|------|------|---------|-------------|
| 20 | `sys_mmap` | `x0=hint, x1=size, x2=flags` | vaddr / error | Map anonymous user pages |
| 21 | `sys_munmap` | `x0=vaddr, x1=size` | 0 / error | Unmap owned anonymous user pages |

Current limitations:
- `sys_mmap` allocates contiguous physical pages, installs user PTEs in the
  current process page table, and records process-owned metadata.
- `hint` must be `0`.
- `flags=0` maps readable/writable anonymous memory.
- `PROT_READ`, `PROT_WRITE`, and `PROT_EXEC` are supported. `MAP_SHARED` and
  `MAP_FIXED` are reserved but rejected today.
- `sys_munmap` requires an exact owned `sys_mmap` region match. Image and stack
  regions cannot be unmapped through this syscall yet.

`mmap` protection/map flags:
```
0x01  PROT_READ
0x02  PROT_WRITE
0x04  PROT_EXEC
0x10  MAP_SHARED    reserved, not implemented
0x20  MAP_FIXED     reserved, not implemented
```

### I/O

| # | Name | Args | Returns | Description |
|---|------|------|---------|-------------|
| 40 | `sys_open` | `x0=path, x1=flags` | fd / error | Open a VFS file |
| 41 | `sys_close` | `x0=fd` | 0 / error | Close a VFS file descriptor |
| 42 | `sys_read` | `x0=fd, x1=buf, x2=len` | bytes read / error | Read from stdin or a VFS file |
| 43 | `sys_write` | `x0=fd, x1=buf, x2=len` | bytes written / error | Write to UART-backed stdout/stderr or a VFS file |
| 44 | `sys_seek` | `x0=fd, x1=offset, x2=whence` | new offset / error | Seek within a VFS file |
| 45 | `sys_stat` | `x0=path, x1=stat_ptr` | 0 / error | Get VFS file metadata |
| 46 | `sys_readdir` | `x0=path, x1=buf, x2=len` | bytes written / error | List mounted VFS paths or supported VFS directories |

Current file descriptors:
```
0  stdin   UART, non-blocking
1  stdout  UART
2  stderr  UART
3+ VFS files
```

Notes:
- `sys_read(0, buf, len)` reads at most one UART byte. It returns `ERR_AGAIN`
  when no byte is available.
- `sys_open` accepts `flags=0` (`O_RDONLY`), `flags=1` (`O_WRONLY`), and
  `flags=2` (`O_RDWR`).
- `sys_read`, `sys_write`, `sys_seek`, and `sys_close` on VFS file descriptors
  are backed by the fixed kernel VFS descriptor table.
- `sys_seek` currently supports only `whence=0` (`SEEK_SET`).
- `sys_stat` writes the current `vfs_stat_t` layout: one `uint64_t size`.
- `sys_readdir("/")` writes newline-separated mounted VFS paths. When FAT32 is
  present, `sys_readdir("/fat")` writes newline-separated root 8.3 directory
  entries.
- `sys_write` returns `ERR_BADF` for unsupported descriptors or descriptors
  not opened for writing.
- `sys_write` validates that `buf..buf+len` is inside the current process's
  registered user regions before reading from it.
- FAT32 writes are limited to existing root 8.3 files with already allocated
  cluster chains. The current driver can overwrite or grow within that chain
  and updates the directory entry size; it does not allocate new clusters.

### IPC

| # | Name | Args | Returns | Description |
|---|------|------|---------|-------------|
| 60 | `sys_ipc_send` | `x0=target_pid, x1=buf, x2=len` | bytes sent / error | Send one fixed-size queued message |
| 61 | `sys_ipc_recv` | `x0=buf, x1=capacity` | bytes received / error | Receive the next queued message for the caller |

Current limitations:
- Message size is capped at `IPC_MAX_MESSAGE_SIZE`.
- `sys_ipc_recv` requires `capacity == IPC_MAX_MESSAGE_SIZE`.
- The queue is fixed-size and non-blocking; empty/full paths return
  `ERR_AGAIN`.

### Window System

| # | Name | Args | Returns | Description |
|---|------|------|---------|-------------|
| 70 | `sys_window_create` | `x0=x, x1=y, x2=w, x3=h, x4=bg, x5=border, x6=title_ptr` | window id / error | Create a process-owned window |
| 71 | `sys_window_destroy` | `x0=window_id` | 0 / error | Destroy a window owned by the caller |
| 72 | `sys_window_draw_text` | `x0=window_id, x1=x, x2=y, x3=color, x4=str_ptr` | 0 / error | Draw text inside a window |
| 73 | `sys_window_draw_rect` | `x0=window_id, x1=x, x2=y, x3=w, x4=h, x5=color` | 0 / error | Draw a clipped filled rectangle inside a window |
| 74 | `sys_window_event` | `x0=window_id, x1=buf, x2=max_events` | event count / error | Read queued window events |
| 75 | `sys_window_set_title` | `x0=window_id, x1=title_ptr` | 0 / error | Replace a window title |
| 76 | `sys_window_redraw` | `x0=window_id` | 0 / error | Mark the demo GUI desktop dirty |

Current limitations:
- These syscalls are the early desktop ABI, not a stable long-term ABI.
- Window ownership is enforced with the current process pid.
- `sys_window_event` writes packed `gui_event_t` triples:
  `uint32_t type, int32_t data1, int32_t data2`.
- `sys_window_event` yields for a bounded number of scheduler turns and returns
  `ERR_AGAIN` when no event arrives.
- Drawing still writes through the current kernel framebuffer path; there is no
  per-window backing buffer or explicit flush rectangle yet.

### Error Codes Implemented Today

| Code | Name | Meaning |
|------|------|---------|
| -3 | `ERR_NOENT` | File or resource not found |
| -2 | `ERR_NOMEM` | Out of memory |
| -5 | `ERR_BADF` | Bad file descriptor |
| -7 | `ERR_INVAL` | Invalid argument |
| -11 | `ERR_AGAIN` | Try again later |

### System Info

| # | Name | Args | Returns | Description |
|---|------|------|---------|-------------|
| 100 | `sys_timeinfo` | `x0=info_ptr` | 0 / error | Fill timer/scheduler counters |
| 101 | `sys_meminfo` | `x0=info_ptr` | 0 / error | Fill memory page counters |
| 102 | `sys_proclist` | `x0=entries, x1=max_entries` | entry count / error | Fill a process snapshot |

`sys_timeinfo` writes three `uint64_t` values:
```
info[0]  ARM generic timer IRQ ticks
info[1]  scheduler timer ticks
info[2]  completed scheduler quantums
```

`sys_meminfo` writes two `uint64_t` values:
```
info[0]  total physical pages tracked by PMM
info[1]  free physical pages
```

`sys_proclist` writes up to `max_entries` fixed-size entries:
```c
typedef struct {
    uint32_t pid;
    uint32_t state;
    char name[16];
} syscall_proc_entry_t;
```

`max_entries` is currently capped at `PROCESS_MAX_PROCESSES`.

---

## Reserved / Planned Syscalls

The following numbers describe the intended ABI shape. They are not implemented
yet unless also listed in "Implemented Now".

### Planned Process

| # | Name | Args | Returns | Description |
|---|------|------|---------|-------------|
| 5 | `sys_exec` | `x0=path_ptr, x1=argv_ptr` | — | Replace process image |
| 8 | `sys_sleep` | `x0=ms` | 0 / error | Sleep for N milliseconds |
| 9 | `sys_fork` | — | PID / 0 | Clone current process |

### Planned Memory

| # | Name | Args | Returns | Description |
|---|------|------|---------|-------------|
| 22 | `sys_mprotect` | `x0=vaddr, x1=size, x2=prot` | 0 / error | Change page protection |

### Planned I/O and Files

| # | Name | Args | Returns | Description |
|---|------|------|---------|-------------|
| 47 | `sys_mkdir` | `x0=path` | 0 / error | Create directory |
| 48 | `sys_unlink` | `x0=path` | 0 / error | Delete file |
| 49 | `sys_rename` | `x0=old, x1=new` | 0 / error | Rename/move file |

Planned standard file descriptors:
```
0  stdin   keyboard or terminal input
1  stdout  terminal/display, UART in debug mode
2  stderr  terminal/display or UART debug channel
```

Planned `open` flags:
```
0x00  O_RDONLY
0x01  O_WRONLY
0x02  O_RDWR
0x40  O_CREAT
0x200 O_TRUNC
0x400 O_APPEND
```

### Planned GUI / Window System Extensions

The live GUI range starts at 70. Future GUI numbers should be assigned after
the current 70-76 ABI is cleaned up; do not move GUI calls into 60-69 because
that range is already used for IPC.

Planned but not implemented yet:

| Name | Description |
|------|-------------|
| `sys_window_get_bounds` | Read current window bounds |
| `sys_window_set_bounds` | Move and/or resize a window |
| `sys_window_show` / `sys_window_hide` | Toggle visibility |
| `sys_window_focus` | Request focus/raise |
| `sys_window_flush` | Flush an explicit dirty rectangle |
| `sys_draw_line` | Draw a clipped line in a window |
| `sys_draw_bitmap` | Blit a bitmap into a window |
| `sys_draw_get_text_metrics` | Measure text before drawing |
| `sys_event_poll` | Non-blocking event read with final event layout |
| `sys_event_wait` | Blocking event read with final event layout |

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

### Planned IPC

| # | Name | Args | Returns | Description |
|---|------|------|---------|-------------|
| 80 | `sys_msg_send` | `x0=pid, x1=buf, x2=len` | 0 / error | Send message to process |
| 81 | `sys_msg_recv` | `x0=buf, x1=maxlen` | len / error | Receive next message |
| 82 | `sys_shm_create` | `x0=size` | shmid | Create shared memory region |
| 83 | `sys_shm_map` | `x0=shmid` | vaddr | Map shared region into this process |
| 84 | `sys_shm_unmap` | `x0=shmid` | 0 / error | Unmap shared region |
| 85 | `sys_shm_destroy` | `x0=shmid` | 0 / error | Destroy shared region |

### Planned System Info Extensions

The live system-info numbers are already `100 sys_timeinfo`,
`101 sys_meminfo`, and `102 sys_proclist`. Future additions should avoid
renumbering those calls.

| Name | Description |
|------|-------------|
| `sys_uptime` | Return milliseconds since boot, if this remains distinct from `sys_timeinfo` |
| `sys_cpuinfo` | Fill CPU identification and feature information |

---

## Planned Error Codes

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
