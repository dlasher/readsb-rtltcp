// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// sdr_rtlsdr.c: rtlsdr dongle support
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
//
// This code is based on a detached fork of dump1090-fa.
//
// Copyright (c) 2014-2017 Oliver Jowett <oliver@mutability.co.uk>
// Copyright (c) 2017 FlightAware LLC
//
// This file is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
//
// This file is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// This file incorporates work covered by the following copyright and
// license:
//
// Copyright (C) 2012 by Salvatore Sanfilippo <antirez@gmail.com>
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//  *  Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//
//  *  Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "readsb.h"
#include "sdr_rtlsdr.h"

#include <rtl-sdr.h>

// POSIX socket headers for rtl_tcp client support
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

#if (defined(__arm__) || defined(__aarch64__)) && !defined(DISABLE_RTLSDR_ZEROCOPY_WORKAROUND)
#  define USE_BOUNCE_BUFFER
#endif

// rtl_tcp protocol structures
#pragma pack(push, 1)
struct rtltcp_command {
    unsigned char cmd;
    unsigned int param; // network byte order (big-endian)
};
#pragma pack(pop)

typedef struct {
    char magic[4];            // "RTL0"
    uint32_t tuner_type;      // network byte order
    uint32_t tuner_gain_count; // network byte order
} dongle_info_t;

// RTL-TCP command codes
#define RTLTCP_SET_FREQ           0x01
#define RTLTCP_SET_SAMPLE_RATE    0x02
#define RTLTCP_SET_GAIN_MODE      0x03
#define RTLTCP_SET_GAIN           0x04
#define RTLTCP_SET_FREQ_CORR      0x05
#define RTLTCP_SET_IF_GAIN        0x06
#define RTLTCP_SET_TEST_MODE      0x07
#define RTLTCP_SET_AGC_MODE       0x08
#define RTLTCP_SET_DIRECT_SAMP    0x09
#define RTLTCP_SET_OFFSET_TUNING  0x0A
#define RTLTCP_SET_RTL_CRYSTAL    0x0B
#define RTLTCP_SET_TUNER_CRYSTAL  0x0C
#define RTLTCP_SET_GAIN_BY_INDEX  0x0D
#define RTLTCP_SET_BIAS_TEE       0x0E

static struct {
    iq_convert_fn converter;
    struct converter_state *converter_state;
    rtlsdr_dev_t *dev;
    uint8_t *bounce_buffer;
    int ppm_error;
    bool digital_agc;
    bool use_rtl_agc;
    int numgains;
    int *gains;
    int curGain;
    int tunerAgcEnabled;
    // rtl_tcp TCP client support
    int rtl_tcp_socket;         // TCP socket fd
    pthread_t rtl_tcp_thread;   // Read thread
    volatile bool rtl_tcp_mode; // true if connected via TCP
    uint8_t *rtl_tcp_buffer;    // Buffer for TCP reads
    char *rtl_tcp_host;         // Host name for reconnect
    int rtl_tcp_port;           // Port for reconnect
    int direct_sampling;        // rtl_tcp: direct sampling mode
    int offset_tuning;          // rtl_tcp: offset tuning
    int bias_tee;               // rtl_tcp: bias tee (0 or 1)
} RTLSDR;

// Forward declarations for functions used before definition
static int rtltcp_send_command(int sock, unsigned char cmd, unsigned int param);
void rtlsdrCallback(unsigned char *buf, uint32_t len, void *ctx);

void rtlsdrInitConfig() {
    RTLSDR.dev = NULL;
    RTLSDR.digital_agc = false;
    RTLSDR.use_rtl_agc = false;
    RTLSDR.ppm_error = 0;
    RTLSDR.converter = NULL;
    RTLSDR.converter_state = NULL;
    RTLSDR.bounce_buffer = NULL;
    RTLSDR.numgains = 0;
    RTLSDR.gains = NULL;
    RTLSDR.tunerAgcEnabled = 0;
    // rtl_tcp initialization
    RTLSDR.rtl_tcp_socket = -1;
    RTLSDR.rtl_tcp_mode = false;
    RTLSDR.rtl_tcp_buffer = NULL;
    RTLSDR.rtl_tcp_host = NULL;
    RTLSDR.rtl_tcp_port = 1234;
    RTLSDR.direct_sampling = 0;
    RTLSDR.offset_tuning = 0;
    RTLSDR.bias_tee = 0;
}

