/* SPDX-License-Identifier: GPL-2.0-only. Regression for the actual stale graph
 * snapshot: the probe dispatches events before the graph starts enumerating.
 * Simulated services only; no firmware or GPU access is available. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include "../../internal-boot/boot/diagnostic.c"
static int ProbeFinished,Enumerations,Checks;
static char Output[16384];static UINTN OutputUsed;
static EFI_GUID DummyProtocol={0};static EFI_GUID *Protocols[]={&DummyProtocol};
static void Check(int condition,const char *message){Checks++;if(!condition){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
EFI_STATUS GuProbeGpu(EFI_SYSTEM_TABLE *t,CAPTURE *c){(void)t;(void)c;ProbeFinished=1;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI MissingVariable(CHAR16 *name,EFI_GUID *guid,UINT32 *attributes,UINTN *size,VOID *data){(void)name;(void)guid;(void)attributes;(void)size;(void)data;return EFI_NOT_FOUND;}
static EFI_STATUS EFIAPI Locate(EFI_LOCATE_SEARCH_TYPE type,EFI_GUID *guid,VOID *key,UINTN *count,EFI_HANDLE **handles) {
  (void)guid;(void)key;Check(type==AllHandles,"graph only enumerates all handles");Enumerations++;
  *count=ProbeFinished?1:4;*handles=malloc(*count*sizeof(**handles));Check(*handles!=NULL,"allocate fixture snapshot");
  for(UINTN i=0;i<*count;i++)(*handles)[i]=(VOID *)(i+1);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI PerHandle(EFI_HANDLE handle,EFI_GUID ***protocols,UINTN *count) {
  if(handle!=(VOID *)1)return EFI_INVALID_PARAMETER;
  *count=1;*protocols=malloc(sizeof(**protocols));Check(*protocols!=NULL,"allocate fixture protocol list");(*protocols)[0]=Protocols[0];return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI OpenInfo(EFI_HANDLE handle,EFI_GUID *guid,EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **entries,UINTN *count){(void)handle;(void)guid;*entries=NULL;*count=0;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Free(VOID *memory){free(memory);return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Write(EFI_FILE_PROTOCOL *file,UINTN *bytes,VOID *data){(void)file;Check(*bytes<sizeof(Output)-OutputUsed,"bounded diagnostic output");memcpy(Output+OutputUsed,data,*bytes);OutputUsed+=*bytes;Output[OutputUsed]=0;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI FlushFile(EFI_FILE_PROTOCOL *file){(void)file;return EFI_SUCCESS;}
int main(void) {
  EFI_BOOT_SERVICES bs={0};EFI_RUNTIME_SERVICES rt={0};EFI_SYSTEM_TABLE table={0};EFI_FILE_PROTOCOL file={0};
  bs.LocateHandleBuffer=Locate;bs.ProtocolsPerHandle=PerHandle;bs.OpenProtocolInformation=OpenInfo;bs.FreePool=Free;
  rt.GetVariable=MissingVariable;table.BootServices=&bs;table.RuntimeServices=&rt;file.Write=Write;file.Flush=FlushFile;
  Check(GuCaptureGraph(&table,&file)==EFI_SUCCESS,"graph starts with the post-probe handle set");
  Check(ProbeFinished&&Enumerations==2,"probe followed by start/end snapshot");
  Check(strstr(Output,"capture-complete=1\r\n")&&!strstr(Output,"incomplete reason="),"transient handles removed by probe never enter graph");
  Check(!bs.DisconnectController&&!rt.SetVariable,"no write/detach services supplied");
  printf("PASS %d graph snapshot assertions; probe-time transient handle removal; hardware access unavailable\n",Checks);return 0;
}
