/* GPL-2.0-only. Real wrapper choice + flow; EFI services/policies simulated. */
#include "../../internal-boot/boot/handoff.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int Checks,Reviews,Releases,Unlocks,Boots,Scans,FailLog;
static BOOLEAN Escape;
static EFI_STATUS ColdResult,ReviewResult;
static EFI_BOOT_SERVICES TestBs;
static EFI_SYSTEM_TABLE TestTable;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL TestInput;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL TestOutput;
static int ColdDataLogged,ColdDataDisplayed;
static int Graphs,UseActualCapture,CaptureErrorOnSecond;
static EFI_STATUS GraphResult;
static EFI_FILE_PROTOCOL TestLog;
static void Check(int yes){Checks++;if(!yes){fprintf(stderr,"FAIL choice check %d\n",Checks);exit(1);}}
EFI_STATUS GuPeekCold(EFI_SYSTEM_TABLE *t,UINT32 *v){Check(t==Table);memset(v,0,9*sizeof(*v));v[6]=0x10515124;return ColdResult;}
EFI_STATUS GuReviewReleasePlan(EFI_SYSTEM_TABLE *t,GU_PREPARE_LOG log,VOID *context){Check(t==Table && log==Record && !context);Reviews++;return ReviewResult;}
/* Unused firmware-entry dependencies: any accidental invocation is a failure. */
EFI_STATUS GuPrepareConsole(EFI_SYSTEM_TABLE *t,GU_PREPARE_LOG l,VOID *c){(void)t;(void)l;(void)c;Check(0);return EFI_UNSUPPORTED;}
EFI_STATUS GuReleaseGop(EFI_SYSTEM_TABLE *t,EFI_HANDLE i,GU_PREPARE_LOG l,VOID *c){(void)t;(void)i;(void)l;(void)c;Check(0);return EFI_UNSUPPORTED;}
EFI_STATUS GuVerifyReleased(EFI_SYSTEM_TABLE *t,BOOLEAN b){(void)t;(void)b;Check(0);return EFI_UNSUPPORTED;}
EFI_STATUS GuCaptureGraph(EFI_SYSTEM_TABLE *t,EFI_FILE_PROTOCOL *f){Check(t==Table && f==&TestLog);Graphs++;return CaptureErrorOnSecond && Graphs==2?EFI_DEVICE_ERROR:GraphResult;}
const GU_VERIFY_DIAGNOSTIC *GuLastVerifyDiagnostic(VOID){Check(0);return NULL;}
static EFI_STATUS EFIAPI Key(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *p,EFI_INPUT_KEY *k){Check(p==&TestInput);Scans++;if(Escape){k->ScanCode=0x17;return EFI_SUCCESS;}return EFI_NOT_READY;}
static EFI_STATUS EFIAPI Stall(UINTN n){Check(n==100000);return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Write(EFI_FILE_PROTOCOL *f,UINTN *n,VOID *b){Check(f==&TestLog && n && b);char text[200];Check(*n<sizeof(text));memcpy(text,b,*n);text[*n]=0;if(strstr(text,"pre-release-cold-ss0 status=0x0000000010515124"))ColdDataLogged++;return FailLog?EFI_DEVICE_ERROR:EFI_SUCCESS;}
static EFI_STATUS EFIAPI Output(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *p,CHAR16 *s){Check(p==&TestOutput);char text[512];UINTN i=0;while(s[i] && i<sizeof(text)-1){text[i]=(char)s[i];i++;}text[i]=0;if(strstr(text,"pre-release-cold-ss0"))ColdDataDisplayed++;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Flush(EFI_FILE_PROTOCOL *f){Check(f==&TestLog);return EFI_SUCCESS;}
static EFI_STATUS PrepareFlow(VOID *c){(void)c;return EFI_SUCCESS;}
static EFI_STATUS CaptureFlow(VOID *c){(void)c;return EFI_SUCCESS;}
static EFI_STATUS ReleaseFlow(VOID *c){(void)c;Releases++;return EFI_SUCCESS;}
static EFI_STATUS VerifyFlow(VOID *c){(void)c;return EFI_SUCCESS;}
static EFI_STATUS UnlockFlow(VOID *c){(void)c;Unlocks++;return EFI_SUCCESS;}
static EFI_STATUS WindowsFlow(VOID *c){(void)c;Boots++;return EFI_SUCCESS;}
static void Setup(void){
  Reviews=Releases=Unlocks=Boots=Scans=FailLog=ColdDataLogged=ColdDataDisplayed=0;Escape=FALSE;ColdResult=ReviewResult=EFI_SUCCESS;
  Graphs=UseActualCapture=CaptureErrorOnSecond=0;GraphResult=EFI_SUCCESS;
  memset(&TestBs,0,sizeof(TestBs));memset(&TestTable,0,sizeof(TestTable));memset(&TestInput,0,sizeof(TestInput));memset(&TestLog,0,sizeof(TestLog));
  TestInput.ReadKeyStroke=Key;TestBs.Stall=Stall;TestLog.Write=Write;TestLog.Flush=Flush;
  TestOutput=(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL){0};TestOutput.OutputString=Output;
  TestTable.BootServices=&TestBs;TestTable.ConIn=&TestInput;TestTable.ConOut=&TestOutput;Table=&TestTable;Log=&TestLog;ResearchOutputDisabled=FALSE;
}
static EFI_STATUS Run(void){GU_FULL_FLOW_OPS o={NULL,PrepareFlow,FullChoice,UseActualCapture?FullCapture:CaptureFlow,ReleaseFlow,VerifyFlow,UnlockFlow,WindowsFlow,WindowsFlow,Record};return GuFullFlow(&o);}
int main(void){
  Setup();Check(Run()==EFI_ABORTED && Reviews==1 && Releases==1 && Unlocks==1 && Boots==1 && Scans==50);
  Check(ColdDataLogged==1 && !ColdDataDisplayed);
  const EFI_STATUS bad[]={EFI_NOT_READY,EFI_ACCESS_DENIED,EFI_CRC_ERROR,EFI_DEVICE_ERROR,EFI_WARN_UNKNOWN_GLYPH};
  for(UINTN i=0;i<sizeof(bad)/sizeof(bad[0]);i++){Setup();ReviewResult=bad[i];Check(Run()==EFI_ABORTED && Reviews==1 && !Releases && !Unlocks && Boots==1);}
  Setup();ColdResult=EFI_NOT_READY;Check(Run()==EFI_ABORTED && !Reviews && !Releases && !Unlocks && Boots==1);
  Setup();Escape=TRUE;Check(Run()==EFI_ABORTED && !Reviews && !Releases && !Unlocks && Boots==1);
  Setup();FailLog=1;Check(Run()==EFI_DEVICE_ERROR && !Reviews && !Releases && !Unlocks && !Boots);
  Setup();ReviewResult=EFI_NOT_READY;Check(Run()==EFI_ABORTED && Reviews==1 && !Releases && !Unlocks && Boots==1 && Graphs==1);
  Setup();ReviewResult=EFI_NOT_READY;GraphResult=EFI_DEVICE_ERROR;Check(Run()==EFI_ABORTED && Reviews==1 && !Releases && !Unlocks && Boots==1 && Graphs==1);
  Setup();GraphResult=EFI_NOT_READY;Check(FullCapture(NULL)==EFI_SUCCESS && Graphs==1 && !Releases && !Unlocks && !Boots);
  Setup();GraphResult=EFI_DEVICE_ERROR;Check(FullCapture(NULL)==EFI_DEVICE_ERROR && Graphs==1 && !Releases && !Unlocks && !Boots);
  Setup();GraphResult=EFI_NOT_READY;FailLog=1;Check(FullCapture(NULL)==EFI_DEVICE_ERROR && Graphs==1 && !Releases && !Unlocks && !Boots);
  Setup();UseActualCapture=1;GraphResult=EFI_NOT_READY;Check(Run()==EFI_ABORTED && Graphs==2 && Reviews==1 && Releases==1 && Unlocks==1 && Boots==1);
  Setup();UseActualCapture=1;GraphResult=EFI_DEVICE_ERROR;Check(Run()==EFI_DEVICE_ERROR && Graphs==1 && !Releases && !Unlocks && !Boots);
  Setup();UseActualCapture=1;CaptureErrorOnSecond=1;Check(Run()==EFI_DEVICE_ERROR && Graphs==2 && Releases==1 && !Unlocks && !Boots);
  printf("PASS %d real wrapper choice/flow checks: failed full read-only review boots Windows with zero release/unlock calls; no GPU access\n",Checks);return 0;
}
