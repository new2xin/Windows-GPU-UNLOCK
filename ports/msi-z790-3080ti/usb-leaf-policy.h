/* SPDX-License-Identifier: GPL-2.0-only.
 * Included by prepare-console.c only for the exact MSI profile.
 * Compare three otherwise unowned ASMedia USB leaves by route + cached USB
 * descriptors. EFI handles may be recreated by firmware enumeration. This is
 * NOT a graph rebaseline: every other edge remains exact, absent leaves fail,
 * and any active protocol on a leaf prevents the exception. No USB transfer,
 * reset, OpenProtocol, ConnectController or DisconnectController is issued.
 */
static EFI_GUID MsiUsbHcGuid=EFI_USB2_HC_PROTOCOL_GUID,MsiUsbIoGuid=EFI_USB_IO_PROTOCOL_GUID;
static const UINT8 MsiUsbHostPath[]={2,1,12,0,0xd0,0x41,3,10,0,0,0,0,1,1,6,0,1,28,1,1,6,0,0,0,127,255,4,0};
static const EFI_GUID MsiUsbFile={0x580dd900,0x385d,0x11d7,{0x88,0x3a,0,0x50,4,0x73,0xd4,0xeb}};
static const char MsiUsbTextHash[]="d52186550b66abcb67d9eb4d1a5f27bbfc302541ff714ef9025a261e4126de47";

