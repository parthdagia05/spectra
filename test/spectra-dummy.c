/*
 * spectra-dummy: test harness for spectra Android detection.
 *
 * Instead of cross-compiling this, use the simpler approach:
 *   1. Open any camera app on the phone manually
 *   2. Run spectra scan --android
 *   3. Confirm detection
 *
 * For network testing, push and run test_upload.sh:
 *   adb push test/test_upload.sh /data/local/tmp/
 *   adb shell sh /data/local/tmp/test_upload.sh
 *
 * This file exists as the cross-compiled alternative if needed.
 * Build with Android NDK:
 *   $NDK/toolchains/llvm/prebuilt/*/bin/aarch64-linux-android33-clang \
 *     -o spectra-dummy test/spectra-dummy.c
 *   adb push spectra-dummy /data/local/tmp/
 *   adb shell /data/local/tmp/spectra-dummy
 */

#include <stdio.h>

int main(void)
{
    printf("spectra-dummy: test harness placeholder\n");
    printf("Use the shell-based test instead:\n");
    printf("  adb shell am start -a android.media.action.STILL_IMAGE_CAMERA\n");
    printf("  spectra scan --android\n");
    return 0;
}