static int getClosestGainIndex(int target) {
    target = (target == MODES_MAX_GAIN ? 9999 : target);
    int closest = 0;
    for (int i = 0; i < RTLSDR.numgains; ++i) {
        if (abs(RTLSDR.gains[i] - target) < abs(RTLSDR.gains[closest] - target)) {
            closest = i;
        }
    }
    return closest;
}

void rtlsdrSetGain(char *reason) {
    if (RTLSDR.rtl_tcp_mode) {
        int sock;
        pthread_mutex_lock(&Modes.sdrControlMutex);
        sock = RTLSDR.rtl_tcp_socket;
        pthread_mutex_unlock(&Modes.sdrControlMutex);
        if (sock < 0) return;
        if (Modes.gain < 0) Modes.gain = 0;
        if (Modes.gain == MODES_AUTO_GAIN || Modes.gain >= 520) {
            RTLSDR.tunerAgcEnabled = 1;
            if (!Modes.gainQuiet) fprintf(stderr, "%srtl_tcp: tuner gain set to automatic\n", reason);
            rtltcp_send_command(sock, RTLTCP_SET_GAIN_MODE, 0);
        } else {
            RTLSDR.tunerAgcEnabled = 0;
            if (!Modes.gainQuiet) fprintf(stderr, "%srtl_tcp: tuner gain set to %.1f dB\n", reason, Modes.gain / 10.0);
            rtltcp_send_command(sock, RTLTCP_SET_GAIN_MODE, 1);
            rtltcp_send_command(sock, RTLTCP_SET_GAIN, (unsigned int)Modes.gain);
        }
        return;
    }

    // USB mode
    if (RTLSDR.use_rtl_agc && (Modes.gain == MODES_AUTO_GAIN || Modes.gain >= 520)) {
        Modes.gain = MODES_RTL_AGC;
    }
    if (Modes.increaseGain || Modes.lowerGain) {
        int closest = getClosestGainIndex(Modes.gain);
        if (Modes.increaseGain) closest += Modes.increaseGain;
        if (Modes.lowerGain) closest -= Modes.lowerGain;
        if (closest >= RTLSDR.numgains) closest = RTLSDR.numgains - 1;
        if (closest < 0) closest = 0;
        Modes.increaseGain = 0;
        Modes.lowerGain = 0;
        if (RTLSDR.gains[closest] < Modes.minGain) return;
        if (Modes.gain == RTLSDR.gains[closest]) return;
        Modes.gain = RTLSDR.gains[closest];
    }
    if (Modes.gain < 0) Modes.gain = 0;
    if (RTLSDR.use_rtl_agc && Modes.gain == MODES_RTL_AGC) {
        RTLSDR.tunerAgcEnabled = 1;
        if (!Modes.gainQuiet) fprintf(stderr, "%srtlsdr: tuner gain set to 59.0 dB (tuner AGC)\n", reason);
        if (rtlsdr_set_tuner_gain_mode(RTLSDR.dev, 0)) { fprintf(stderr, "rtlsdr: enabling tuner AGC failed\n"); return; }
    } else {
        int closest = getClosestGainIndex(Modes.gain);
        int newGain = RTLSDR.gains[closest];
        if (RTLSDR.tunerAgcEnabled) {
            if (rtlsdr_set_tuner_gain_mode(RTLSDR.dev, 1)) { fprintf(stderr, "rtlsdr: disabling tuner AGC failed\n"); return; }
            RTLSDR.tunerAgcEnabled = 0;
            usleep(1000);
        }
        if (rtlsdr_set_tuner_gain(RTLSDR.dev, newGain)) { fprintf(stderr, "rtlsdr: setting tuner gain failed\n"); return; }
        else {
            if (!Modes.gainQuiet) fprintf(stderr, "%srtlsdr: tuner gain set to %4.1f dB\n", reason, newGain / 10.0);
            Modes.gain = newGain;
        }
    }
}

