## Description

<!-- Provide a clear and concise description of your changes -->

### What does this PR do?

### Why is this change needed?

### Related Issue(s)

<!-- Link to issues this PR addresses -->

Closes #

## Changes Made

<!-- List the specific changes in bullet points -->

-
-
-

## Testing Performed

### Test Environment

- **Pi Model**: <!-- Pi 5 8GB / Pi 4 4GB / etc -->
- **Camera Type**: <!-- Pi GS / Innomaker / etc -->
- **OS Version**: <!-- Bookworm 64-bit / etc -->
- **Installation Method**: <!-- Source / APT / Docker -->

### Test Results

<!-- Describe testing performed and results -->

- [ ] `pitrac test hardware` passes
- [ ] `pitrac test camera` passes (if camera-related)
- [ ] `pitrac test pulse` passes (if strobe-related)
- [ ] Simulator integration tested with: <!-- GSPro / E6 / etc -->
- [ ] Performance metrics: <!-- FPS, latency, accuracy if relevant -->

### Test Commands Run

```bash
# Paste the actual commands and output
```

## Performance Impact

<!-- For performance-related changes -->

- **Before**: <!-- Metrics before change -->
- **After**: <!-- Metrics after change -->
- **Impact**: <!-- Positive/Negative/Neutral -->

## Breaking Changes

<!-- List any breaking changes and migration steps -->

- [ ] This PR includes breaking changes
    - **What breaks**:
    - **Migration steps**:

## Dependencies

<!-- List any new dependencies or version changes -->

- [ ] No new dependencies
- [ ] New dependencies added:
    -
- [ ] ## Updated dependencies:

## Hardware Compatibility

<!-- For hardware-related changes -->

- [ ] Tested on Pi 5
- [ ] Tested on Pi 4
- [ ] Tested with single Pi setup
- [ ] Tested with dual Pi setup

## Documentation

<!-- Documentation changes needed -->

- [ ] No documentation needed
- [ ] Documentation updated in this PR
- [ ] Documentation PR to follow
- [ ] Updated relevant sections:
    - [ ] README
    - [ ] Hardware guide
    - [ ] Software guide
    - [ ] Troubleshooting guide

## Screenshots/Videos

<!-- If applicable, add screenshots or videos demonstrating the change -->

## AI (Vibe-Coded) Content Description

<!-- Describe whether this PR contains any AI-generated code, and if so, the amount, location, etc. -->

## Checklist

### Code Quality

- [ ] Code follows existing patterns and conventions
- [ ] No unnecessary comments added
- [ ] Error handling implemented appropriately

### Build & Test

- [ ] Successfully builds with `./packaging/build.sh build`
- [ ] All existing tests pass
- [ ] New tests added for new functionality
- [ ] Tested on actual Raspberry Pi hardware (not just CI)

### Submission Requirements

- [ ] Commits squashed if needed (`git rebase -i HEAD~n`)
- [ ] [CLA signed](https://gist.github.com/jamespilgrim/e6996a438adc0919ebbe70561efbb600)
- [ ] PR title follows format: `[PR TYPE] Brief description`
- [ ] Branch is up-to-date with main

## Additional Context

<!-- Any additional information that reviewers should know -->

---

<!--
Thank you for contributing to PiTrac! 🏌️
Join our Discord for help: https://discord.gg/vGuyAAxXJH
-->
