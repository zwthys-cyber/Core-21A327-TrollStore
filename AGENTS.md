# Build and verification

The user requires compilation and automated tests to run only in GitHub Actions. Do not compile, link, run Python/Unicorn regression tests, or run native test executables on this server; local compilation and tests caused unacceptable resource use.

Use this workspace for file edits, Git operations, reading CI results, downloading or publishing artifacts, and lightweight metadata/checksum inspection. Validate the final build with the existing GitHub Actions workflow.

Project handoff, current device evidence and unfinished vehicle work are recorded in `docs/AI_HANDOFF_ZH.md`. Read that document before continuing the cosmetics task; do not treat CI success as device validation.
