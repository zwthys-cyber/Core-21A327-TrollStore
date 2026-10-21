# Upstream source investigation

The v18 report demonstrated cached crossbows at 133 m and 62 m and valid bow
projection beyond 20 m. Later watched pointers disappeared from both classified
source and display vectors. This does not establish a server limitation.

This diagnostic build changes three guarded call destinations only:

- D5404: native D632C actor pointer snapshot, including its 24-byte indirect result.
- D63E0: existing E1908 chunk read, unchanged arguments and boolean result.
- D6410: existing E1908 fallback pointer read, unchanged arguments and result.

The native helper still allocates its vector, performs its original chunk reads,
zeroes failed chunks, and performs its original individual fallback reads. The
bridge transfers the same three result words without destroying the vector.
Incoming errno reaches the original snapshot; its resulting errno is forwarded.
The scanner's original three-second throttle, publication, filters, rendering,
ground item preferences, fill precision and source/cache lifetimes are unchanged.

Only up to 256 previously classified bow pointers are watched. Watches are sorted
on the background report queue. The native wrapper checks at most 50,000 existing
local pointer slots with binary search. Try-lock contention drops diagnostic data
instead of delaying original work. No additional game reads or retries are added.

The report compares Core's existing scan timestamp with the captured copy. A newer
attempt that does not reach D632C invalidates the older evidence. Each target says
present, not_found_in_completed_copy, unknown_after_read_failure, not_watched,
incomplete_copy, watch_copy_unavailable, latest_scan_not_observed or unknown_scan.
Successful fallback after a failed chunk remains usable; failed individual reads
make missing pointers unknown. A found pointer remains positive evidence even if
another pointer read fails. Source/cache pointer membership before type exclusion
is reported separately to avoid mistaking a changed type flag for upstream loss.

The report is not an atomic game snapshot. Pointer reuse and replaced item wrapper
actors remain unverified identities. Completed absence does not prove server or
game absence. The build is a diagnostic step, not a claimed disappearance fix.

Validation runs only in Actions: actual ARM64 helper parity including failed chunk
fallback, compiled entry argument/LR/x8 forwarding, production native/background
integration with synthetic inputs under ASan/UBSan, independent item menu/range
regressions, existing raster references/TSan, and full signed payload verification.
