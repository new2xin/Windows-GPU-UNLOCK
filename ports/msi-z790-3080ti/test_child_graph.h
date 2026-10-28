/* Host-only integration: actual child + core with the physical post-release graph.
 * All EFI, memory, PCI, DMA and GPU effects are provided by test_headless.c. */
#include "child-replay-fixture.h"
static GU_HEADLESS_GRAPH ReplayGraph;
static int ReplayMode,ReplayResets,ReplayFaults,ReplayChanged;
static UINTN ReplayAmbient;
static EFI_GUID ReplayDummy={0xface1234,0,0,{0}};
static BOOLEAN ReplayRoot(EFI_HANDLE h) {return h==H(3) || h==H(6) || h==H(1);}
static BOOLEAN ReplaySensitive(const GU_HEADLESS_EDGE *e) {
  if(ReplayRoot(e->handle) || ReplayRoot(e->agent) || ReplayRoot(e->controller))return TRUE;
  for(UINTN i=0;i<sizeof(MsiCapturedEdges)/sizeof(*MsiCapturedEdges);i++) {
    const GU_HEADLESS_EDGE *p=&MsiCapturedEdges[i];
    if(p->controller==H(6) && (e->handle==p->handle || e->agent==p->handle || e->controller==p->handle ||
        e->handle==p->agent || e->agent==p->agent || e->controller==p->agent))return TRUE;
  }
  return FALSE;
}
static VOID ReplayRemove(UINTN index) {
  CHECK(index<ReplayGraph.count);
  for(UINTN i=index+1;i<ReplayGraph.count;i++)ReplayGraph.edges[i-1]=ReplayGraph.edges[i];
  ReplayGraph.count--;
}
static VOID ReplayMutate(VOID) {
  ReplayChanged=1;
  if(ReplayMode==1 || ReplayMode==2)ReplayRemove(ReplayAmbient);
  if(ReplayMode==3)ReplayGraph.edges[ReplayAmbient].controller=H(0xf001);
  if(ReplayMode==4)for(UINTN i=ReplayGraph.count;i>0;i--)if(!ReplaySensitive(&ReplayGraph.edges[i-1]))ReplayRemove(i-1);
  if(ReplayMode==5)for(UINTN i=0;i<256;i++)ReplayGraph.edges[ReplayGraph.count++]=(GU_HEADLESS_EDGE){H(0x8000+i),H(0x9000),H(0xa000+i),ReplayDummy,16,1};
  if(ReplayMode==7)ReplayGraph.edges[ReplayGraph.count++]=(GU_HEADLESS_EDGE){H(3),H(0xf002),H(3),pg,16,1};
  if(ReplayMode==8)ReplayGraph.edges[ReplayGraph.count++]=(GU_HEADLESS_EDGE){H(0xf003),H(0xf004),H(6),ReplayDummy,8,1};
  if(ReplayMode==9)for(UINTN i=0;i<ReplayGraph.count;i++)if(ReplaySensitive(&ReplayGraph.edges[i])){ReplayRemove(i);break;}
  if(ReplayMode==10 || ReplayMode==11 || ReplayMode==12)ReplayFaults=ReplayMode==10?1:3;
  if(ReplayMode==14)for(UINTN i=0;i<ReplayGraph.count/2;i++){GU_HEADLESS_EDGE tmp=ReplayGraph.edges[i];ReplayGraph.edges[i]=ReplayGraph.edges[ReplayGraph.count-1-i];ReplayGraph.edges[ReplayGraph.count-1-i]=tmp;}
}
static EFI_STATUS EFIAPI ReplayReset(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *p,BOOLEAN extended) {
  ReplayResets++;EFI_STATUS s=input_reset(p,extended);
  if(ReplayMode==1)ReplayMutate();return s;
}
static EFI_STATUS EFIAPI ReplayWrite(EFI_FILE_PROTOCOL *f,UINTN *n,VOID *data) {
  EFI_STATUS s=HeadWrite(f,n,data);
  if(!ReplayChanged && ReplayMode!=1 && strstr(report_text,"CHECK stage=assets status=0"))ReplayMutate();
  return s;
}
static EFI_STATUS EFIAPI ReplayKey(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *p,EFI_INPUT_KEY *k) {
  CHECK(p==&conin);Reads++;
  if(ReplayMode==6 && Reads==5)conin.WaitForKey=(VOID *)0xf005;
  if(ReplayMode==16){k->ScanCode=0x17;return EFI_SUCCESS;}
  return EFI_NOT_READY;
}
static EFI_STATUS EFIAPI ReplayLocate(EFI_LOCATE_SEARCH_TYPE t,EFI_GUID *g,VOID *key,UINTN *n,EFI_HANDLE **h) {
  if(t!=AllHandles)return HeadLocate(t,g,key,n,h);
  CHECK(current_tpl==TPL_NOTIFY);
  if(ReplayFaults){ReplayFaults--;*n=0;*h=NULL;return ReplayMode==12?EFI_WARN_UNKNOWN_GLYPH:EFI_NOT_FOUND;}
  EFI_HANDLE handles[1024]={H(3)};*n=1;
  for(UINTN i=0;i<ReplayGraph.count;i++) {
    BOOLEAN exists=FALSE;for(UINTN j=0;j<*n;j++)if(handles[j]==ReplayGraph.edges[i].handle)exists=TRUE;
    if(!exists){CHECK(*n<1024);handles[(*n)++]=ReplayGraph.edges[i].handle;}
  }
  CHECK(allocate_pool(EfiLoaderData,*n*sizeof(**h),(VOID **)h)==EFI_SUCCESS);memcpy(*h,handles,*n*sizeof(**h));return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ReplayProtocols(EFI_HANDLE h,EFI_GUID ***out,UINTN *n) {
  if(h==H(3))return protocols(h,out,n);
  CHECK(current_tpl==TPL_NOTIFY);EFI_GUID *list[256];*n=0;
  for(UINTN i=0;i<ReplayGraph.count;i++)if(ReplayGraph.edges[i].handle==h) {
    BOOLEAN exists=FALSE;for(UINTN j=0;j<*n;j++)if(guid(list[j],&ReplayGraph.edges[i].protocol))exists=TRUE;
    if(!exists){CHECK(*n<256);list[(*n)++]=&ReplayGraph.edges[i].protocol;}
  }
  if(!*n)list[(*n)++]=&ReplayDummy;
  CHECK(allocate_pool(EfiLoaderData,*n*sizeof(**out),(VOID **)out)==EFI_SUCCESS);memcpy(*out,list,*n*sizeof(**out));return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ReplayOpens(EFI_HANDLE h,EFI_GUID *g,EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **out,UINTN *n) {
  if(h==H(3)) {
    EFI_STATUS s=HeadOpens(h,g,out,n);
    if(s!=EFI_SUCCESS || !guid(g,&pg) || !exclusive)return s;
    if(ReplayMode==15){CHECK(*n==1);free_pool(*out);*out=NULL;*n=0;return s;}
    if(ReplayMode==7 || ReplayMode==13) {
      CHECK(*n==1);free_pool(*out);*n=2;CHECK(allocate_pool(EfiLoaderData,2*sizeof(**out),(VOID **)out)==EFI_SUCCESS);
      (*out)[0]=(EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){Gu.image,NULL,EFI_OPEN_PROTOCOL_EXCLUSIVE,1};
      (*out)[1]=ReplayMode==13?(*out)[0]:(EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){H(0xf002),H(3),16,1};
    }
    return s;
  }
  *n=0;*out=NULL;
  for(UINTN i=0;i<ReplayGraph.count;i++)if(ReplayGraph.edges[i].handle==h && guid(&ReplayGraph.edges[i].protocol,g))(*n)++;
  if(!*n)return EFI_SUCCESS;
  CHECK(allocate_pool(EfiLoaderData,*n*sizeof(**out),(VOID **)out)==EFI_SUCCESS);UINTN j=0;
  for(UINTN i=0;i<ReplayGraph.count;i++) {
    GU_HEADLESS_EDGE *e=&ReplayGraph.edges[i];if(e->handle==h && guid(&e->protocol,g))(*out)[j++]=(EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){e->agent,e->controller,e->attributes,e->count};
  }
  return EFI_SUCCESS;
}
static VOID ReplaySetup(int mode) {
  HeadSetup(0);ReplayMode=mode;ReplayResets=ReplayFaults=ReplayChanged=0;
  ReplayGraph.count=sizeof(MsiCapturedEdges)/sizeof(*MsiCapturedEdges);memcpy(ReplayGraph.edges,MsiCapturedEdges,sizeof(MsiCapturedEdges));
  ReplayAmbient=ReplayGraph.count;
  for(UINTN i=0;i<ReplayGraph.count;i++)if(!ReplaySensitive(&ReplayGraph.edges[i])){ReplayAmbient=i;break;}
  CHECK(ReplayAmbient<ReplayGraph.count);
  conin.Reset=ReplayReset;conin.ReadKeyStroke=ReplayKey;
  bs.LocateHandleBuffer=ReplayLocate;bs.ProtocolsPerHandle=ReplayProtocols;bs.OpenProtocolInformation=ReplayOpens;
  files[3].protocol.Write=ReplayWrite;
}
static VOID RunMsiChildReplay(VOID) {
  for(int mode=0;mode<=16;mode++) {
    ReplaySetup(mode);EFI_STATUS s=run_app();
    BOOLEAN pass=mode<=5 || mode==10 || mode==14;
#ifdef GU_TEST_LEGACY_CHILD
    /* Exact v5 app/header must fail these otherwise irrelevant changes. */
    pass=mode==0 || mode==14;
    if(mode==1)CHECK(ReplayResets==1 && ReplayChanged && strstr(report_text,"stage=arming attempted=0"));
#else
    CHECK(ReplayResets==0);
    if(mode==6 || mode==8 || mode==9 || mode==11 || mode==12 || mode==13 || mode==15) {
      CHECK(strstr(report_text,"HEADLESS CHECK FAILED: reason=") && strstr(report_text,"HEADLESS edge: handle="));
      CHECK(!strstr(report_text,"Log format or length error") && !Gu.fault && !halted && !fake.pool_live);
    }
#endif
    if(pass)CHECK(s==EFI_SUCCESS && Reads==30 && fake.flrs==1 && fake.gsp_starts==1 && fake.sec2_starts==1 && registers[SS0/4]==0x88888888 && registers[SS1/4]==8 && !halted && !fake.pool_live);
    else CHECK(s!=EFI_SUCCESS && !Gu.started && !fake.mmio_writes && !fake.master_enables && !fake.flrs);
    CHECK(!Outputs && !disconnect_calls && current_tpl==TPL_APPLICATION);
    printf("PASS physical child replay %d: %s\n",mode,pass?"full simulated unlock completed":"refused before GPU writes");
    dispose_host_fixture();
  }
#ifndef GU_TEST_LEGACY_CHILD
  /* Every observed edge: protected mutation/removal/duplication refuses;
   * unrelated variations remain acceptable. No hardware calls here. */
  ReplaySetup(0);Gu.handle=H(3);Gu.image=H(1);Gu.owned=FALSE;GuHeadlessReset(&st);
  GuHeadless.graph=ReplayGraph;GuHeadless.ready=TRUE;UINTN checks=0,protected=0;
  for(UINTN i=0;i<ReplayGraph.count;i++) {
    BOOLEAN sensitive=ReplaySensitive(&MsiCapturedEdges[i]);if(sensitive)protected++;
    for(UINTN variant=0;variant<5;variant++) {
      ReplayGraph=GuHeadless.graph;
      if(variant==0)ReplayRemove(i);
      if(variant==1)ReplayGraph.edges[ReplayGraph.count++]=ReplayGraph.edges[i];
      if(variant==2)ReplayGraph.edges[i].agent=H(0xf100);
      if(variant==3)ReplayGraph.edges[i].controller=H(0xf101);
      if(variant==4)ReplayGraph.edges[i].count++;
      CHECK(GuHeadlessGraphCheck(&ReplayGraph)==!sensitive);checks++;
    }
    ReplayGraph=GuHeadless.graph;
  }
  const EFI_HANDLE roots[]={H(3),H(6),H(1)};
  for(UINTN root=0;root<3;root++)for(UINTN field=0;field<3;field++) {
    ReplayGraph=GuHeadless.graph;GU_HEADLESS_EDGE *e=&ReplayGraph.edges[ReplayGraph.count++];
    *e=(GU_HEADLESS_EDGE){H(0xf110),H(0xf111),H(0xf112),ReplayDummy,16,1};
    if(field==0)e->handle=roots[root];if(field==1)e->agent=roots[root];if(field==2)e->controller=roots[root];
    CHECK(!GuHeadlessGraphCheck(&ReplayGraph));checks++;
  }
  ReplayGraph=GuHeadless.graph;
  for(UINTN i=0;i<65;i++)ReplayGraph.edges[ReplayGraph.count++]=(GU_HEADLESS_EDGE){H(0xe000+i),H(0xe100+i),H(6),ReplayDummy,8,1};
  CHECK(!GuHeadlessGraphCheck(&ReplayGraph) && !strcmp(GuHeadlessDiagnostic.stage,"scope-capacity"));checks++;
  CHECK(protected>0);dispose_host_fixture();
  printf("PASS %zu physical graph mutations over %zu edges (%zu protected); inbound boundaries preserved\n",(size_t)checks,sizeof(MsiCapturedEdges)/sizeof(*MsiCapturedEdges),(size_t)protected);
#endif
}