static void show_rtlsdr_devices() {
    int device_count = rtlsdr_get_device_count();
    fprintf(stderr, "rtlsdr: found %d device(s):\n", device_count);
    for (int i = 0; i < device_count; i++) {
        char vendor[256], product[256], serial[256];
        if (rtlsdr_get_device_usb_strings(i, vendor, product, serial) != 0)
            fprintf(stderr, "  %d:  unable to read device details\n", i);
        else
            fprintf(stderr, "  %d:  %s, %s, SN: %s\n", i, vendor, product, serial);
    }
}

static int find_device_index(char *s) {
    int device_count = rtlsdr_get_device_count();
    if (!device_count) return -1;
    if (!strcmp(s, "0")) return 0;
    if (s[0] != '0') {
        char *s2;
        int device = (int) strtol(s, &s2, 10);
        if (s2[0] == '\0' && device >= 0 && device < device_count) return device;
    }
    for (int i = 0; i < device_count; i++) {
        char serial[256];
        if (rtlsdr_get_device_usb_strings(i, NULL, NULL, serial) == 0 && !strcmp(s, serial)) return i;
    }
    for (int i = 0; i < device_count; i++) {
        char serial[256];
        if (rtlsdr_get_device_usb_strings(i, NULL, NULL, serial) == 0 && !strncmp(s, serial, strlen(s))) return i;
    }
    for (int i = 0; i < device_count; i++) {
        char serial[256];
        if (rtlsdr_get_device_usb_strings(i, NULL, NULL, serial) == 0 && strlen(s) < strlen(serial) && !strcmp(serial + strlen(serial) - strlen(s), s)) return i;
    }
    return -1;
}

bool rtlsdrHandleOption(int key, char *arg) {
    switch (key) {
        case OptRtlSdrEnableAgc: RTLSDR.digital_agc = true; break;
        case OptRtlSdrPpm: RTLSDR.ppm_error = atoi(arg); break;
        case OptRtlTcpDirectSamp: RTLSDR.direct_sampling = atoi(arg); break;
        case OptRtlTcpOffsetTune: RTLSDR.offset_tuning = atoi(arg); break;
        case OptRtlTcpBiasTee: RTLSDR.bias_tee = atoi(arg); break;
        default: return false;
    }
    return true;
}

// ======================== rtl_tcp client support ==========================

static bool rtltcp_parse_device(const char *dev_name, char **host, int *port) {
    *port = 1234;
    if (strncmp(dev_name, "rtl_tcp:", 8) != 0) return false;
    const char *p = dev_name + 8;
    const char *colon = strchr(p, ':');
    if (colon) {
        size_t host_len = colon - p;
        *host = strndup(p, host_len);
        *port = atoi(colon + 1);
    } else {
        *host = strdup(p);
    }
    return true;
}

static int rtltcp_send_command(int sock, unsigned char cmd, unsigned int param) {
    struct rtltcp_command command;
    command.cmd = cmd;
    command.param = htonl(param);
    ssize_t sent = send(sock, &command, sizeof(command), MSG_NOSIGNAL);
    return (sent == sizeof(command)) ? 0 : -1;
}

