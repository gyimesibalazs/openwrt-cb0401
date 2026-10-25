#!/bin/bash
set -e
FQBN="esp32:esp32:esp32c3:CDCOnBoot=cdc,FlashMode=qio,FlashFreq=80,FlashSize=4M,PartitionScheme=default"
echo "=== compiling $FQBN ==="
arduino-cli compile --fqbn "$FQBN" --output-dir /out /sketch/c3_uart_bridge
echo "=== artifacts ==="
ls -la /out
