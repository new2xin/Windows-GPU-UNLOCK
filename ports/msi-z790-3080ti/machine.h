/* SPDX-License-Identifier: GPL-2.0-only. Exact captured machine, experimental port.
 * Full capture sha256 and loaded-code fingerprints are checked in host replay.
 * No addresses of EFI handles are compiled into the live application. */
#pragma once
#if defined(GU_EXPERIMENTAL_3090) || defined(GU_FULL_UNLOCK)
#if !defined(GU_MSI_EXPERIMENTAL_PORT) || !defined(GU_HEADLESS_RESEARCH) || !defined(GU_USB_MEDIA) || !defined(GU_AUTOMATIC_BOOT)
#error MSI 3080 Ti write builds require explicit experimental USB headless selection
#endif
#if defined(GU_EXPERIMENTAL_3090) && (!defined(GU_ENTRY_LATCHED_SS1_FULL) || !defined(GU_INTERNAL_BOOT))
#error MSI 3080 Ti child requires immutable entry-latched full selectors
#endif
#if defined(GU_FULL_UNLOCK) && !defined(GU_SINGLE_RTX_CONSOLE)
#error MSI 3080 Ti wrapper requires the reviewed single-RTX console policy
#endif
#endif
#define GU_PROFILE_NAME "MSI-PRO-Z790-P-WIFI-DDR4-1.I0-PALIT-RTX3080TI"
#define GU_BOARD_ID 0x220810deU
#define GU_BOARD_SUBSYSTEM 0x22081569U
#define GU_BAR0_BASE 0x81000000ULL
#define GU_BAR0_END 0x81ffffffULL
#define GU_DEVICE_MEMORY_SIZE 0x3000U
#define GU_CHIP_ID 0xb72000a1U
#define GU_PROM_SHA256 "81915bf4d2ee2ca8c6069857b97a702d8269dbbb3767e51ec985f4ac5cf4047a"
#define GU_ROOT_PORT_DEVICE 1U
#define GU_ROOT_PORT_FUNCTION 0U
#define GU_RTX_PATH_BYTES \
  2,1,12,0,0xd0,0x41,3,10,0,0,0,0,1,1,6,0,0,1,1,1,6,0,0,0,2,3,8,0,32,3,1,128,127,255,4,0
#define GU_CONSOLE_TEXT_SIZE 0x6420U
#define GU_CONSOLE_TEXT_STOP 0x1584U
#define GU_CONSOLE_TEXT_VERSION 0x10U
#define GU_CONSOLE_TEXT_RVA 0x280U
#define GU_CONSOLE_TEXT_BYTES 0x5178U
#define GU_CONSOLE_TEXT_SHA256 "4758c6be70c7f455de499aefb045fb9fc96c75ab64b1c0e69beac04c4446cc22"
#define GU_CONSOLE_GFX_SIZE 0x3ba0U
#define GU_CONSOLE_GFX_STOP 0xcb0U
#define GU_CONSOLE_GFX_VERSION 0x10U
#define GU_CONSOLE_GFX_RVA 0x280U
#define GU_CONSOLE_GFX_BYTES 0x1db2U
#define GU_CONSOLE_GFX_SHA256 "488441b6a16fc302206fd84d5c1745899d4020b4a903233ba4aead3299fb1aa4"
#define GU_NVGOP_VERSION 0x6000aU
#define GU_NVGOP_IMAGE_SIZE 0x2be40U
#define GU_NVGOP_STOP 0x698U
#define GU_NVGOP_TEXT_BYTES 0x24426U
#define GU_NVGOP_TEXT_SHA256 "8924ba488419e5d82b524d10e8dd63bfe886f077026fae1a9cc03e25b920b276"
#define GU_EDID_CONSUMER_REQUIRED 0
/* No runtime image is accepted in the zero-owner branch. These constants keep
 * the existing two-profile helper compilable; Profile(mouse) rejects it first. */
#define GU_EDID_IMAGE_SIZE 0U
#define GU_EDID_STOP 0U
#define GU_EDID_TEXT_BYTES 0U
#define GU_EDID_TEXT_SHA256 ""
