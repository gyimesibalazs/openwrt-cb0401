# CB0401V2 rescue console (ESP32-C3 Super Mini)

Sits on the router's 1.8 V serial console: interrupts U-Boot on every
power-on and runs the boot command stored in its NVS, or TFTP-boots `ow.itb`
into RAM; bridges the console to its WiFi AP (raw TCP 2323) and keeps a boot
log.

Wiring: C3 GPIO7 (TX) through a 1k (series) / 1.2k (to GND) divider to router
RX (3.3 V -> 1.8 V), router TX straight to GPIO6, GND. Power it from the
router or from USB, never both at once.

Commands unlock with `::cb0401-magic::`: `!nand` (default), `!tftp`,
`!hold`, `!bootcmd <cmd>`, `!status`, `!dump`, `!dl` (USB download mode),
`!lock`. The AP is up for 90 s after power-on and stays up once someone joins.
`!hold` and `!tftp` last until the C3 restarts, so restart the router
(web UI, `reboot`, U-Boot `reset`) rather than power-cycling it.

Build: copy `c3_uart_bridge/secrets.h.example` to `secrets.h`, set your own
AP passphrase (also the OTA password), run `./build.sh` (Docker). Flash
`out/c3_uart_bridge.ino.merged.bin` at 0x0 over USB once; later updates go to
`http://192.168.4.1/` (OTA, app image `c3_uart_bridge.ino.bin`).
