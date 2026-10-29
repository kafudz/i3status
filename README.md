# i3status (personal fork)

## Description

i3status is a small program for generating a status bar for i3bar, dzen2, xmobar
or similar programs. It is designed to be very efficient by issuing a very small
number of system calls, as one generally wants to update such a status line
every second. This ensures that even under high load, your status bar is updated
correctly. Also, it saves a bit of energy by not hogging your CPU as much as
spawning the corresponding amount of shell commands would.

## This fork

This is a **personal fork** of upstream i3status
(https://github.com/i3/i3status). It is maintained for my own daily use and may
differ from upstream in configuration defaults, small fixes and experimental
commits. If you want the canonical, supported version, use upstream or your
distribution package.

Changes in this fork are tracked in the commit log; notable local touches
include configuration/`etc` updates and small syntax/typo fixes. Upstream
compatibility is intended but not guaranteed.

### Fork vs. upstream: notes from review

- The fork is a derivative of `i3/i3status`; upstream remains the source of
  truth for features and fixes.
- Recent commits (Oct 2026 — `etc/i3status.conf`, `include`, `src`,
  `meson.build`, `tests/verification.sh`) use brief messages such as "fix",
  so review diffs against upstream before relying on them.
- The build system moved to Meson in Oct 2021; the old `autoconf` reference
  in the install line was stale.
- The dependency list and the Debian install line had drifted apart
  (`libiw-dev` present only in the apt line, `meson` missing from it); both
  are now consistent below.

## Development

i3status has the following dependencies:
  * libconfuse-dev
  * libyajl-dev
  * libasound2-dev
  * libnl-genl-3-dev
  * meson (compile-time only dependency)
  * asciidoc (only for the documentation)
  * libpulse-dev (for getting the current volume using PulseAudio)

On Debian-based systems, the following line installs all requirements
(including the wireless library used by the network module):

```bash
sudo apt-get install libconfuse-dev libyajl-dev libasound2-dev libiw-dev \
  asciidoc libpulse-dev libnl-genl-3-dev meson
```
Upstream
i3status is developed at https://github.com/i3/i3status

Compilation
Prefer installing i3status via your Linux distribution's package manager.

If you absolutely have to build from source, use:
```bash
mkdir build
cd build
meson setup ..
ninja
sudo ninja install
```
Personal usage notes
- Run i3status directly to see raw output, or point i3bar at it via
status_command i3status in your i3 config.

- Config file: copy etc/i3status.conf to ~/.i3status.conf (or
~/.config/i3status/config) and edit the modules you need.

- Test a config without restarting i3: i3status -c ~/.i3status.conf.

- Validate build after changes:
ninja -C build && ./build/i3status -c etc/i3status.conf.

- Keep in sync with upstream:
```bash
git remote add upstream https://github.com/i3/i3status
git fetch upstream
git rebase upstream/master
```
```

This single file combines:
- the original README description,
- corrected dependencies and build instructions (Meson, no `autoconf`, `libiw-dev` and `meson` consistent),
- fork identification plus review notes explaining how this repo differs from upstream,
- personal usage notes (config location, testing, running, syncing with upstream).
