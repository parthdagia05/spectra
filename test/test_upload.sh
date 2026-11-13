#!/system/bin/sh
# Push to phone: adb push test/test_upload.sh /data/local/tmp/
# Run: adb shell sh /data/local/tmp/test_upload.sh
#
# Generates network traffic from the phone for spectra to detect.

echo "spectra test: generating network traffic..."

# Create test data (1KB)
dd if=/dev/urandom of=/data/local/tmp/spectra_test_data bs=1024 count=1 2>/dev/null

# Try to upload to a known endpoint (httpbin)
if command -v curl >/dev/null 2>&1; then
    curl -s -X POST -d @/data/local/tmp/spectra_test_data \
        https://httpbin.org/post > /dev/null 2>&1
    echo "Sent test data via curl"
elif command -v wget >/dev/null 2>&1; then
    wget -q --post-file=/data/local/tmp/spectra_test_data \
        https://httpbin.org/post -O /dev/null 2>&1
    echo "Sent test data via wget"
else
    # Fallback: just make a connection
    ping -c 3 8.8.8.8 > /dev/null 2>&1
    echo "Pinged 8.8.8.8 as fallback"
fi

rm -f /data/local/tmp/spectra_test_data
echo "Done. Now run: spectra scan --android"
