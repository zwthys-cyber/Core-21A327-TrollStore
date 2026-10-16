# Core cosmetics investigation

Target: the user-provided iOS game 1.38.12, build 15915, UUID
`34B785B2-0DAB-3992-985D-359E6BF45585`, with the original v19 Core
binary and iOS 17.0/TrollStore environment.

The resource packages contain replacements and human-readable appearance
recipes. Their text files alone do not establish runtime object layouts or
item identifiers. This analysis records candidate reflection
descriptors and native registration functions from the exact game input.
Candidate offsets remain unvalidated until their owning type and runtime
behavior have been checked. No game memory is modified by these scripts.

All disassembly runs in the `Analyze Core cosmetics inputs` GitHub Actions
workflow. The original executable and packages are stored only in a private
draft release, not in Git history or published releases. The workflow uploads
metadata and disassembly reports only. The existing main branch and v19
release remain the working baseline.

## Device adaptation build

`Build Core cosmetics adaptation` calls the existing macOS build and verification
workflow with `--cosmetics-probe`. It retains the v19 main executable, feature
limits, signing permissions and bundle identifier. The default build remains v19.
The optional injected module adds a “美化适配” button to the Core app.

This is a read-only adaptation build, not a working appearance replacement.
It pins Core's UUID and the original read/snapshot entry instructions, then
checks the remote game's UUID before reading candidate avatar arrays. The
original target read wrapper retains its PID, generation and binding checks.
The sampler checks component ownership, bounded arrays, repeated reads and
stable session/character state. It does not invoke the native write entry or
change game files. A successful sample is still candidate layout evidence.

On the phone, enter the training ground, return to Core, initialize it and
enable its existing game acquisition. Select “美化适配 → 开始采集”, then
switch back to the training ground. After about 24 seconds,
return to Core and select “导出报告”. The file is
`Documents/Core-cosmetics-probe.json`, also exposed through File Sharing.
It contains statuses and numeric appearance fields, with no raw addresses,
process IDs or item instance IDs. Background expiration stops the capture.
The report is needed to establish actual device behavior before implementing
the appearance write and refresh path.

All compilation, native parser tests, packaging regressions and final IPA
signature/program preservation checks run in GitHub Actions. No such checks
run on the user's server.