static void rtltcp_send_config(int sock) {
    rtltcp_send_command(sock, RTLTCP_SET_FREQ, (unsigned int)(Modes.freq));
    rtltcp_send_command(sock, RTLTCP_SET_SAMPLE_RATE, (unsigned int)(Modes.sample_rate));
    if (Modes.gain == MODES_AUTO_GAIN || Modes.gain >= 520) {
        rtltcp_send_command(sock, RTLTCP_SET_GAIN_MODE, 0);
    } else {
        rtltcp_send_command(sock, RTLTCP_SET_GAIN_MODE, 1);
        rtltcp_send_command(sock, RTLTCP_SET_GAIN, (unsigned int)(Modes.gain));
    }
    if (RTLSDR.ppm_error != 0)
        rtltcp_send_command(sock, RTLTCP_SET_FREQ_CORR, (unsigned int)(RTLSDR.ppm_error));
    if (RTLSDR.digital_agc)
        rtltcp_send_command(sock, RTLTCP_SET_AGC_MODE, 1);
    if (RTLSDR.direct_sampling != 0)
        rtltcp_send_command(sock, RTLTCP_SET_DIRECT_SAMP, (unsigned int)(RTLSDR.direct_sampling));
    if (RTLSDR.offset_tuning != 0)
        rtltcp_send_command(sock, RTLTCP_SET_OFFSET_TUNING, (unsigned int)(RTLSDR.offset_tuning));
    if (RTLSDR.bias_tee != 0)
        rtltcp_send_command(sock, RTLTCP_SET_BIAS_TEE, 1);
}

static bool rtltcp_do_connect(const char *host, int port) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    struct addrinfo hints, *res, *res0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_ADDRCONFIG;

    int ret = getaddrinfo(host, port_str, &hints, &res0);
    if (ret) {
        fprintf(stderr, "rtl_tcp: address lookup failed for %s: %s\n", host, gai_strerror(ret));
        return false;
    }
    int sock = -1;
    for (res = res0; res; res = res->ai_next) {
        sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock >= 0) {
            ret = connect(sock, res->ai_addr, res->ai_addrlen);
            if (ret == 0) break;
            close(sock);
            sock = -1;
        }
    }
    freeaddrinfo(res0);
    if (sock < 0) {
        fprintf(stderr, "rtl_tcp: connection failed to %s:%d\n", host, port);
        return false;
    }
    int one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    dongle_info_t info;
    ssize_t received = recv(sock, (char*)&info, sizeof(info), 0);
    if (received != (ssize_t)sizeof(info)) {
        fprintf(stderr, "rtl_tcp: failed to receive dongle info (got %zd bytes)\n", received);
        close(sock);
        return false;
    }
    if (strncmp(info.magic, "RTL0", 4) != 0) {
        fprintf(stderr, "rtl_tcp: invalid dongle magic\n");
        close(sock);
        return false;
    }
    RTLSDR.rtl_tcp_socket = sock;
    uint32_t tuner_number = ntohl(info.tuner_type);
    uint32_t gain_count = ntohl(info.tuner_gain_count);
    const char *tuner_names[] = {"Unknown", "E4000", "FC0012", "FC0013", "FC2580", "R820T", "R828D"};
    const char *tuner_name = (tuner_number <= 6) ? tuner_names[tuner_number] : "Invalid";
    fprintf(stderr, "rtl_tcp: connected to %s:%d (Tuner: %s, %u gain steps)\n",
            host, port, tuner_name, gain_count);
    return true;
}

static bool rtltcp_setup_gains(void) {
    static const int r820t_gains[] = {
        0, 9, 14, 27, 37, 77, 87, 125, 144, 157, 166, 197, 207, 229, 254,
        280, 297, 328, 338, 364, 372, 386, 402, 421, 434, 439, 445, 480, 496
    };
    RTLSDR.numgains = sizeof(r820t_gains) / sizeof(r820t_gains[0]);
    RTLSDR.gains = cmalloc((RTLSDR.numgains + 1) * sizeof(int));
    if (!RTLSDR.gains) { fprintf(stderr, "FATAL: rtl_tcp: can't allocate gains array\n"); return false; }
    memcpy(RTLSDR.gains, r820t_gains, RTLSDR.numgains * sizeof(int));
    RTLSDR.use_rtl_agc = true;
    RTLSDR.gains[RTLSDR.numgains] = MODES_RTL_AGC;
    RTLSDR.numgains++;
    return true;
}

