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

## Device evidence and current adaptation build

`Build Core cosmetics adaptation` calls the existing macOS build and verification
workflow with `--cosmetics-probe`. It retains the v19 main executable, feature
limits, signing permissions and bundle identifier. The default build remains v19.
The optional injected module adds a “美化适配” button to the Core app.

The r1 device report contains eight identical, stable samples: 30 sync slots,
six nonzero IDs and an empty client array. Game UUID and component ownership
checks passed. The user confirmed that the character wore the initial outfit.
The compact record is `device-evidence-r1.json`.

r2 is a write-capability check, not a working appearance replacement.
It pins Core's UUID and the original read/snapshot/write entry instructions, then
checks the remote game's UUID before reading candidate avatar arrays. The
original target read wrapper retains its PID, generation and binding checks.
The sampler checks component ownership, bounded arrays, repeated reads and
stable session/character state. It makes at most one same-value compare/write
attempt per capture against the exact initial-outfit slot in the r1 report.
The expected and replacement IDs are both 40601010; no appearance value is
changed. The original write backend retains all capability and binding guards.
Success requires enum status 1, exactly four bytes completed, native readback
availability and a separate stable readback. Status 3 or 5 must not be treated
as success just because its low bit is set. Other outfits skip the check.
No game files are changed, and appearance refresh remains unvalidated.

On the phone, keep the same initial outfit, enter the training ground, return
to Core, initialize it and enable its existing game acquisition.
Select “美化适配 → 检测写入并采集”, then
switch back to the training ground. After about 24 seconds,
return to Core and select “导出报告”. The file is
`Documents/Core-cosmetics-probe.json`, also exposed through File Sharing.
It contains statuses and numeric appearance fields, with no raw addresses,
process IDs or item instance IDs. Background expiration stops the capture.
The report's `write_check` establishes whether the current native backend can
perform a checked target write. The relative OnRep method offset is also
sampled without calling game code. These results are needed before enabling
appearance replacement and its refresh path.

`catalog_match.py` only downloads the pinned text catalog described by
`catalog-source.json`. It never executes third-party files. It checks the Git
blob digest and intersects candidate IDs with row names from the supplied
packages. External labels, especially truncated ones, remain candidate evidence
and are not automatically turned into a device write configuration.

All compilation, native parser tests, packaging regressions and final IPA
signature/program preservation checks run in GitHub Actions. No such checks
run on the user's server.
