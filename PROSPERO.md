# Prospero

This is the Ostrium Labs fork of [KytyPS5](https://github.com/KytyPS5/KytyPS5),
the base for **Prospero**, a vendor-neutral PS5 emulator.

The plan, architecture, roadmap, compatibility strategy and desktop launcher
live in the [ostrium-labs/prospero](https://github.com/ostrium-labs/prospero)
monorepo. The launcher is at
[`ui/`](https://github.com/ostrium-labs/prospero/tree/main/ui); it starts this
emulator as a child process and is Apache-2.0, separate from this GPL-2.0 fork.

## Relationship to upstream

- Upstream: `KytyPS5/KytyPS5`.
- This fork tracks upstream `main` and keeps patches small so they can be
  upstreamed.
- Licence: **GPL-2.0**, inherited from upstream. Do not relicense.
- No game data, firmware, keys, or leaked SDK material belongs in this repo.

## Why KytyPS5

It already contains the component the plan called highest-leverage: a working
RDNA2 → SPIR-V recompiler with a typed IR and a SPIR-V backend. It builds
headless on Linux, Windows and macOS, and it has the broadest HLE surface of the
2026 candidates. See the
[bake-off](https://github.com/ostrium-labs/prospero/blob/main/docs/bakeoff.md).

## Fork changes

### Shader corpus metadata

With `--graphics-debug-dump`, every compiled shader now gets a JSON manifest
next to its `.spv`:

```
_Shaders/
  original/Compute_<hash>.bin      # guest shader dwords
  Compute_<hash>_<id>.spv          # translated SPIR-V
  Compute_<hash>_<id>.json         # replay manifest
  Compute_<hash>_<id>.ir.txt       # IR dump
  Compute_<hash>_<id>.decoded.txt  # decoded ISA dump
```

The manifest records stage, hash, module id, wave size, user-data words,
back code, static state, push-data cursor, and — for compute shaders — the
input info needed to replay the shader without a game. This is what turns a
debug dump into a regression corpus.

See [docs/prospero/shader-corpus.md](docs/prospero/shader-corpus.md).

### Shader corpus replay

`shader_corpus_tool` loads a manifest, recompiles the shader and compares the
decoded dump, IR dump and SPIR-V with the capture:

```bash
_Build/linux-no-qt/shader_corpus_tool _Shaders --out /tmp/actual
```

`tests/corpus_smoke/` is a redistributable self-test.

## Controls

Keyboard and gamepad defaults, mouse behaviour and emulator shortcuts are in
[docs/prospero/key-mapping.md](docs/prospero/key-mapping.md).

## Build

Headless Linux, no Qt:

```bash
git submodule update --init --recursive --depth 1
cmake -S . -B _Build/linux-no-qt -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DKYTY_BUILD_LAUNCHER=OFF
cmake --build _Build/linux-no-qt --target kyty_emulator --parallel
```

Run:

```bash
./_Build/linux-no-qt/kyty_emulator --game <dir|elf|zar> --graphics-debug-dump
```

Only use game files you obtained legally from hardware you own.