static void *rtltcp_read_thread(void *arg) {
    MODES_NOTUSED(arg);
    if (!RTLSDR.rtl_tcp_buffer) {
        RTLSDR.rtl_tcp_buffer = cmalloc(Modes.sdr_buf_size);
        if (!RTLSDR.rtl_tcp_buffer) { fprintf(stderr, "FATAL: rtl_tcp: can't allocate TCP read buffer\n"); return NULL; }
    }
    while (!atomic_load(&Modes.exit)) {
        int sock;
        pthread_mutex_lock(&Modes.sdrControlMutex);
        sock = RTLSDR.rtl_tcp_socket;
        pthread_mutex_unlock(&Modes.sdrControlMutex);
        if (sock < 0) {
            // Socket not ready, sleep a bit
            usleep(100000);
            continue;
        }
        ssize_t received = recv(sock, (char*)RTLSDR.rtl_tcp_buffer, Modes.sdr_buf_size, MSG_WAITALL);
        if (received <= 0) {
            if (atomic_load(&Modes.exit)) break;
            fprintf(stderr, "rtl_tcp: %s\n", received == 0 ? "connection closed by server" : strerror(errno));
            pthread_mutex_lock(&Modes.sdrControlMutex);
            close(RTLSDR.rtl_tcp_socket);
            RTLSDR.rtl_tcp_socket = -1;
            pthread_mutex_unlock(&Modes.sdrControlMutex);
            while (!atomic_load(&Modes.exit)) {
                fprintf(stderr, "rtl_tcp: attempting to reconnect...\n");
                if (rtltcp_do_connect(RTLSDR.rtl_tcp_host, RTLSDR.rtl_tcp_port)) {
                    rtltcp_send_config(RTLSDR.rtl_tcp_socket);
                    fprintf(stderr, "rtl_tcp: reconnected successfully\n");
                    break;
                }
                fprintf(stderr, "rtl_tcp: reconnect failed, retrying in 5 seconds...\n");
                sleep(5);
            }
        } else {
            rtlsdrCallback(RTLSDR.rtl_tcp_buffer, (uint32_t)received, NULL);
        }
    }
    return NULL;
}

static bool rtlsdrOpenTcp(void) {
    char *host = NULL;
    int port = 1234;
    if (!rtltcp_parse_device(Modes.dev_name, &host, &port)) {
        fprintf(stderr, "FATAL: rtl_tcp: invalid device string '%s'\n", Modes.dev_name);
        return false;
    }
    RTLSDR.rtl_tcp_host = host;
    RTLSDR.rtl_tcp_port = port;
    fprintf(stderr, "rtl_tcp: connecting to server at %s:%d\n", host, port);
    if (!rtltcp_do_connect(host, port)) {
        free(host);
        RTLSDR.rtl_tcp_host = NULL;
        return false;
    }
    rtltcp_send_config(RTLSDR.rtl_tcp_socket);
    RTLSDR.rtl_tcp_mode = true;
    RTLSDR.dev = NULL;
    if (!rtltcp_setup_gains()) { rtlsdrClose(); return false; }
    rtlsdrSetGain("");
    if (RTLSDR.digital_agc) fprintf(stderr, "rtl_tcp: digital AGC enabled\n");
    if (RTLSDR.ppm_error) fprintf(stderr, "rtl_tcp: frequency correction set to %d ppm\n", RTLSDR.ppm_error);
    if (RTLSDR.direct_sampling) fprintf(stderr, "rtl_tcp: direct sampling mode set to %d\n", RTLSDR.direct_sampling);
    if (RTLSDR.offset_tuning) fprintf(stderr, "rtl_tcp: offset tuning enabled\n");
    if (RTLSDR.bias_tee) fprintf(stderr, "rtl_tcp: bias-T enabled\n");
    RTLSDR.converter = init_converter(INPUT_UC8, Modes.sample_rate, Modes.dc_filter, &RTLSDR.converter_state);
    if (!RTLSDR.converter) { fprintf(stderr, "FATAL: rtl_tcp: can't initialize sample converter\n"); rtlsdrClose(); return false; }
#ifdef USE_BOUNCE_BUFFER
    if (!(RTLSDR.bounce_buffer = cmalloc(Modes.sdr_buf_size))) {
        fprintf(stderr, "FATAL: rtl_tcp: can't allocate bounce buffer\n"); rtlsdrClose(); return false;
    }
#endif
    return true;
}

