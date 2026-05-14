#!/bin/bash
set -e

ARGS=()

# Location / position
[ -n "$LAT" ] && ARGS+=("--lat" "$LAT")
[ -n "$LON" ] && ARGS+=("--lon" "$LON")

# SDR device configuration
[ -n "$READSB_DEVICE_TYPE" ] && ARGS+=("--device-type" "$READSB_DEVICE_TYPE")
[ -n "$READSB_DEVICE" ] && ARGS+=("--device" "$READSB_DEVICE")
[ -n "$GAIN" ] && ARGS+=("--gain" "$GAIN")
[ -n "$PPM" ] && ARGS+=("--ppm" "$PPM")

# Networking
if [ "$NET" = "yes" ]; then
    ARGS+=("--net")
    [ -n "$NET_BO_PORT" ] && ARGS+=("--net-bo-port" "$NET_BO_PORT")
    [ -n "$NET_BI_PORT" ] && ARGS+=("--net-bi-port" "$NET_BI_PORT")
    [ -n "$NET_RO_PORT" ] && ARGS+=("--net-ro-port" "$NET_RO_PORT")
    [ -n "$NET_RI_PORT" ] && ARGS+=("--net-ri-port" "$NET_RI_PORT")
    [ -n "$NET_SBS_PORT" ] && ARGS+=("--net-sbs-port" "$NET_SBS_PORT")
    [ -n "$NET_CONNECTOR" ] && ARGS+=("--net-connector" "$NET_CONNECTOR")
fi

# Additional options passed through
[ -n "$READSB_EXTRA_ARGS" ] && ARGS+=($READSB_EXTRA_ARGS)

# If user passed extra args on the command line, append them
ARGS+=("$@")

# Log the command for debugging
echo "Starting readsb with: ${ARGS[*]}"

exec /usr/local/bin/readsb "${ARGS[@]}"
