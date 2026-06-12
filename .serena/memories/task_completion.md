# Task Completion Checklist

Run these in order before declaring any coding task done:

1. **Build (Debug)**
   ```bash
   scripts/build.sh
   ```

2. **Full test suite**
   ```bash
   scripts/test.sh
   ```
   All tests must pass.

3. **ASAN/UBSan/LSan** (for any crypto, memory, or vault changes)
   ```bash
   scripts/test.sh --asan
   ```

4. **Regenerate compile_commands.json** (if files were added/removed/renamed)
   ```bash
   scripts/gen.sh
   ```

5. **SonarCloud / SonarQube** — use findings posted to the PR directly; do NOT attempt to install or authenticate sonarqube-cli (agents cannot complete the auth flow). Zero issues must remain before merging.

6. **CI green** — all checks must pass on all platforms. ASAN job does not provision SDL3; do not add SDL3 as a dep to that job.

7. **ROADMAP / docs** — after a squash merge, `git fetch` and confirm ROADMAP.md and any doc changes are present on `origin/main`. A squash merge can silently drop files if PR ordering is wrong.
