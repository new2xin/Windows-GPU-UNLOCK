/* SPDX-License-Identifier: GPL-2.0-only. Production policies, read-only entry. */
#include "observer.h"
#include "../../internal-boot/boot/full-release.h"
static EFI_STATUS ReviewLog(VOID *context,const char *event,EFI_STATUS status) {
  CAPTURE *c=context;Text(c,"port-plan event=");Text(c,event);
  Text(c," status=0x");Hex(c,status,16);End(c);return c->Io;
}
EFI_STATUS PortReviewPlan(EFI_SYSTEM_TABLE *table,CAPTURE *capture) {
  return GuReviewReleasePlan(table,ReviewLog,capture);
}
