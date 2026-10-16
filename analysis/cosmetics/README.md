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
workflow with `--cosmetics-probe`. It retains the v19 feature limits, signing
permissions and bundle identifier. The default build remains v19. r4 makes
bounded optional native menu changes: restore one sidebar row below Position,
label it “美化”, and route only its page to the injected C renderer through the
original authenticated dlsym stub. Other saved page values fall back to
initialization. There is no separate UIKit button and no runtime code patching.
The current artifact is `Core-17.0-21A327-cosmetics-menu-r4.ipa`; see
[the r4 guide](../../docs/cosmetics-r4.md).

The r1 device report contains eight identical, stable samples: 30 sync slots,
six nonzero IDs and an empty client array. Game UUID and component ownership
checks passed. The user confirmed that the character wore the initial outfit.
The compact record is `device-evidence-r1.json`.

r2 was a write-capability check, not an appearance replacement.
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
The user's r2 report confirmed native status 1, four bytes completed and stable
readback, with the expected OnRep RVA. The compact record is
`device-evidence-r2.json`.

r3 changes only slot 5 from initial white shirt 403251 to golden-dragon coat
403188, requests the game's deferred refresh, and attempts restoration after
15 seconds or on background expiration. The exact target name is cross-checked
against the pinned text catalog and current package Item row names. It waits
for Core/character readiness, checks the binding generation and instance, and
does not overwrite later game-owned slot changes during restoration.
`trial-profile.json` records the reviewed game-specific refresh path. GitHub
analysis confirms the tick consumes the pending flag through its native updater,
which clears that flag and calls the existing appearance callback. The user
confirmed the golden-dragon coat appeared and then reverted to the white shirt;
`device-evidence-r3.json` records this report and explicit visual confirmation.
The full packages are not converted, and the new r4 menu remains unverified on device.
No game files are changed. The report remains `Documents/Core-cosmetics-probe.json`,
with `apply`, `observations` and `restore`, without raw addresses, process IDs or
item instance IDs.

The lobby's character getter/property names were located in the pinned game,
but their owner chain has not been validated. No lobby write path is enabled.

`catalog_match.py` only downloads the pinned text catalog described by
`catalog-source.json`. It never executes third-party files. It checks the Git
blob digest and intersects candidate IDs with row names from the supplied
packages. External labels, especially truncated ones, remain candidate evidence
and are not automatically turned into a device write configuration.

All compilation, native parser tests, packaging regressions and final IPA
signature/program preservation checks run in GitHub Actions. No such checks
run on the user's server.
