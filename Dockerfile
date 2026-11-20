# Toolchain image only; the source tree is mounted at build time.
FROM alpine:3.22
RUN apk add --no-cache make gcc-arm-none-eabi newlib-arm-none-eabi python3 py3-crcmod git
WORKDIR /src
