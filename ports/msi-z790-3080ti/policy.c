/* SPDX-License-Identifier: GPL-2.0-only. Bounds for observation, never permission to unlock. */
#include "observer.h"
BOOLEAN PortConfigValid(const UINT32 c[64],UINTN s,UINTN b,UINTN d,UINTN f) {
  return c && !s && b==1 && !d && !f && c[0]==PORT_PCI_ID && c[11]==PORT_SUBSYSTEM &&
    ((c[2]>>24)&255)==3 && (c[2]&255)==0xa1 && ((c[3]>>16)&0x7f)==0 &&
    (c[1]&2)!=0 && (c[4]&1)==0 && ((c[4]>>1)&3)!=1 && ((c[4]>>1)&3)!=3;
}
BOOLEAN PortSameResource(const UINT32 a[64],const UINT32 b[64]) {
  return a && b && a[0]==b[0] && a[1]==b[1] && a[2]==b[2] && a[3]==b[3] &&
    a[4]==b[4] && a[5]==b[5] && a[11]==b[11];
}
BOOLEAN PortBarValid(const UINT32 c[64],const EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *r) {
  if(!c || !r || r->Desc!=ACPI_ADDRESS_SPACE_DESCRIPTOR || r->Len!=0x2b ||
     r->ResType!=ACPI_ADDRESS_SPACE_TYPE_MEM || (c[4]&1))return FALSE;
  UINT32 type=(c[4]>>1)&3;
  if(type!=0 && type!=2)return FALSE;
  UINT64 base=c[4]&~0xfU;if(type==2)base|=(UINT64)c[5]<<32;
  return base && r->AddrRangeMin==base && r->AddrLen>=0x1000000 &&
    r->AddrLen<=0x10000000 && r->AddrRangeMin<=~(UINT64)0-(r->AddrLen-1) &&
    r->AddrRangeMax==base+r->AddrLen-1 && r->AddrTranslationOffset==0;
}
EFI_STATUS PortOpenNew(EFI_FILE_PROTOCOL *dir,const CHAR16 *name,EFI_FILE_PROTOCOL **out) {
  *out=NULL;EFI_FILE_PROTOCOL *existing=NULL;
  EFI_STATUS s=dir->Open(dir,&existing,(CHAR16 *)name,EFI_FILE_MODE_READ,0);
  if(s==EFI_SUCCESS){if(existing)existing->Close(existing);return EFI_ALREADY_STARTED;}
  if(s!=EFI_NOT_FOUND)return s;
  return dir->Open(dir,out,(CHAR16 *)name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
}
