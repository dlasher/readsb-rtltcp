# Readsb RTLTCP

Fork of [wiedehopf/readsb:dev](https://github.com/wiedehopf/readsb) with **rtl_tcp client support** built in, allowing readsb to connect to a remote rtl_tcp server over the network instead of a local USB RTL-SDR dongle.

## Why

The upstream readsb only supports local USB RTL-SDR devices via librtlsdr. This fork adds native rtl_tcp protocol support directly into the existing rtlsdr driver, enabling readsb to receive I/Q samples from any rtl_tcp server on the network with zero external dependencies.

## What was added

- **rtl_tcp client** in `sdr_rtlsdr.c` — TCP socket connection, protocol handshake, sample reception, and automatic reconnect
- **New command-line options:**
  - `--device rtl_tcp:host:port` — connect to remote rtl_tcp server (default port: 1234)
  - `--rtltcp-direct-samp=<mode>` — set direct sampling mode (0=off, 1=I-ADC, 2=Q-ADC)
  - `--rtltcp-offset-tune=<0|1>` — enable offset tuning
  - `--rtltcp-bias-tee=<0|1>` — enable bias-T on GPIO pin 0
- **Automatic reconnect** with 5-second retry on disconnect
- **Dockerfile.rtl_tcp** — multi-stage Docker build (compatible with systems without buildx)

## Usage

```bash
# Connect to a remote rtl_tcp server
readsb --device-type rtlsdr --device rtl_tcp:192.168.1.100:1234 --gain 496

# With all options
readsb --device-type rtlsdr --device rtl_tcp:10.0.0.5:1234 \
    --gain 496 --ppm 46 \
    --rtltcp-direct-samp 0 \
    --rtltcp-bias-tee 1

# With networking enabled
readsb --device-type rtlsdr --device rtl_tcp:10.0.0.5:1234 --gain 496 --net
```

For the full list of readsb options, see the [upstream README.md](https://github.com/wiedehopf/readsb?tab=readme-ov-file#readsb---help).

## Building

Same as upstream readsb — no extra build dependencies needed. The rtl_tcp code uses only POSIX sockets.

```bash
git clone https://github.com/FirebirdRender/readsb-rtltcp.git
cd readsb-rtltcp
make RTLSDR=yes
```

For the full build instructions and Debian package creation, see the [upstream README.md](https://github.com/wiedehopf/readsb?tab=readme-ov-file#debian-package).

## Docker

Three Dockerfiles are provided:

| File | Purpose |
|------|---------|
| `Dockerfile` | Standard build (uses BuildKit `--mount` syntax) |
| `Dockerfile.rtl_tcp` | RTL-TCP client support — **no buildx required**, uses `COPY` instead of `--mount` |
| `Dockerfile.soapy` | SoapySDR support — multi-stage build with `COPY`, no buildx required |

### Build (rtl_tcp version)

```bash
docker build -f Dockerfile.rtl_tcp -t readsb-rtltcp:latest .
```

### Run

```bash
docker run --rm --network host readsb-rtltcp:latest \
    --device-type rtlsdr \
    --device rtl_tcp:10.0.0.5:1234 \
    --gain 496 \
    --net
```

## Credits / lineage

antirez (original dump1090) → Malcom Robb → mutability (dump1090-fa) → Mictronics (readsb) → wiedehopf (this fork) → **FirebirdRender (rtl_tcp client support)**

## NO WARRANTY

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
see the LICENSE file for details
