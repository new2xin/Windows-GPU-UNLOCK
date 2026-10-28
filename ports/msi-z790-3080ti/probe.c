/* SPDX-License-Identifier: GPL-2.0-only. Read-only PCI/MMIO/PROM and loaded EFI images.
 * No Attributes, Map, Unmap, AllocateBuffer, Write, DisconnectController or reset APIs. */
#include "observer.h"
#include <Protocol/DriverBinding.h>
#include <Protocol/LoadedImage.h>
#include "../../internal-boot/unlock/sha256.h"
static EFI_FILE_PROTOCOL *OutputDirectory;
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID,BindGuid=EFI_DRIVER_BINDING_PROTOCOL_GUID;
static EFI_GUID ImageGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
VOID PortSetDirectory(EFI_FILE_PROTOCOL *d){OutputDirectory=d;}
static VOID Value(CAPTURE *c,const char *label,UINT64 v){Text(c,label);Hex(c,v,16);End(c);}
static VOID Hash(CAPTURE *c,const char *label,const VOID *p,UINTN bytes) {
  UINT8 digest[32];GuSha256(p,bytes,digest);Text(c,label);
  for(UINTN i=0;i<32;i++)Hex(c,digest[i],2);End(c);
}
static EFI_STATUS Save(CAPTURE *c,const CHAR16 *name,VOID *data,UINTN size) {
  EFI_FILE_PROTOCOL *f=NULL;
  EFI_STATUS s=OutputDirectory?PortOpenNew(OutputDirectory,name,&f):EFI_NOT_READY;
  if(s!=EFI_SUCCESS||!f){Problem(c,"dump-open-new",s);return s;}
  UINTN written=size;s=f->Write(f,&written,data);
  if(s==EFI_SUCCESS && written!=size)s=EFI_DEVICE_ERROR;
  if(s==EFI_SUCCESS)s=f->Flush(f);
  EFI_STATUS closed=f->Close(f);if(s==EFI_SUCCESS)s=closed;
  if(s!=EFI_SUCCESS)Problem(c,"dump-write",s);return s;
}
static VOID Images(EFI_SYSTEM_TABLE *t,CAPTURE *c) {
  EFI_HANDLE *bindings=NULL;UINTN count=0;
  EFI_STATUS s=t->BootServices->LocateHandleBuffer(ByProtocol,&BindGuid,NULL,&count,&bindings);
  if(s!=EFI_SUCCESS||!bindings||count>4096){Problem(c,"binding-enumeration",s);if(bindings)t->BootServices->FreePool(bindings);return;}
  EFI_HANDLE seen[PORT_MAX_IMAGES];UINTN used=0,total=0;
  for(UINTN i=0;i<count && c->Io==EFI_SUCCESS;i++) {
    EFI_DRIVER_BINDING_PROTOCOL *binding=NULL;EFI_LOADED_IMAGE_PROTOCOL *image=NULL;
    if(t->BootServices->HandleProtocol(bindings[i],&BindGuid,(VOID **)&binding)!=EFI_SUCCESS||!binding)continue;
    Text(c,"port-binding handle=0x");Hex(c,(UINTN)bindings[i],16);
    Text(c," image=0x");Hex(c,(UINTN)binding->ImageHandle,16);
    Text(c," version=0x");Hex(c,binding->Version,8);Text(c," stop=0x");Hex(c,(UINTN)binding->Stop,16);End(c);
    BOOLEAN duplicate=FALSE;for(UINTN j=0;j<used;j++)if(seen[j]==binding->ImageHandle)duplicate=TRUE;
    if(duplicate||!binding->ImageHandle)continue;
    if(used==PORT_MAX_IMAGES){Problem(c,"image-count-limit",EFI_VOLUME_FULL);break;}
    s=t->BootServices->HandleProtocol(binding->ImageHandle,&ImageGuid,(VOID **)&image);
    if(s!=EFI_SUCCESS||!image||!image->ImageBase||!image->ImageSize||image->ImageSize>4U*1024U*1024U){Problem(c,"driver-image",EFI_UNSUPPORTED);continue;}
    if(image->ImageSize>PORT_MAX_IMAGE_BYTES-total){Problem(c,"image-byte-limit",EFI_VOLUME_FULL);break;}
    seen[used]=binding->ImageHandle;
    CHAR16 name[]=L"IMG000.BIN";name[3]=(CHAR16)('0'+used/100);name[4]=(CHAR16)('0'+(used/10)%10);name[5]=(CHAR16)('0'+used%10);
    Text(c,"port-image index=0x");Hex(c,used,4);Text(c," handle=0x");Hex(c,(UINTN)binding->ImageHandle,16);
    Text(c," base=0x");Hex(c,(UINTN)image->ImageBase,16);Text(c," bytes=0x");Hex(c,image->ImageSize,16);End(c);
    /* Firmware drivers contain mutable data. Hash and save the same private
     * RAM snapshot so filesystem callbacks cannot change the source between
     * hashing and writing it. The snapshot never runs as an EFI image. */
    VOID *snapshot=NULL;UINTN imageBytes=(UINTN)image->ImageSize;
    s=t->BootServices->AllocatePool(EfiBootServicesData,imageBytes,&snapshot);
    if(s!=EFI_SUCCESS||!snapshot){Problem(c,"image-snapshot-allocation",EFI_OUT_OF_RESOURCES);break;}
    for(UINTN j=0;j<imageBytes;j++)((UINT8 *)snapshot)[j]=((const volatile UINT8 *)image->ImageBase)[j];
    Hash(c,"port-image sha256=",snapshot,imageBytes);
    s=Save(c,name,snapshot,imageBytes);t->BootServices->FreePool(snapshot);
    if(s!=EFI_SUCCESS)break;
    total+=(UINTN)image->ImageSize;used++;
  }
  Value(c,"port-image-count=0x",used);Value(c,"port-image-total-bytes=0x",total);
  t->BootServices->FreePool(bindings);
}
EFI_STATUS GuProbeGpu(EFI_SYSTEM_TABLE *t,CAPTURE *c) {
  EFI_HANDLE *handles=NULL;UINTN count=0,matches=0;EFI_PCI_IO_PROTOCOL *target=NULL;
  EFI_STATUS s=t->BootServices->LocateHandleBuffer(ByProtocol,&PciGuid,NULL,&count,&handles);
  if(s!=EFI_SUCCESS||!handles||count>4096){Problem(c,"port-pci-enumeration",s);if(handles)t->BootServices->FreePool(handles);return EFI_NOT_READY;}
  for(UINTN i=0;i<count;i++) {
    EFI_PCI_IO_PROTOCOL *io=NULL;UINT32 id=0;
    if(t->BootServices->HandleProtocol(handles[i],&PciGuid,(VOID **)&io)!=EFI_SUCCESS||!io||!io->Pci.Read){Problem(c,"port-pci-interface",EFI_DEVICE_ERROR);continue;}
    s=io->Pci.Read(io,EfiPciIoWidthUint32,0,1,&id);
    if(s!=EFI_SUCCESS){Problem(c,"port-pci-id-read",s);continue;}
    if(id==PORT_PCI_ID){matches++;target=io;}
  }
  t->BootServices->FreePool(handles);Value(c,"port-target-count=0x",matches);
  if(matches!=1 || !c->Complete){Problem(c,"port-target-ambiguous-or-incomplete",EFI_NOT_READY);return EFI_NOT_READY;}
  UINT32 cfg[64],again[64];UINTN seg=0,bus=0,dev=0,fun=0;
  if(!target->GetLocation||!target->GetBarAttributes||!target->Mem.Read)return EFI_UNSUPPORTED;
  s=target->GetLocation(target,&seg,&bus,&dev,&fun);
  if(s!=EFI_SUCCESS){Problem(c,"port-location",s);return s;}
  Text(c,"port-location seg=0x");Hex(c,seg,4);Text(c," bus=0x");Hex(c,bus,4);Text(c," dev=0x");Hex(c,dev,4);Text(c," fun=0x");Hex(c,fun,4);End(c);
  s=target->Pci.Read(target,EfiPciIoWidthUint32,0,64,cfg);
  if(s!=EFI_SUCCESS){Problem(c,"port-config-read",s);return s;}
  Text(c,"port-config-dwords=");for(UINTN i=0;i<64;i++){Hex(c,cfg[i],8);Text(c," ");}End(c);
  if(!PortConfigValid(cfg,seg,bus,dev,fun)){Problem(c,"port-identity-or-decode",EFI_UNSUPPORTED);return EFI_UNSUPPORTED;}
  VOID *resource=NULL;UINT64 supports=0;
  s=target->GetBarAttributes(target,0,&supports,&resource);
  if(s!=EFI_SUCCESS||!resource){Problem(c,"port-bar-descriptor",s);if(resource)t->BootServices->FreePool(resource);return EFI_NOT_READY;}
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *bar=resource;
  Value(c,"port-bar0-base=0x",bar->AddrRangeMin);Value(c,"port-bar0-length=0x",bar->AddrLen);
  BOOLEAN valid=PortBarValid(cfg,bar);t->BootServices->FreePool(resource);
  if(!valid){Problem(c,"port-bar-bounds",EFI_UNSUPPORTED);return EFI_UNSUPPORTED;}
  s=target->Pci.Read(target,EfiPciIoWidthUint32,0,64,again);
  if(s!=EFI_SUCCESS||!PortSameResource(cfg,again)){Problem(c,"port-config-changed",EFI_NOT_READY);return EFI_NOT_READY;}
  /* Booter ucode 3 and the board PROM's FWSEC ucode 9 select these exact
   * fuse words. They cannot be reconstructed from the ucode-1 fuse value. */
  static const UINT32 offsets[]={0,0x108154,0x108170,0x108180,0x108354,0x118128,0x118234,0x110100,0x1183a4,0x1fa828,0x8219c0,0x822804,0x823804,0x82381c,0x823820,0x824140,0x824148,0x8241e0,0x1fa7c8,0x1fa82c,0x1fa830,0x625f04};
  for(UINTN i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
    UINT32 value=0;s=target->Mem.Read(target,EfiPciIoWidthUint32,0,offsets[i],1,&value);
    Text(c,"port-mmio off=0x");Hex(c,offsets[i],8);Text(c," status=0x");Hex(c,s,16);Text(c," value=0x");Hex(c,value,8);End(c);
    if(s!=EFI_SUCCESS){Problem(c,"port-mmio-read",s);return s;}
    if(i==0 && value!=0xb72000a1U){Problem(c,"port-chip-id",EFI_UNSUPPORTED);return EFI_UNSUPPORTED;}
  }
  VOID *prom=NULL;s=t->BootServices->AllocatePool(EfiBootServicesData,0x100000,&prom);
  if(s!=EFI_SUCCESS||!prom){Problem(c,"port-prom-allocation",EFI_OUT_OF_RESOURCES);return EFI_OUT_OF_RESOURCES;}
  for(UINTN off=0;off<0x100000 && s==EFI_SUCCESS;off+=4096)
    s=target->Mem.Read(target,EfiPciIoWidthUint32,0,0x300000+off,1024,(UINT8 *)prom+off);
  if(s==EFI_SUCCESS) {
    UINT8 *bytes=prom;BOOLEAN zero=TRUE,ones=TRUE;
    for(UINTN i=0;i<0x100000;i++){if(bytes[i])zero=FALSE;if(bytes[i]!=255)ones=FALSE;}
    if(zero||ones){Problem(c,"port-prom-unreadable",EFI_COMPROMISED_DATA);s=EFI_COMPROMISED_DATA;}
    else {Hash(c,"port-prom-sha256=",prom,0x100000);s=Save(c,L"PROM.BIN",prom,0x100000);}
  } else Problem(c,"port-prom-read",s);
  t->BootServices->FreePool(prom);
  if(s==EFI_SUCCESS)Images(t,c);
#ifdef GU_PORT_PLAN_REVIEW
  if(s==EFI_SUCCESS && c->Complete) {
    extern EFI_STATUS PortReviewPlan(EFI_SYSTEM_TABLE *,CAPTURE *);
    s=PortReviewPlan(t,c);
    if(s!=EFI_SUCCESS)Problem(c,"read-only-release-plan",s);
  }
#endif
  Text(c,"port-observation-only=1 can-unlock=0\r\n");
  return s;
}
