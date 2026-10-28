/* GPL-2.0-only. Full-release preparation; original unlock image remains separate. */
#pragma once
#include "prepare-console.h"
EFI_STATUS GuReleaseGop(EFI_SYSTEM_TABLE *Table,EFI_HANDLE Image,GU_PREPARE_LOG Log,VOID *Context);
EFI_STATUS GuVerifyReleased(EFI_SYSTEM_TABLE *Table,BOOLEAN CheckCold);
EFI_STATUS GuPeekCold(EFI_SYSTEM_TABLE *Table,UINT32 *Values);
#if defined(GU_PORT_PLAN_REVIEW) || (defined(GU_PROFILE_MSI_Z790_3080TI) && defined(GU_FULL_UNLOCK))
/* Read-only projection. Never calls prepare/release and clears all authority. */
EFI_STATUS GuReviewReleasePlan(EFI_SYSTEM_TABLE *Table,GU_PREPARE_LOG Log,VOID *Context);
#endif
typedef struct {
  const char *Stage;
  EFI_STATUS Status;
  BOOLEAN ConfigDiff;
  UINT32 Offset,Expected,Observed;
} GU_VERIFY_DIAGNOSTIC;
const GU_VERIFY_DIAGNOSTIC *GuLastVerifyDiagnostic(VOID);