bool rtlsdrOpen(void) {
    if (Modes.dev_name && strncmp(Modes.dev_name, "rtl_tcp:", 8) == 0)
        return rtlsdrOpenTcp();

    // USB mode
    if (!rtlsdr_get_device_count()) { fprintf(stderr, "FATAL: rtlsdr: no supported devices found.\n"); return false; }
    int dev_index = 0;
    if (Modes.dev_name) {
        if ((dev_index = find_device_index(Modes.dev_name)) < 0) {
            fprintf(stderr, "FATAL: rtlsdr: no device matching '%s' found.\n", Modes.dev_name);
            show_rtlsdr_devices();
            return false;
        }
    }
    char manufacturer[256], product[256], serial[256];
    if (rtlsdr_get_device_usb_strings(dev_index, manufacturer, product, serial) < 0) {
        fprintf(stderr, "FATAL: rtlsdr: error querying device #%d: %s\n", dev_index, strerror(errno));
        return false;
    }
    fprintf(stderr, "rtlsdr: using device #%d: %s (%s, %s, SN %s)\n",
            dev_index, rtlsdr_get_device_name(dev_index), manufacturer, product, serial);
    if (rtlsdr_open(&RTLSDR.dev, dev_index) < 0) {
        fprintf(stderr, "FATAL: rtlsdr: error opening the RTLSDR device: %s\n", strerror(errno));
        return false;
    }
    RTLSDR.numgains = rtlsdr_get_tuner_gains(RTLSDR.dev, NULL);
    if (RTLSDR.numgains <= 0) { fprintf(stderr, "FATAL: rtlsdr: error getting tuner gains\n"); return false; }
    enum rtlsdr_tuner tuner = rtlsdr_get_tuner_type(RTLSDR.dev);
    if (tuner == RTLSDR_TUNER_FC2580 || tuner == RTLSDR_TUNER_FC0012 || tuner == RTLSDR_TUNER_FC0013) {
        Modes.bad_tuner = 1;
        fprintf(stderr, "\n\nrtlsdr: BAD TUNER, 1090 reception will be TERRIBLE, use another SDR\n\n\n");
    }
    RTLSDR.gains = cmalloc((RTLSDR.numgains + 1) * sizeof (int));
    if (rtlsdr_get_tuner_gains(RTLSDR.dev, RTLSDR.gains) != RTLSDR.numgains) {
        fprintf(stderr, "FATAL: rtlsdr: error getting tuner gains\n"); free(RTLSDR.gains); return false;
    }
    if (RTLSDR.numgains == 29) {
        RTLSDR.use_rtl_agc = true;
        RTLSDR.gains[RTLSDR.numgains] = MODES_RTL_AGC;
        RTLSDR.numgains++;
    }
    rtlsdrSetGain("");
    if (RTLSDR.digital_agc) {
        fprintf(stderr, "rtlsdr: enabling digital AGC\n");
        rtlsdr_set_agc_mode(RTLSDR.dev, 1);
    }
    rtlsdr_set_freq_correction(RTLSDR.dev, RTLSDR.ppm_error);
    rtlsdr_set_center_freq(RTLSDR.dev, Modes.freq);
    rtlsdr_set_sample_rate(RTLSDR.dev, (unsigned) Modes.sample_rate);
#ifdef ENABLE_RTLSDR_BIASTEE
    rtlsdr_set_bias_tee(RTLSDR.dev, Modes.biastee);
#endif
    rtlsdr_reset_buffer(RTLSDR.dev);
    RTLSDR.converter = init_converter(INPUT_UC8, Modes.sample_rate, Modes.dc_filter, &RTLSDR.converter_state);
    if (!RTLSDR.converter) { fprintf(stderr, "FATAL: rtlsdr: can't initialize sample converter\n"); rtlsdrClose(); return false; }
#ifdef USE_BOUNCE_BUFFER
    if (!(RTLSDR.bounce_buffer = cmalloc(Modes.sdr_buf_size))) {
        fprintf(stderr, "FATAL: rtlsdr: can't allocate bounce buffer\n"); rtlsdrClose(); return false;
    }
#endif
    return true;
}

