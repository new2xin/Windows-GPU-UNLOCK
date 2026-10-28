/* SPDX-License-Identifier: GPL-2.0-only. Production pure parsers and 12 GiB
 * layout, using RUN001 inputs. No GPU, EFI or on-disk ROM modification. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include "../../internal-boot/unlock/sha256.h"
#include "../../internal-boot/unlock/vendor/prep_610.h"
#include "../../internal-boot/unlock/vendor/vbios_610.h"
#include "stock-fixture.h"
static int Checks;
static void Check(int ok,const char *text){Checks++;if(!ok){fprintf(stderr,"FAIL %s\n",text);exit(1);}}
static unsigned char *Load(const char *path,size_t size,const char *hash) {
  unsigned char *data=malloc(size);FILE *file=fopen(path,"rb");Check(data && file,"input exists");
  Check(fread(data,1,size,file)==size && fgetc(file)==EOF,"exact input length");fclose(file);
  Check(GuHashMatches(data,size,hash),"pinned input SHA-256");return data;
}
static unsigned char *Asset(const char *root,const char *name,size_t size,const char *hash) {
  char path[2048];snprintf(path,sizeof(path),"%s/%s",root,name);return Load(path,size,hash);
}
int main(int argc,char **argv) {
  Check(argc==3,"PROM and assets directory required");
  unsigned char *rom=Load(argv[1],1048576,"81915bf4d2ee2ca8c6069857b97a702d8269dbbb3767e51ec985f4ac5cf4047a");
  unsigned char *gsp=Asset(argv[2],"gsp_ga10x.bin",84277400,"572373620a37418f24dc16b5031c39338778c3257e48e8408de9a57291b24f3a");
  unsigned char *desc=Asset(argv[2],"gsp_boot_desc_ga102.bin",84,"cc5a3fc1f4c47f2fe107b1b68f0176d8a448e6dec84b575e5560ccef5262bf15");
  unsigned char *boot=Asset(argv[2],"gsp_boot_ga102.bin",24576,"3df651acb174fd5f5561a1996965b192f50c5d763168446ed0d4ead19d895768");
  unsigned char *booter=Asset(argv[2],"booter_load_ga102.bin",61256,"056cbc064a888f80717726c54d7f64fcd68044d123870c95a74f37f4975dcaa4");
  unsigned char *v67=Asset(argv[2],"v67-ga102-610.43.03.bin",64000,"2a0aa1f86d0b6afff5214d641d87d46dc98ef06ed908ff05d0ddbad5ffb7e197");
  struct gpu_unlock_span image,signature;struct gpu_unlock_boot_desc bd;struct gpu_unlock_booter_hdr hdr;
  memcpy(&hdr,booter+8,sizeof(hdr));
  Check(gpu_unlock_elf_section(gsp,84277400,".fwimage",&image)==0,"actual GSP image section");
  Check(gpu_unlock_elf_section(gsp,84277400,".fwsignature_ga10x",&signature)==0,"actual GSP signature section");
  Check(gpu_unlock_boot_desc_parse(desc,84,24576,&bd)==0,"actual boot descriptor");
  struct gpu_unlock_layout_in in={0};struct gpu_unlock_layout_out out;
  /* Exact RUN001 registers: 1183a4=3000, 625f04=1, 1fa7c8=0045cb8f,
   * 1fa82c=00100000, 1fa830=02effff0. Same decode as read_layout(). */
  in.usable_fb=(u64)FX_MEMORY<<20;in.vga_base=in.usable_fb-(1ULL<<20);
  in.mmu_lock_low=((u64)FX_MMU_LOW>>4)<<12;
  u64 high=((u64)FX_MMU_HIGH>>4)<<12,top=(high+0xffff)&~0xffffULL;
  in.mmu_lock_valid=high>=in.mmu_lock_low && (top>>20)==(in.usable_fb>>20);
  in.frts_size=1ULL<<20;in.boot_size=bd.image_size;in.image_size=image.size;
  in.nonwpr_size=1ULL<<20;in.heap_request=88ULL<<20;
  Check(gpu_unlock_layout(&in,&out)==0,"12 GiB production WPR layout");
  Check(out.nonwpr_start<out.wpr_start && out.wpr_start<out.gsp_offset && out.gsp_offset<out.boot_offset &&
        out.boot_offset<out.frts_offset && out.frts_offset<out.wpr_end && out.wpr_end<in.usable_fb,"ordered bounded regions");
  struct gpu_unlock_fwsec fw;Check(gpu_unlock_vbios_fwsec(rom,1048576,&fw)==0,"actual board FWSEC");
  unsigned char *copy=malloc(fw.image_size);Check(copy!=NULL,"private FWSEC RAM");memcpy(copy,rom+fw.image_off,fw.image_size);
  Check(gpu_unlock_fwsec_patch_frts(copy,&fw,out.frts_offset)==0,"real computed FRTS patch in host RAM");
  Check(GuHashMatches(rom,1048576,"81915bf4d2ee2ca8c6069857b97a702d8269dbbb3767e51ec985f4ac5cf4047a"),"source ROM remains immutable");
  struct gpu_unlock_radix_plan radix;Check(gpu_unlock_radix_plan(image.size,&radix)==0,"GSP radix geometry");
  unsigned char *bc=malloc(GU_BOOTER_IMAGE_SIZE);Check(bc!=NULL,"private Booter RAM");
  for(u32 version=0;version<hdr.num_sigs;version++){memcpy(bc,booter+72,GU_BOOTER_IMAGE_SIZE);Check(gpu_unlock_booter_patch(bc,GU_BOOTER_IMAGE_SIZE,&hdr,booter+72+GU_BOOTER_IMAGE_SIZE,GU_BOOTER_SIG_TOTAL,version)==0,"each supported Booter version parses");}
  Check(gpu_unlock_booter_patch(bc,GU_BOOTER_IMAGE_SIZE,&hdr,booter+72+GU_BOOTER_IMAGE_SIZE,GU_BOOTER_SIG_TOTAL,hdr.num_sigs)!=0,"out of range Booter version rejected");
  u32 booter_version=gpu_unlock_fuse_version(FX_BOOTER_FUSE);
  u32 fwsec_version=gpu_unlock_fuse_version(FX_FWSEC_FUSE),sig_at=0;
  Check(booter_version==1 && fwsec_version==2,"captured fuse versions decoded by production code");
  Check(gpu_unlock_booter_patch(bc,GU_BOOTER_IMAGE_SIZE,&hdr,booter+72+GU_BOOTER_IMAGE_SIZE,GU_BOOTER_SIG_TOTAL,booter_version)==0,"captured Booter fuse accepted");
  Check(!memcmp(bc+hdr.patch_loc,booter+72+GU_BOOTER_IMAGE_SIZE,384),"captured Booter selects slot zero");
  Check(gpu_unlock_fwsec_sig_offset(fw.sig_versions,fwsec_version,384,&sig_at)==0 && sig_at==768,"captured FWSEC selects slot two");
  u32 offset=0;Check(gpu_unlock_fwsec_sig_offset(fw.sig_versions,16,384,&offset)!=0,"unsupported FWSEC fuse version rejected");
  struct gpu_unlock_layout_in bad=in;bad.usable_fb=0x100000;bad.vga_base=0x80000;
  struct gpu_unlock_layout_out rejected;Check(gpu_unlock_layout(&bad,&rejected)!=0,"undersized memory layout rejected");
  printf("{\"capture_sha256\":\"" FX_CAPTURE_SHA256 "\",\"booter_fuse_raw\":%u,\"fwsec_fuse_raw\":%u,\"booter_version\":%u,\"fwsec_version\":%u,\"fwsec_signature_offset\":%u,",FX_BOOTER_FUSE,FX_FWSEC_FUSE,booter_version,fwsec_version,sig_at);
  printf("\"host_checks\":%d,\"fb_bytes\":%llu,\"gsp_image_bytes\":%llu,\"signature_bytes\":%llu,\"mmu_lock_constrains\":%s,\"wpr_start\":%llu,\"wpr_end\":%llu,\"frts_offset\":%llu,\"gsp_offset\":%llu,\"heap_bytes\":%llu,\"booter_ucode_id\":%u,\"booter_sig_count\":%u,\"booter_fuse_offset\":%u,\"fwsec_ucode_id\":%u,\"fwsec_sig_versions\":%u,\"fwsec_fuse_offset\":%u,\"captured_fuse_values_verified\":true,\"hardware_unlock_tested\":false,\"can_unlock\":false}\n",
    Checks,(unsigned long long)in.usable_fb,(unsigned long long)image.size,(unsigned long long)signature.size,in.mmu_lock_valid?"true":"false",
    (unsigned long long)out.wpr_start,(unsigned long long)out.wpr_end,(unsigned long long)out.frts_offset,(unsigned long long)out.gsp_offset,(unsigned long long)out.heap_size,
    hdr.ucode_id,hdr.num_sigs,GU_FUSE_SEC2_UCODE1+(hdr.ucode_id-1)*4,fw.ucode_id,fw.sig_versions,0x8241c0+(fw.ucode_id-1)*4);
  free(bc);free(copy);free(v67);free(booter);free(boot);free(desc);free(gsp);free(rom);return 0;
}
