/* SPDX-License-Identifier: GPL-2.0-only.
 * Compare dependencies discovered in this boot. Both snapshots contribute
 * GPU children/owners: a new dependency cannot hide outside the old scope.
 * No baseline replacement or normalization of changed protected edges.
 */
#define MSI_SCOPE_HANDLES 32
typedef struct {UINTN Count;EFI_HANDLE Handles[MSI_SCOPE_HANDLES];} MSI_GRAPH_SCOPE;
static BOOLEAN MsiScopeHas(const MSI_GRAPH_SCOPE *s,EFI_HANDLE h) {
  if(!h)return FALSE;
  for(UINTN i=0;i<s->Count;i++)if(s->Handles[i]==h)return TRUE;
  return FALSE;
}
static BOOLEAN MsiScopeAdd(MSI_GRAPH_SCOPE *s,EFI_HANDLE h) {
  if(!h || MsiScopeHas(s,h))return TRUE;
  if(s->Count==MSI_SCOPE_HANDLES)return FALSE;
  s->Handles[s->Count++]=h;return TRUE;
}
static BOOLEAN MsiBuildScope(const PLAN *p,const GRAPH *a,const GRAPH *b,MSI_GRAPH_SCOPE *s) {
  *s=(MSI_GRAPH_SCOPE){0};
  const EFI_HANDLE roots[]={p->RtxPci,p->Rtx,p->Console,p->Error,p->Input,p->OutAgent,p->GfxAgent};
  for(UINTN i=0;i<sizeof(roots)/sizeof(roots[0]);i++)if(!MsiScopeAdd(s,roots[i]))return FALSE;
  const GRAPH *graphs[]={a,b};
  for(UINTN n=0;n<2;n++)for(UINTN i=0;i<graphs[n]->Count;i++) {
    const EDGE *e=&graphs[n]->Edges[i];
    if(e->Handle!=p->RtxPci)continue;
    /* Unknown owners/children are sensitive too. Consumers/profile checks
     * separately require the exact supported GPU shape before any mutation. */
    if(!MsiScopeAdd(s,e->Agent))return FALSE;
    if(e->Attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER && !MsiScopeAdd(s,e->Controller))return FALSE;
  }
  return TRUE;
}
static BOOLEAN MsiScopedEdge(const MSI_GRAPH_SCOPE *s,const EDGE *e) {
  return MsiScopeHas(s,e->Handle) || MsiScopeHas(s,e->Agent) || MsiScopeHas(s,e->Controller);
}
static UINTN MsiScopedCount(const MSI_GRAPH_SCOPE *s,const GRAPH *g) {
  UINTN count=0;for(UINTN i=0;i<g->Count;i++)if(MsiScopedEdge(s,&g->Edges[i]))count++;
  return count;
}
