# Sniper two-hand aim capture

This build can record a sampled `aim79` trace while reproducing a jump. The
trace is off by default. Use an APK built from this checkout.

1. Install the APK with `adb install -r`. Use the release APK if the headset
   already has a release-signed copy.
2. Start the game once, then enable the trace:

   ```text
   adb shell "touch /sdcard/Android/data/com.gevr.port/files/gevr_aimlog.txt"
   adb shell "chmod 644 /sdcard/Android/data/com.gevr.port/files/gevr_aimlog.txt"
   ```

3. In a terminal on the PC, start a capture and leave it running:

   ```text
   adb logcat -v time -s GoldenEye:I > aim79-demo.log
   ```

4. Hold the sniper with both hands. Aim at a fixed point, slowly move each
   hand separately, bring them together, and briefly hide the support hand
   from the headset. Note which motion produces a jump. A headset recording
   alongside the log is useful.
5. Stop the capture with Ctrl+C and share `aim79-demo.log`. Disable the trace:

   ```text
   adb shell "rm /sdcard/Android/data/com.gevr.port/files/gevr_aimlog.txt"
   ```

The `aim79 hold` lines show grip state, actual tracking for both controllers,
consecutive out-of-range samples (`far`), and distance from the barrel. The `aim79 axis`
lines show raw and filtered hand separation, angular difference from the
wrist, blend, filter step, magnification, and both hand positions. They are
sampled about every six controller snapshots to keep the log manageable.
