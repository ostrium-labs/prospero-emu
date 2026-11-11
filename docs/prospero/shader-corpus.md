# Shader corpus: capture and replay

The goal is a regression corpus of **real** shaders captured from titles the
user owns, recompiled deterministically in CI without the game.

This is the missing half of the existing test stack:

| Layer | Existing | Gap |
|---|---|---|
| Opcode/IR semantics | `tests/shaderCfgTests.cpp` | — |
| GPU execution of synthetic cases | `tests/ShaderRecompilerComputeTests.cpp` | — |
| Real captured shaders | — | **this** |
| Hardware golden frames | — | P3 |

## Capture

Run the emulator with graphics debug dumps enabled:

```bash
./kyty_emulator --game <your-dump> --graphics-debug-dump
```

Output lands in `_Shaders/` (or `--shader-log-folder <path>`):

```
_Shaders/
  original/<Stage>_<hash>.bin       guest shader dwords, little-endian
  <Stage>_<hash>_<id>.spv           translated SPIR-V
  <Stage>_<hash>_<id>.json          replay manifest
  <Stage>_<hash>_<id>.ir.txt        IR dump
  <Stage>_<hash>_<id>.decoded.txt   decoded ISA dump
```

Do not publish captures from commercial games. A corpus is local, or built from
homebrew and shaders you are allowed to redistribute.

## Manifest format (version 1)

```json
{
  "format": 1,
  "stage": "Compute",
  "shader_hash": "0x0123456789abcdef",
  "module_id": "0xfedcba9876543210",
  "wave_size": 64,
  "user_data_base": 0,
  "user_data": ["0x00000000", "..."],
  "back_code": [],
  "user_data_count": 16,
  "static_state": [],
  "push_data_start_dword": 0,
  "code_file": "original/Compute_0123456789abcdef.bin",
  "spirv_file": "Compute_0123456789abcdef_fedcba9876543210.spv",
  "ir_file": "Compute_0123456789abcdef_fedcba9876543210.ir.txt",
  "decoded_file": "Compute_0123456789abcdef_fedcba9876543210.decoded.txt",
  "standalone_replay": true,
  "compute_input": {
    "threads_num": [8, 8, 1],
    "lds_size_dwords": 0,
    "scratch_size_dwords": 0,
    "host_subgroup_size": 64,
    "wave_size": 64,
    "async_compute": false,
    "float_mode": 192,
    "dispatch_threads_num": [0, 0, 0],
    "workgroup_counts": [0, 0, 0],
    "indirect_args_address": "0x0000000000000000",
    "group_id": [false, false, false],
    "dispatch_thread_dimensions": false,
    "lds_storage": false,
    "thread_ids_num": 0,
    "workgroup_register": 0,
    "tg_size_en": false
  }
}
```

`standalone_replay` is `true` only when the manifest carries everything needed
to recompile the shader without a running guest. Compute shaders are supported
first. Graphics stages need their `ShaderVertexInputInfo` / `ShaderPixelInputInfo`
serialized before they can be replayed standalone.

## Check a capture

```bash
python3 tools/prospero/corpus_check.py --root _Shaders
```

It verifies the manifest references, checks every `.spv` with `spirv-val`, and
fails on any broken entry. `--require-replay` also fails on shaders that are not
standalone-replayable, which is the right setting for a CI corpus.

## Replay

`shader_corpus_tool` loads one manifest or a directory of them, re-runs the
recompiler and compares the result with the capture:

```bash
_Build/linux-no-qt/shader_corpus_tool _Shaders --out /tmp/actual
```

- the decoded ISA dump and the IR dump are compared as text,
- SPIR-V is compared word-for-word, which is deterministic for the same input,
- `--frontend-only` skips the compile and only checks the decoder,
- `--capture` regenerates `.decoded.txt`, `.ir.txt` and `.spv` from a manifest;
  use it to update a baseline deliberately, never to hide a regression,
- a mismatch exits non-zero and writes the actual artifacts under `--out`.

`tests/corpus_smoke/` is the redistributable self-test. It captures a single
`s_endpgm` compute shader, so it exercises the manifest reader, the replay path
and the comparison without shipping game data.

Resource materialization needs descriptor memory that the manifest does not
carry yet, so most real shaders currently replay in frontend-only mode. Full
SPIR-V replay for them needs a captured resource snapshot (descriptor table and
SRT reads), which is the next corpus addition.

## CI

- Corpus is a local artifact; CI uses a small, redistributable subset.
- Every shader is compiled and validated on NVIDIA and AMD.
- A shader that fails to compile is a hard failure, never a skip.
- Baselines are updated only in a PR that explains the change.
