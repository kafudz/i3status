git remote -v
git fetch origin
git fetch https://github.com/i3/i3status.git main

# Compare your fork with upstream
git diff FETCH_HEAD...origin/main --stat
git diff FETCH_HEAD...origin/main -- i3status.c

# Build and run tests
meson setup build-review
meson compile -C build-review
meson test -C build-review --print-errorlogs

# Test configuration validation
./build-review/i3status --config-test
./build-review/i3status --help