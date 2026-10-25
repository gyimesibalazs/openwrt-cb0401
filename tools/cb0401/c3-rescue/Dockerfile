# Build environment for the C3 rescue console firmware (see build.sh)
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
      curl ca-certificates python3 git xz-utils && \
    rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/v1.5.1/install.sh \
      | BINDIR=/usr/local/bin sh -s 1.5.1

ENV ARDUINO_DIRECTORIES_DATA=/root/.arduino15 \
    ARDUINO_DIRECTORIES_DOWNLOADS=/root/.arduino15/staging

RUN arduino-cli config init && \
    arduino-cli config add board_manager.additional_urls \
      https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json && \
    arduino-cli core update-index && \
    arduino-cli core install esp32:esp32@3.1.3
