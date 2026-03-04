# lean-pari

A working demonstration of calling [PARI/GP](https://pari.math.u-bordeaux.fr/) library functions from [Lean 4](https://lean-lang.org/) via the C FFI.

Targets **Lean v4.28.0** and **PARI 2.13.x**.

## What this project does

- Wraps PARI's `GEN` type as a Lean `opaque` type
- Exposes `pari_init`, `gp_read_str`, `GENtostr`, `ellinit`, and `ellap` through C FFI bindings
- Manages PARI's `avma` stack from Lean (mark / restore / gcopy)
- Demonstrates computing the Frobenius trace $a_p$ of an elliptic curve for a list of primes

## Prerequisites

| Tool | Version |
|---|---|
| Lean / Lake | v4.28.0 (via `elan`) |
| PARI/GP | 2.13.x |
| GCC | any recent version |

PARI headers and `libpari.so` must be installed. On Debian/Ubuntu:

```bash
sudo apt install libpari-dev
```

Or build from source and install to a custom prefix (e.g. `~/`).

## Project structure

```
lean-pari/
├── lean-toolchain          # pins Lean v4.28.0
├── lakefile.toml           # lake build config
├── Makefile                # compiles c/pari_lean.o, then runs lake build
├── c/
│   └── pari_lean.c         # C glue code between Lean and PARI
├── LeanPari/
│   ├── Types.lean          # opaque GEN type
│   ├── Basic.lean          # FFI bindings: init, stack, readStr, toStr
│   └── Elliptic.lean       # FFI bindings: ellinit, ellap
├── Main.lean               # demo entry point
└── docs/
    └── sample-implementation.md   # detailed implementation notes (Japanese)
```

## Build

### 1. Edit path settings in `Makefile`

```makefile
PARI_INCLUDE = /path/to/include   # parent of the pari/ directory
PARI_LIB     = /path/to/lib       # directory containing libpari.so
```

> **Important**: `PARI_INCLUDE` must be the **parent** of `pari/`, not `pari/` itself.
> For example, if the header is at `/home/user/include/pari/pari.h`, set `PARI_INCLUDE = /home/user/include`.

### 2. Edit `lakefile.toml`

```toml
moreLinkArgs = ["-lpari", "-lm", "-L/path/to/lib", "-Wl,-rpath,/path/to/lib", "c/pari_lean.o"]
```

Replace `/path/to/lib` with the directory that contains `libpari.so`.
The `-Wl,-rpath,...` flag embeds the runtime search path so `lake exec` works without setting `LD_LIBRARY_PATH`.

### 3. Build and run

```bash
make          # compiles c/pari_lean.o, then runs lake build
lake exec Main
```

Expected output:

```
ap(2) = 0
ap(3) = 0
ap(5) = -2
ap(7) = 0
ap(11) = 0
ap(13) = 6
ap(17) = 2
ap(19) = 0
ap(23) = 0
---
secp256k1: ap(2) = 0
secp256k1: ap(3) = 0
secp256k1: ap(5) = 0
secp256k1: ap(7) = 0
secp256k1: ap(11) = 0
secp256k1: ap(101) = 0
secp256k1: ap(1009) = -19
```

## Key design notes

### Do not use `initialize` for PARI init

`initialize initializePari` would run during Lean's **elaboration** of the module. At that point, `lean_pari_initialize` exists only in the final binary (`pari_lean.o`) and cannot be `dlopen`'d by the Lean interpreter — this causes an immediate crash.

Instead, `initializePari` is exposed as a plain `opaque` and must be called **explicitly at the top of `main`**:

```lean
def main : IO Unit := do
  Pari.initializePari   -- must be first
  ...
```

### `withStack` is typed `IO GEN → IO GEN`

Functions that return `IO Int` (such as `ellap`) cannot use `withStack`. Use manual `stackMark` / `stackRestore` instead:

```lean
def computeEllap (coeffsStr primeStr : String) : IO Int := do
  let mark ← stackMark
  let E    ← ellinit coeffsStr
  let p    ← readStr  primeStr
  let ap   ← ellap E p
  stackRestore mark
  return ap
```

### PARI 2.13.x error-handling macros

The old `pari_CATCH { } TRY { } CATCH(e) { e->warning }` syntax is **gone** in PARI 2.13.x. Use:

```c
pari_CATCH(CATCH_ALL) {
    /* error handler — __iferr_data (GEN) is available here */
    long num = err_get_num(__iferr_data);
    ...
} pari_TRY {
    /* code that may throw */
} pari_ENDCATCH;
```

## Compatibility checklist

### Lean v4.28.0
- [x] `lean-toolchain` set to `v4.28.0`
- [x] `lakefile.toml` format (default since `lake new`)
- [x] `moreLinkArgs` uses bare integer literals (`.ofNat` no longer needed)
- [x] `-fwrapv` compile flag removed (dropped in v4.28.0)
- [x] `lean_register_external_class` called with explicit `foreach` callback

### PARI 2.13.x
- [x] `PARI_INCLUDE` points to the parent of `pari/` (not `pari/` itself)
- [x] Correct `-L` path in `moreLinkArgs` with `-Wl,-rpath,...`
- [x] Error-handling macros updated to `pari_CATCH(CATCH_ALL) / pari_TRY / pari_ENDCATCH`

### FFI design
- [x] `initialize` removed; `initializePari` called explicitly in `main`
- [x] `withStack` used only for `IO GEN`; `IO Int` results use manual stack management

## License

MIT
