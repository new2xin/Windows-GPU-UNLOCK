/* SPDX-License-Identifier: GPL-2.0-only. Local diagnostic port; no write eligibility. */
#pragma once
#include <Uefi.h>
#include <Protocol/PciIo.h>
#include <IndustryStandard/Acpi.h>
#include "../../internal-boot/boot/diagnostic.h"
#if defined(GU_EXPERIMENTAL_3090) || defined(GU_FULL_UNLOCK) || defined(GU_AUTOMATIC_BOOT)
#error This unverified port only supports observation; no unlock build is available
#endif
#define PORT_PCI_ID 0x220810deU
#define PORT_SUBSYSTEM 0x22081569U
#define PORT_MAX_IMAGES 96U
#define PORT_MAX_IMAGE_BYTES (32U*1024U*1024U)
VOID Text(CAPTURE *C,const char *S);
VOID Hex(CAPTURE *C,UINT64 V,UINTN Digits);
VOID End(CAPTURE *C);
VOID Problem(CAPTURE *C,const char *Where,EFI_STATUS Status);
EFI_STATUS PortOpenNew(EFI_FILE_PROTOCOL *Directory,const CHAR16 *Name,EFI_FILE_PROTOCOL **File);
BOOLEAN PortConfigValid(const UINT32 Cfg[64],UINTN Segment,UINTN Bus,UINTN Device,UINTN Function);
BOOLEAN PortBarValid(const UINT32 Cfg[64],const EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *Bar);
BOOLEAN PortSameResource(const UINT32 A[64],const UINT32 B[64]);
VOID PortSetDirectory(EFI_FILE_PROTOCOL *Directory);
EFI_STATUS GuProbeGpu(EFI_SYSTEM_TABLE *Table,CAPTURE *Capture);
