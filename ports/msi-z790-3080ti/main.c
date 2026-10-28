/* SPDX-License-Identifier: GPL-2.0-only. Standalone observer, then the pinned Windows ESP.
 * Adds new files on its own launch volume only. Never executes an unlock child. */
#include "observer.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/DevicePath.h>
#include <Guid/FileSystemInfo.h>
#include "platform-config.h"
static EFI_GUID LoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID,FsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID,FsInfoGuid=EFI_FILE_SYSTEM_INFO_ID;
static const EFI_GUID WindowsGuid=GU_WINDOWS_PARTITION_INITIALIZER;
static VOID Say(EFI_SYSTEM_TABLE *t,const CHAR16 *s){if(t->ConOut&&t->ConOut->OutputString)t->ConOut->OutputString(t->ConOut,(CHAR16 *)s);}
static VOID SayValue(EFI_SYSTEM_TABLE *t,const CHAR16 *label,UINT64 value) {
  CHAR16 hex[19]=L"0x0000000000000000";
  for(UINTN i=0;i<16;i++){UINTN digit=(UINTN)((value>>(4*(15-i)))&15);hex[2+i]=(CHAR16)(digit<10?'0'+digit:'a'+digit-10);}
  Say(t,label);Say(t,hex);Say(t,L"\r\n");
}
static EFI_STATUS Trace(EFI_FILE_PROTOCOL *file,const char *label,UINT64 value) {
  char line[128];UINTN length=0;
  while(*label&&length<100)line[length++]=*label++;
  if(*label)return EFI_BAD_BUFFER_SIZE;
  line[length++]='=';line[length++]='0';line[length++]='x';
  for(UINTN i=0;i<16;i++){UINTN digit=(UINTN)((value>>(4*(15-i)))&15);line[length++]=(char)(digit<10?'0'+digit:'a'+digit-10);}
  line[length++]='\r';line[length++]='\n';UINTN written=length;
  EFI_STATUS s=file->Write(file,&written,line);
  if(s!=EFI_SUCCESS)return s;if(written!=length)return EFI_DEVICE_ERROR;
  return file->Flush(file);
}
static BOOLEAN Equal(const VOID *a,const VOID *b){for(UINTN i=0;i<16;i++)if(((const UINT8 *)a)[i]!=((const UINT8 *)b)[i])return FALSE;return TRUE;}
/* Firmware pointers are trusted; traversal is bounded to 4 KiB/64 nodes. */
static BOOLEAN Inspect(const EFI_DEVICE_PATH_PROTOCOL *p,UINTN *size,BOOLEAN *windows) {
  UINTN offset=0,hd=0;BOOLEAN match=FALSE;
  if(!p)return FALSE;
  for(UINTN i=0;i<64 && offset+4<=4096;i++) {
    UINTN n=p->Length[0]+((UINTN)p->Length[1]<<8);
    if(n<4||n>4096-offset)return FALSE;
    if(p->Type==END_DEVICE_PATH_TYPE) {
      if(n!=4||p->SubType!=END_ENTIRE_DEVICE_PATH_SUBTYPE)return FALSE;
      if(size)*size=offset+4;if(windows)*windows=match;return TRUE;
    }
    if(p->Type==MEDIA_DEVICE_PATH&&p->SubType==MEDIA_HARDDRIVE_DP) {
      if(n!=sizeof(HARDDRIVE_DEVICE_PATH)||++hd!=1)return FALSE;
      const HARDDRIVE_DEVICE_PATH *h=(const VOID *)p;
      match=h->MBRType==2&&h->SignatureType==2&&h->PartitionSize&&Equal(h->Signature,&WindowsGuid);
    }
    offset+=n;p=(const VOID *)((const UINT8 *)p+n);
  }
  return FALSE;
}
static EFI_STATUS Windows(EFI_HANDLE self,EFI_SYSTEM_TABLE *t) {
  EFI_BOOT_SERVICES *bs=t->BootServices;EFI_HANDLE *volumes=NULL,device=NULL;UINTN count=0,matches=0;
  EFI_STATUS s=bs->LocateHandleBuffer(ByProtocol,&FsGuid,NULL,&count,&volumes);
  if(s!=EFI_SUCCESS)return s;
  if(count>4096){if(volumes)bs->FreePool(volumes);return EFI_BAD_BUFFER_SIZE;}
  for(UINTN i=0;i<count;i++) {
    EFI_DEVICE_PATH_PROTOCOL *p=NULL;BOOLEAN match=FALSE;
    if(bs->HandleProtocol(volumes[i],&PathGuid,(VOID **)&p)==EFI_SUCCESS&&Inspect(p,NULL,&match)&&match){device=volumes[i];matches++;}
  }
  if(volumes)bs->FreePool(volumes);if(matches!=1)return EFI_NOT_FOUND;
  EFI_DEVICE_PATH_PROTOCOL *base=NULL;UINTN baseBytes=0;
  s=bs->HandleProtocol(device,&PathGuid,(VOID **)&base);
  if(s!=EFI_SUCCESS||!Inspect(base,&baseBytes,NULL))return EFI_UNSUPPORTED;
  static const CHAR16 name[]=L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
  UINTN fileBytes=4+sizeof(name),total=baseBytes-4+fileBytes+4;UINT8 *data=NULL;
  s=bs->AllocatePool(EfiBootServicesData,total,(VOID **)&data);
  if(s!=EFI_SUCCESS||!data)return EFI_OUT_OF_RESOURCES;
  for(UINTN i=0;i<baseBytes-4;i++)data[i]=((const UINT8 *)base)[i];
  EFI_DEVICE_PATH_PROTOCOL *node=(VOID *)(data+baseBytes-4);
  node->Type=MEDIA_DEVICE_PATH;node->SubType=MEDIA_FILEPATH_DP;node->Length[0]=(UINT8)fileBytes;node->Length[1]=(UINT8)(fileBytes>>8);
  for(UINTN i=0;i<sizeof(name);i++)((UINT8 *)node)[4+i]=((const UINT8 *)name)[i];
  EFI_DEVICE_PATH_PROTOCOL *end=(VOID *)((UINT8 *)node+fileBytes);
  end->Type=END_DEVICE_PATH_TYPE;end->SubType=END_ENTIRE_DEVICE_PATH_SUBTYPE;end->Length[0]=4;end->Length[1]=0;
  EFI_HANDLE image=NULL;s=bs->LoadImage(FALSE,self,(VOID *)data,NULL,0,&image);bs->FreePool(data);
  if(s!=EFI_SUCCESS)return s;
  Say(t,L"Starting the original Windows Boot Manager.\r\n");
  return bs->StartImage(image,NULL,NULL);
}
static EFI_STATUS NewRun(EFI_FILE_PROTOCOL *root,EFI_FILE_PROTOCOL **run) {
  EFI_FILE_PROTOCOL *parent=NULL;
  EFI_STATUS s=root->Open(root,&parent,L"GPU-PORT",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
  if(s!=EFI_SUCCESS||!parent)return EFI_NOT_READY;
  for(UINTN i=0;i<1000;i++) {
    CHAR16 name[]=L"RUN000";name[3]=(CHAR16)('0'+i/100);name[4]=(CHAR16)('0'+(i/10)%10);name[5]=(CHAR16)('0'+i%10);
    EFI_FILE_PROTOCOL *existing=NULL;s=parent->Open(parent,&existing,name,EFI_FILE_MODE_READ,0);
    if(s==EFI_SUCCESS){if(existing)existing->Close(existing);continue;}
    if(s!=EFI_NOT_FOUND){parent->Close(parent);return s;}
    s=parent->Open(parent,run,name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
    parent->Close(parent);return s;
  }
  parent->Close(parent);return EFI_VOLUME_FULL;
}
static EFI_STATUS Capture(EFI_HANDLE self,EFI_SYSTEM_TABLE *t) {
  EFI_LOADED_IMAGE_PROTOCOL *loaded=NULL;EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;
  EFI_STATUS s=t->BootServices->HandleProtocol(self,&LoadedGuid,(VOID **)&loaded);
  SayValue(t,L"Loaded image status: ",s);
  if(s!=EFI_SUCCESS||!loaded)return EFI_UNSUPPORTED;
  s=t->BootServices->HandleProtocol(loaded->DeviceHandle,&FsGuid,(VOID **)&fs);
  SayValue(t,L"Launch filesystem status: ",s);
  if(s!=EFI_SUCCESS||!fs)return EFI_UNSUPPORTED;
  EFI_FILE_PROTOCOL *root=NULL,*run=NULL,*log=NULL,*trace=NULL;s=fs->OpenVolume(fs,&root);
  SayValue(t,L"Open volume status: ",s);
  if(s!=EFI_SUCCESS||!root)return EFI_NOT_READY;
  union {UINT64 align;UINT8 bytes[1024];} info;UINTN bytes=sizeof(info);
  s=root->GetInfo(root,&FsInfoGuid,&bytes,&info);
  SayValue(t,L"Filesystem GetInfo status: ",s);SayValue(t,L"Filesystem info bytes: ",bytes);
  if(s==EFI_SUCCESS && bytes>=SIZE_OF_EFI_FILE_SYSTEM_INFO && bytes<=sizeof(info)) {
    EFI_FILE_SYSTEM_INFO *fi=(VOID *)&info;
    SayValue(t,L"Filesystem free bytes: ",fi->FreeSpace);SayValue(t,L"Filesystem read-only: ",fi->ReadOnly);
    if(fi->Size>bytes||fi->Size<SIZE_OF_EFI_FILE_SYSTEM_INFO||fi->ReadOnly||fi->FreeSpace<48U*1024U*1024U)s=EFI_VOLUME_FULL;
  } else s=EFI_NOT_READY;
  if(s==EFI_SUCCESS){s=NewRun(root,&run);SayValue(t,L"Create new run status: ",s);}
  if(s==EFI_SUCCESS)s=PortOpenNew(run,L"BOOTTRACE.TXT",&trace);
  if(s==EFI_SUCCESS&&!trace)s=EFI_DEVICE_ERROR;
  if(s==EFI_SUCCESS)s=Trace(trace,"observer-version",4);
  if(s==EFI_SUCCESS)s=Trace(trace,"entry-reached",1);
  if(s==EFI_SUCCESS)s=Trace(trace,"filesystem-free-bytes",((EFI_FILE_SYSTEM_INFO *)&info)->FreeSpace);
  if(s==EFI_SUCCESS) {
    s=PortOpenNew(run,L"CAPTURE.TXT",&log);if(s==EFI_SUCCESS&&!log)s=EFI_DEVICE_ERROR;
    SayValue(t,L"Open capture log status: ",s);
    EFI_STATUS recorded=Trace(trace,"capture-log-open-status",s);if(s==EFI_SUCCESS)s=recorded;
  }
  if(s==EFI_SUCCESS&&log) {
    PortSetDirectory(run);s=GuCaptureGraph(t,log);PortSetDirectory(NULL);
    EFI_STATUS recorded=Trace(trace,"capture-graph-status",s);if(s==EFI_SUCCESS)s=recorded;
  }
  if(log){EFI_STATUS closed=log->Close(log);if(s==EFI_SUCCESS)s=closed;}
  if(trace){EFI_STATUS closed=trace->Close(trace);if(s==EFI_SUCCESS)s=closed;}
  if(run)run->Close(run);root->Close(root);return s;
}
EFI_STATUS EFIAPI efi_main(EFI_HANDLE self,EFI_SYSTEM_TABLE *t) {
  if(!t||!t->BootServices||!t->RuntimeServices)return EFI_INVALID_PARAMETER;
  Say(t,L"RTX 3080 Ti - READ ONLY DIAGNOSTIC v4\r\nNo unlock or detach. Diagnostic starts in 5 seconds.\r\n");
  if(t->BootServices->Stall)t->BootServices->Stall(5000000);
  EFI_STATUS s=t->BootServices->SetWatchdogTimer(0,0,0,NULL);
  SayValue(t,L"Watchdog disable status: ",s);
  if(s==EFI_SUCCESS)s=Capture(self,t);
  SayValue(t,L"Final capture status: ",s);
  Say(t,s==EFI_SUCCESS?L"Capture complete.\r\n":L"Capture incomplete; keep the files for analysis. GPU state was not changed.\r\n");
  Say(t,L"Returning to Windows in 10 seconds. Photograph these status lines if capture failed.\r\n");
  if(t->BootServices->Stall)t->BootServices->Stall(10000000);
  s=Windows(self,t);
  Say(t,L"Windows returned or could not be started. Select Windows Boot Manager in the firmware boot menu.\r\n");
  return s;
}
