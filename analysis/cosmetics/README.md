# Core cosmetics investigation

Target: the user-provided iOS game 1.38.12, build 15915, UUID
`34B785B2-0DAB-3992-985D-359E6BF45585`, with the original v19 Core
binary and iOS 17.0/TrollStore environment.

The resource packages contain replacements and human-readable appearance
recipes. These do not provide runtime object layouts, item identifiers or a
callable Core write interface. This analysis records candidate reflection
descriptors and native registration functions from the exact game input.
Candidate offsets remain unvalidated until their owning type and runtime
behavior have been checked. No game memory is modified by these scripts.

All disassembly runs in the `Analyze Core cosmetics inputs` GitHub Actions
workflow. The original executable and packages are stored only in a private
draft release, not in Git history or published releases. The workflow uploads
metadata and disassembly reports only. The existing main branch and v19
release remain the working baseline.