static struct timespec rtlsdr_thread_cpu;

void rtlsdrCallback(unsigned char *buf, uint32_t len, void *ctx) {
    struct mag_buf *outbuf;
    struct mag_buf *lastbuf;
    uint32_t slen;
    unsigned next_free_buffer;
    unsigned free_bufs;
    int64_t block_duration;
    static int dropping = 0;
    static uint64_t sampleCounter = 0;
    static int antiSpam;
    static int antiSpam2;
    int64_t sysMicroseconds = mono_micro_seconds();
    int64_t sysTimestamp = mstime();
    if (0) {
        static int fail;
        if (fail++ % (35 * 20) == 0) { fprintf(stderr, "ignoring rtsdrCallback\n"); return; }
    }
    MODES_NOTUSED(ctx);
    lockReader();
    next_free_buffer = (Modes.first_free_buffer + 1) % MODES_MAG_BUFFERS;
    outbuf = &Modes.mag_buffers[Modes.first_free_buffer];
    lastbuf = &Modes.mag_buffers[(Modes.first_free_buffer + MODES_MAG_BUFFERS - 1) % MODES_MAG_BUFFERS];
    free_bufs = (Modes.first_filled_buffer - next_free_buffer + MODES_MAG_BUFFERS) % MODES_MAG_BUFFERS;
    unlockReader();
    if (len != Modes.sdr_buf_size) {
        static int64_t antiSpam;
        if (mstime() > antiSpam) {
            antiSpam = mstime() + 10 * SECONDS;
            fprintf(stderr, "weirdness: rtlsdr gave us a block with an unusual size (got %u bytes, expected %u bytes), suppressing this message for 10 seconds\n", (unsigned) len, (unsigned) Modes.sdr_buf_size);
        }
        if (len > Modes.sdr_buf_size) {
            unsigned discard = (len - Modes.sdr_buf_size + 1) / 2;
            outbuf->dropped += discard;
            buf += discard * 2;
            len -= discard * 2;
        }
    }
    slen = len / 2;
    if (free_bufs == 0 || (dropping && free_bufs < MODES_MAG_BUFFERS / 2)) {
        dropping = 1;
        outbuf->dropped += slen;
        sampleCounter += slen;
        wakeDecode();
        if (--antiSpam <= 0 && !Modes.exit) { fprintf(stderr, "FIFO dropped, suppressing this message for 30 seconds.\n"); antiSpam = 300; }
        return;
    }
    dropping = 0;
    outbuf->sampleTimestamp = sampleCounter * 12e6 / Modes.sample_rate;
    sampleCounter += slen;
    if (Modes.debug_sampleCounter && --antiSpam2 <= 0) { fprintf(stderr, "sampleTimestamp: %020llu\n", (unsigned long long) outbuf->sampleTimestamp); antiSpam2 = 3000; }
    block_duration = 1e3 * slen / Modes.sample_rate;
    outbuf->sysTimestamp = sysTimestamp;
    outbuf->sysMicroseconds = sysMicroseconds;
    outbuf->sysTimestamp -= block_duration;
    outbuf->sysMicroseconds -= block_duration * 1000;
    if (outbuf->dropped == 0) {
        memcpy(outbuf->data, lastbuf->data + lastbuf->length, Modes.trailing_samples * sizeof (uint16_t));
    } else {
        memset(outbuf->data, 0, Modes.trailing_samples * sizeof (uint16_t));
    }
#ifdef USE_BOUNCE_BUFFER
    memcpy(RTLSDR.bounce_buffer, buf, slen * 2);
    buf = RTLSDR.bounce_buffer;
#endif
    outbuf->length = slen;
    RTLSDR.converter(buf, &outbuf->data[Modes.trailing_samples], slen, RTLSDR.converter_state, &outbuf->mean_level, &outbuf->mean_power);
    lockReader();
    Modes.mag_buffers[next_free_buffer].dropped = 0;
    Modes.mag_buffers[next_free_buffer].length = 0;
    Modes.first_free_buffer = next_free_buffer;
    end_cpu_timing(&rtlsdr_thread_cpu, &Modes.reader_cpu_accumulator);
    start_cpu_timing(&rtlsdr_thread_cpu);
    wakeDecode();
    unlockReader();
}

