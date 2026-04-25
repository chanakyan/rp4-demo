// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// tusb_config.h — TinyUSB configuration for rp4 bare-metal RPi4
// USB Host mode with Audio Class support.

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

// Board / MCU
#define CFG_TUSB_MCU         OPT_MCU_BCM2711
#define CFG_TUSB_OS          OPT_OS_NONE
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN   __attribute__((aligned(4)))

// USB Host
#define CFG_TUH_ENABLED      1
#define CFG_TUH_MAX_SPEED    OPT_MODE_HIGH_SPEED
#define CFG_TUH_ENUMERATION_BUFSIZE 256

// Host class drivers
#define CFG_TUH_AUDIO        1
#define CFG_TUH_AUDIO_FUNC_NUM 1

// Host HCD (xHCI for RPi4 VL805)
#define CFG_TUH_MAX_PORTS    4

// No device mode
#define CFG_TUD_ENABLED      0

#endif
