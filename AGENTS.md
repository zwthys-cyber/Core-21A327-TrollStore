# Build and verification

The user requires compilation and automated tests to run only in GitHub Actions. Do not compile, link, run Python/Unicorn regression tests, or run native test executables on this server; local compilation and tests caused unacceptable resource use.

Use this workspace for file edits, Git operations, reading CI results, downloading or publishing artifacts, and lightweight metadata/checksum inspection. Validate the final build with the existing GitHub Actions workflow.
