# Argon2 upstream provenance

- Upstream: https://github.com/P-H-C/phc-winner-argon2
- Release: `20190702`
- Commit: `62358ba2123abd17fccf2a108a301d4b52c01a7c`
- Imported: 2026-08-23
- Configuration: portable reference implementation with `ARGON2_NO_THREADS`

The CLI, benchmarks, KAT generator, `src/thread.c`, `src/opt.c`, and optimized
SIMD headers are intentionally omitted. Quetoo compiles the five selected C
sources directly into Race GAME and its focused test target. The upstream
`LICENSE` is preserved without modification; this import uses its CC0 option.

## SHA-256

```text
include/argon2.h df20cb726a2fe6bccc736b81ea0d86219766a0d17f1794c19e048a34830ca1cc
LICENSE 220f8736a89ff51c92ef3d497f413b48e6cf1df3d6278bc909c6308c78e1718e
src/argon2.c 72a93deebc5fd76bec0c6d300a2d92b500fb354b26babbddbc6d8b88681e663d
src/blake2/blake2-impl.h 8ac91f1f57d94235f8de0069b7809be1deb365c3b37adb8e30423cd364aff09a
src/blake2/blake2.h 3c2de197dd23179f78c57deac7b73a6049778bf651c5db57e508b2cfce5e7559
src/blake2/blake2b.c 52519ccbc1e48f489ff444091f630d05dadcc8f1ee61c5b7361e3a9618299c9a
src/blake2/blamka-round-ref.h bcfdcf785218cf897f05b144e80b659e611188fee3887d533bdcb7a6aa4c336b
src/core.c 0f53eb2370f8971f04fcf043b8083bf81d1530c48b2ead71c0b6c4e22a01aeec
src/core.h c9665623cb3d306f63b6a3effd87bcfab971c28253c77e111445e42c1523235e
src/encoding.c e20124ec0f780f88527e79cf00825e839624ed1f469c7e231596bf070f8c7b14
src/encoding.h 42f283d12ec445cfb423ac2f1f5a5d1a7f152ce2b9363f6ec3e1d5b61cd4ee6d
src/ref.c 4ec47b080c22f4ee416b9dbcfae70ee6007fcfba427f870d7b84395846fa1bc7
src/thread.h 9eab7f9ff356862a00a3075478dd0059344953ed79daab8b29185be2010efe78
```