static int MsiUsbSlot(const EFI_DEVICE_PATH_PROTOCOL *p,UINTN *bytes) {
  *bytes=0;
  if(!GuInspectPath(p,NULL,bytes,NULL,NULL) || (*bytes!=34 && *bytes!=40) ||
     !Bytes(p,MsiUsbHostPath,24))return -1;
  const UINT8 *b=(const VOID *)p;
  if(b[24]!=3 || b[25]!=5 || b[26]!=6 || b[27] || b[29])return -1;
  if(*bytes==34 && Bytes(b+30,MsiUsbHostPath+24,4)) {
    if(b[28]==0)return 0;
    if(b[28]==2)return 1;
  }
  const UINT8 tail[]={3,5,6,0,1,0,127,255,4,0};
  return *bytes==40 && b[28]==2 && Bytes(b+30,tail,sizeof(tail))?2:-1;
}
static EFI_STATUS MsiUsbDriver(EFI_BOOT_SERVICES *bs,EFI_HANDLE agent,EFI_HANDLE *image,UINTN *base) {
  EFI_DRIVER_BINDING_PROTOCOL *d=NULL;EFI_LOADED_IMAGE_PROTOCOL *im=NULL;
  EFI_STATUS s=Interface(bs,agent,&BindingGuid,(VOID **)&d);
  if(s!=EFI_SUCCESS || d->DriverBindingHandle!=agent || d->Version!=0x23 || !d->ImageHandle)return EFI_UNSUPPORTED;
  s=Interface(bs,d->ImageHandle,&ImageGuid,(VOID **)&im);UINTN n=0;
  if(s!=EFI_SUCCESS || !im->ImageBase || im->ImageSize!=0x113e0 || im->ImageCodeType!=EfiBootServicesCode ||
     (UINTN)d->Stop!=(UINTN)im->ImageBase+0x90c4 || !im->FilePath ||
     !GuInspectPath(im->FilePath,NULL,&n,NULL,NULL) || n!=24 ||
     im->FilePath->Type!=MEDIA_DEVICE_PATH || im->FilePath->SubType!=MEDIA_PIWG_FW_FILE_DP ||
     im->FilePath->Length[0]!=20 || im->FilePath->Length[1] || !GuGuidEqual((UINT8 *)im->FilePath+4,&MsiUsbFile))return EFI_UNSUPPORTED;
  /* v4 IMG001, .text RVA 0x280 / 0xe5d1 bytes, zero text relocations. */
  if(!GuHashMatches((UINT8 *)im->ImageBase+0x280,0xe5d1,MsiUsbTextHash))return EFI_CRC_ERROR;
  *image=d->ImageHandle;*base=(UINTN)im->ImageBase;return EFI_SUCCESS;
}
static EFI_STATUS MsiCaptureUsbLeaves(EFI_BOOT_SERVICES *bs,GRAPH *out) {
  for(UINTN i=0;i<3;i++)out->UsbLeaves[i]=(MSI_USB_LEAF){0};
  for(UINTN i=0;i<out->Count;i++) {
    const EDGE *e=&out->Edges[i];
    if(e->Attributes!=EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER || !GuGuidEqual(&e->Protocol,&MsiUsbHcGuid))continue;
    EFI_DEVICE_PATH_PROTOCOL *hostPath=NULL,*path=NULL;UINTN n=0;
    EFI_STATUS s=Interface(bs,e->Handle,&PathGuid,(VOID **)&hostPath);
    if(s!=EFI_SUCCESS || !GuInspectPath(hostPath,NULL,&n,NULL,NULL) || n!=sizeof(MsiUsbHostPath) ||
       !Bytes(hostPath,MsiUsbHostPath,n))continue; /* All other hosts stay exact. */
    s=Interface(bs,e->Controller,&PathGuid,(VOID **)&path);
    if(s!=EFI_SUCCESS)return s;
    int slot=MsiUsbSlot(path,&n);
    if(slot<0)continue; /* Other ports keep exact handles. */
    MSI_USB_LEAF *leaf=&out->UsbLeaves[slot];
    if(leaf->Valid || e->Count!=1 || e->Controller==e->Handle || e->Controller==e->Agent)return EFI_ACCESS_DENIED;
    /* A leaf with a storage/input/other owner is never canonicalized. Retain
     * the complete active graph, including all descendants and all agents. */
    for(UINTN j=0;j<out->Count;j++)if(j!=i) {
      const EDGE *x=&out->Edges[j];
      if(x->Handle==e->Controller || x->Agent==e->Controller || x->Controller==e->Controller)return EFI_ACCESS_DENIED;
    }
    EFI_GUID **protocols=NULL;UINTN pc=0,dp=0,usb=0;
    s=bs->ProtocolsPerHandle(e->Controller,&protocols,&pc);
    if(s==EFI_SUCCESS && (!protocols || pc!=2))s=EFI_ACCESS_DENIED;
    for(UINTN j=0;s==EFI_SUCCESS && j<pc;j++) {
      if(!protocols[j]){s=EFI_COMPROMISED_DATA;break;}
      if(GuGuidEqual(protocols[j],&PathGuid))dp++;
      else if(GuGuidEqual(protocols[j],&MsiUsbIoGuid))usb++;
      else s=EFI_ACCESS_DENIED;
    }
    if(protocols && bs->FreePool(protocols)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
    if(s!=EFI_SUCCESS || dp!=1 || usb!=1)return s==EFI_SUCCESS?EFI_ACCESS_DENIED:s;
    leaf->Host=e->Handle;leaf->Agent=e->Agent;leaf->Child=e->Controller;leaf->PathBytes=n;
    for(UINTN j=0;j<n;j++)leaf->Path[j]=((UINT8 *)path)[j];
    EFI_PCI_IO_PROTOCOL *io=NULL;s=Interface(bs,e->Handle,&PciGuid,(VOID **)&io);
    if(s!=EFI_SUCCESS || !io->GetLocation || !io->Pci.Read)return EFI_UNSUPPORTED;
    UINTN seg=0,bus=0,dev=0,fun=0;
    s=io->GetLocation(io,&seg,&bus,&dev,&fun);
    if(s!=EFI_SUCCESS || seg || bus!=6 || dev || fun)return EFI_UNSUPPORTED;
    s=io->Pci.Read(io,EfiPciIoWidthUint32,0,16,leaf->HostConfig);
    if(s!=EFI_SUCCESS)return s;
    const UINT32 *cfg=leaf->HostConfig;
    if(cfg[0]!=0x21421b21 || cfg[2]!=0x0c033000 || cfg[4]!=0x82e00004 || cfg[5] || cfg[11]!=0x1b213242)return EFI_UNSUPPORTED;
    leaf->HostIo=io;UINTN base=0;
    s=MsiUsbDriver(bs,e->Agent,&leaf->Image,&base);if(s!=EFI_SUCCESS)return s;
    EFI_USB_IO_PROTOCOL *u=NULL;s=Interface(bs,e->Controller,&MsiUsbIoGuid,(VOID **)&u);
    if(s!=EFI_SUCCESS)return s;
    leaf->DeviceGetter=(UINTN)u->UsbGetDeviceDescriptor;leaf->ConfigGetter=(UINTN)u->UsbGetConfigDescriptor;leaf->InterfaceGetter=(UINTN)u->UsbGetInterfaceDescriptor;
    if(!leaf->DeviceGetter || !leaf->ConfigGetter || !leaf->InterfaceGetter)return EFI_UNSUPPORTED;
    /* These getters copy cached descriptors; they do not submit transfers. */
    s=u->UsbGetDeviceDescriptor(u,&leaf->Device);
    if(s==EFI_SUCCESS)s=u->UsbGetConfigDescriptor(u,&leaf->Config);
    if(s==EFI_SUCCESS)s=u->UsbGetInterfaceDescriptor(u,&leaf->Interface);
    if(s!=EFI_SUCCESS)return s;
    if(leaf->Device.Length!=sizeof(leaf->Device) || leaf->Device.DescriptorType!=1 || !leaf->Device.NumConfigurations ||
       leaf->Config.Length!=sizeof(leaf->Config) || leaf->Config.DescriptorType!=2 || leaf->Config.TotalLength<sizeof(leaf->Config) ||
       leaf->Interface.Length!=sizeof(leaf->Interface) || leaf->Interface.DescriptorType!=4 || leaf->Interface.InterfaceNumber)return EFI_UNSUPPORTED;
    leaf->Valid=TRUE;
  }
  return EFI_SUCCESS;
}
static BOOLEAN MsiAllUsbLeaves(const GRAPH *g) {return g->UsbLeaves[0].Valid && g->UsbLeaves[1].Valid && g->UsbLeaves[2].Valid;}
static const MSI_USB_LEAF *MsiLeafForEdge(const GRAPH *g,const EDGE *e) {
  if(e->Attributes!=EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER || e->Count!=1 || !GuGuidEqual(&e->Protocol,&MsiUsbHcGuid))return NULL;
  for(UINTN i=0;i<3;i++) {
    const MSI_USB_LEAF *l=&g->UsbLeaves[i];
    if(l->Valid && l->Host==e->Handle && l->Agent==e->Agent && l->Child==e->Controller)return l;
  }
  return NULL;
}
static BOOLEAN MsiSameLeaf(const MSI_USB_LEAF *a,const MSI_USB_LEAF *b) {
  return a->Host==b->Host && a->Agent==b->Agent && a->Image==b->Image && a->HostIo==b->HostIo &&
    a->PathBytes==b->PathBytes && Bytes(a->Path,b->Path,a->PathBytes) && Bytes(a->HostConfig,b->HostConfig,sizeof(a->HostConfig)) &&
    Bytes(&a->Device,&b->Device,sizeof(a->Device)) && Bytes(&a->Config,&b->Config,sizeof(a->Config)) &&
    Bytes(&a->Interface,&b->Interface,sizeof(a->Interface)) && a->DeviceGetter==b->DeviceGetter &&
    a->ConfigGetter==b->ConfigGetter && a->InterfaceGetter==b->InterfaceGetter;
}
static BOOLEAN MsiUsbHandlesChanged(const GRAPH *a,const GRAPH *b) {
  for(UINTN i=0;i<3;i++)if(a->UsbLeaves[i].Valid && b->UsbLeaves[i].Valid && a->UsbLeaves[i].Child!=b->UsbLeaves[i].Child)return TRUE;
  return FALSE;
}
