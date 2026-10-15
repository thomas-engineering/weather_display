# Screen saver: test paths without waiting for the timeout

The real timeout is 15 / 30 / 60 minutes of inactivity (Settings > Screen
saver). None of the paths below wait for it.

## 1. Host unit test (logic)

`./scripts/host-test.sh` runs `host_test/main/test_screensaver_policy.c`:
start threshold, "fires only once", tap stops it and restarts the countdown,
switching the setting off, 12 h digit split, burn-in drift. The inactivity time
is just a number there, so no clock is involved.

## 2. Simulator (drawing and tap-to-dismiss)

    ./scripts/sim.sh --screen screensaver        --screenshot out.bmp
    ./scripts/sim.sh --screen screensaver-orange --screenshot out.bmp
    ./scripts/sim.sh --screen screensaver-dismiss --screenshot out.bmp

`screensaver-dismiss` brings the saver up, sends a real tap through the mouse
indev and prints `SAVER_DISMISS OK` or `SAVER_DISMISS FAIL`; the screenshot then
shows the main screen again. `./scripts/sim.sh --shots <dir>` runs all three
and fails if the dismiss check does not print `OK`. The simulator clock is fixed
(Thu 2026-10-15 10:08:42) so screenshots are reproducible.

## 3. On the board (not run in CI, needs a human at the display)

Build with the test option so the setting counts seconds, not minutes:

1. `CONFIG_WEATHER_SCREENSAVER_FAST_TEST=y` in the generated `sdkconfig`
   (or `idf.py menuconfig` > Weather display), then
   `./scripts/hw-flash.sh /dev/ttyACM0`.
2. After the first weather fetch (the saver needs the city's UTC offset for its
   clock), open Settings > "Screen saver after inactivity" and pick `15 min`.
3. Leave the display alone: the saver appears after **15 s** and Settings closes.
4. Tap anywhere: it disappears at once and the tap does not hit what is below.
5. Wait again: it returns after 15 s. `Off` stops it. Check both colour styles.
6. Reboot: timeout and style are restored.

Switch the option off again before building anything you flash for real.
