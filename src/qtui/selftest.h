#ifndef SELFTEST_H
#define SELFTEST_H

class QString;

// Renderer selftest: create a synthetic red/blue test image and verify pixel values
// Returns 0 on success; 1 on failure
int runSelftestRender();

// Selftest: Unlock vault programmatically and verify rendering
// Step 1: Check vault can be opened/unlocked and has image
// Step 2: Load QML UI, programmatically unlock, verify render pipeline executes
// Returns 0 on success; 1 on failure
int runSelftest(const QString& vaultPath);

#endif  // SELFTEST_H
