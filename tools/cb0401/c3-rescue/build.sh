#!/bin/sh
# Build the C3 rescue console firmware in Docker; images land in out/:
#   c3_uart_bridge.ino.merged.bin   full flash image, write at 0x0 over USB
#   c3_uart_bridge.ino.bin          app image for the OTA page
set -e
cd "$(dirname "$0")"
[ -f c3_uart_bridge/secrets.h ] || {
	echo "create c3_uart_bridge/secrets.h from secrets.h.example first" >&2
	exit 1
}
docker build -q -t cb0401-espbuilder .
mkdir -p out
docker run --rm \
	-v "$PWD/compile.sh:/compile.sh:ro" \
	-v "$PWD/c3_uart_bridge:/sketch/c3_uart_bridge:ro" \
	-v "$PWD/out:/out" \
	cb0401-espbuilder bash /compile.sh
