# Contributing to KolibriARM

First: thank you for considering contributing. KolibriARM is built on the idea that an OS can be understood by a single person — and that only works if the code stays clean, minimal, and well-documented.

---

## Before You Start

Read these documents first:

1. [README.md](../README.md) — project overview and philosophy
2. [docs/ARCHITECTURE.md](ARCHITECTURE.md) — how the system is structured
3. [ROADMAP.md](../ROADMAP.md) — what's planned and in what order

If you're new to OS development, the [OSDev Wiki](https://wiki.osdev.org) and the [ARM Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487) are the two most important external references.

---

## What We Need Help With

Check the issue tracker for open issues tagged:

- `good first issue` — well-scoped tasks, good starting points
- `driver` — hardware driver work
- `kernel` — core kernel changes
- `docs` — documentation improvements
- `testing` — QEMU test cases and verification

**Things we always need:**
- Bug reports with minimal reproduction steps
- Documentation fixes (typos, unclear explanations, missing info)
- Test cases (QEMU-reproducible scenarios)
- Driver implementations (if you have hardware knowledge)

---

## Development Setup

See the [README](../README.md#building) for the full setup. Short version:

```bash
# WSL2 / Ubuntu
sudo apt install -y qemu-system-arm gcc-aarch64-linux-gnu \
                    binutils-aarch64-linux-gnu gdb-multiarch make

git clone https://github.com/yourname/kolibriarm
cd kolibriarm
make
make qemu
```

---

## Code Standards

### Language

- Kernel core: **C11** only. No C++.
- Boot and context switch: **AArch64 ASM** (GNU assembler syntax, `.S` files).
- No use of libc headers anywhere. Use `kernel/lib/` for string ops, memset, etc.

### Style

```c
// Functions: snake_case
void pmm_init(uint64_t base, uint64_t size);

// Types: snake_case with _t suffix
typedef struct process process_t;

// Constants and macros: UPPER_SNAKE_CASE
#define PAGE_SIZE  4096
#define MAX_PROCS  256

// Global mutable state: g_ prefix
static uint32_t g_proc_count = 0;

// No typedef for structs unless it adds clarity
struct process { ... };          // OK
typedef struct process process_t;  // also OK, use consistently
```

### Formatting

- 4-space indentation. No tabs.
- Opening brace on the same line: `if (x) {`
- Maximum line length: 100 characters
- Every public function in a `.h` file gets a doc comment:

```c
/**
 * pmm_alloc_page - Allocate a single 4KB physical page frame.
 *
 * Returns the physical address of the allocated frame,
 * or 0 if no memory is available.
 */
uint64_t pmm_alloc_page(void);
```

### Assembly

- Use `.S` (uppercase) so the C preprocessor runs on it
- Comment every non-obvious instruction
- Follow AAPCS64: `x0`–`x7` args, `x0` return, `x8`–`x15` caller-saved, `x19`–`x28` callee-saved
- Save/restore callee-saved registers if your function uses them

### Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
type(scope): short description (max 72 chars)

Optional longer body explaining WHY, not what.
```

Types:
- `feat` — new feature
- `fix` — bug fix
- `docs` — documentation only
- `refactor` — code restructuring, no behavior change
- `test` — adding or fixing tests
- `chore` — build system, toolchain, CI

Examples:
```
feat(mm): add bitmap physical memory allocator

fix(uart): handle TX FIFO full condition in putc

docs(arch): clarify page table layout for user space

feat(sched): implement round-robin preemptive scheduler
```

---

## Pull Request Process

1. **Fork** the repository and create a branch: `git checkout -b feat/your-feature`
2. **Write your code** following the standards above
3. **Test in QEMU**: your change must boot cleanly with `make qemu`
4. **Document** any new public APIs in the relevant `.h` file
5. **Open a PR** with:
   - A clear title following the commit convention
   - A description of what you changed and why
   - How you tested it (QEMU command, expected output)
   - Any open questions or known limitations

### PR Checklist

- [ ] Code compiles with `make` without warnings
- [ ] `make qemu` boots and produces expected output
- [ ] No libc headers included
- [ ] New public functions have doc comments
- [ ] Commit messages follow the convention
- [ ] No unrelated changes in the PR

---

## What We Won't Accept

- C++ in the kernel
- POSIX compatibility layers
- Pulling in external libraries without a strong reason and explicit discussion
- Code that requires Linux-specific features to compile
- "Temporary" hacks without a corresponding issue tracking the cleanup
- Changes that increase kernel binary size significantly without a proportional feature gain

---

## Communication

- **Issues**: for bugs, feature requests, and tasks
- **Discussions**: for design questions and open-ended conversation
- Keep communication in English (for now — the codebase is English, the docs are English)

---

## License

By contributing, you agree that your code will be licensed under [GPL-2.0](../LICENSE).
