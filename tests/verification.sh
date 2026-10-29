
# 1. Check the current branch and local modifications
git status --short
git log -5 --oneline

# 2. Build with sanitizers
meson setup build-review \
  -Db_sanitize=address,undefined \
  -Dmans=false

meson compile -C build-review

# 3. Run the project's tests
meson test -C build-review --print-errorlogs

# 4. Check the executable's available options
./build-review/i3status --help

# 5. Inspect the changes relative to upstream
git remote -v
git fetch https://github.com/i3/i3status.git main
git diff FETCH_HEAD...HEAD --stat
git diff FETCH_HEAD...HEAD -- i3status.c meson.build README.md