void rtlsdrRun() {
    if (!RTLSDR.dev && !RTLSDR.rtl_tcp_mode) return;
    if (RTLSDR.rtl_tcp_mode) {
        start_cpu_timing(&rtlsdr_thread_cpu);
        pthread_create(&RTLSDR.rtl_tcp_thread, NULL, rtltcp_read_thread, NULL);
        pthread_join(RTLSDR.rtl_tcp_thread, NULL);
        end_cpu_timing(&rtlsdr_thread_cpu, &Modes.reader_cpu_accumulator);
        return;
    }
    start_cpu_timing(&rtlsdr_thread_cpu);
    rtlsdr_read_async(RTLSDR.dev, rtlsdrCallback, NULL, MODES_RTL_BUFFERS, Modes.sdr_buf_size);
    if (!Modes.exit) {
        fprintf(stderr,"FATAL: rtlsdr_read_async returned unexpectedly, probably lost the USB device, bailing out\n");
    }
}

void rtlsdrCancel() {
    if (RTLSDR.rtl_tcp_mode) {
        int sock;
        pthread_mutex_lock(&Modes.sdrControlMutex);
        sock = RTLSDR.rtl_tcp_socket;
        if (sock >= 0) shutdown(sock, SHUT_RDWR);
        pthread_mutex_unlock(&Modes.sdrControlMutex);
        return;
    }
    rtlsdr_cancel_async(RTLSDR.dev);
}

void rtlsdrClose() {
    if (RTLSDR.rtl_tcp_mode) {
        int sock = -1;
        pthread_mutex_lock(&Modes.sdrControlMutex);
        if (RTLSDR.rtl_tcp_socket >= 0) {
            sock = RTLSDR.rtl_tcp_socket;
            RTLSDR.rtl_tcp_socket = -1;
        }
        RTLSDR.rtl_tcp_mode = false;
        pthread_mutex_unlock(&Modes.sdrControlMutex);
        if (sock >= 0) {
            shutdown(sock, SHUT_RDWR);
            close(sock);
        }
    }
    if (RTLSDR.dev) {
        rtlsdr_close(RTLSDR.dev);
        RTLSDR.dev = NULL;
    }
}
        RTLSDR.rtl_tcp_mode = false;
    }
    if (RTLSDR.dev) {
        rtlsdr_close(RTLSDR.dev);
        RTLSDR.dev = NULL;
    }
    if (RTLSDR.converter) {
        cleanup_converter(&RTLSDR.converter_state);
        RTLSDR.converter = NULL;
    }
    free(RTLSDR.gains);
    RTLSDR.gains = NULL;
    free(RTLSDR.bounce_buffer);
    RTLSDR.bounce_buffer = NULL;
    free(RTLSDR.rtl_tcp_buffer);
    RTLSDR.rtl_tcp_buffer = NULL;
    free(RTLSDR.rtl_tcp_host);
    RTLSDR.rtl_tcp_host = NULL;
}
    if (RTLSDR.dev) {
        rtlsdr_close(RTLSDR.dev);
        RTLSDR.dev = NULL;
    }
    if (RTLSDR.converter) {
        cleanup_converter(&RTLSDR.converter_state);
        RTLSDR.converter = NULL;
    }
    free(RTLSDR.gains);
    RTLSDR.gains = NULL;
    free(RTLSDR.bounce_buffer);
    RTLSDR.bounce_buffer = NULL;
    free(RTLSDR.rtl_tcp_buffer);
    RTLSDR.rtl_tcp_buffer = NULL;
    free(RTLSDR.rtl_tcp_host);
    RTLSDR.rtl_tcp_host = NULL;
}
