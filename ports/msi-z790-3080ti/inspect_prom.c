/* SPDX-License-Identifier: GPL-2.0-only. Offline use of the production parser.
 * The source PROM stays immutable. Patches are tested only in host RAM. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include "../../internal-boot/unlock/sha256.h"
#include "../../internal-boot/unlock/vendor/vbios_610.h"
int main(int argc,char **argv) {
  if(argc!=2)return 2;FILE *file=fopen(argv[1],"rb");if(!file)return 3;
  unsigned char *rom=malloc(0x100001);if(!rom){fclose(file);return 4;}
  size_t size=fread(rom,1,0x100001,file);fclose(file);
  if(size!=0x100000||!GuHashMatches(rom,size,"81915bf4d2ee2ca8c6069857b97a702d8269dbbb3767e51ec985f4ac5cf4047a")){free(rom);return 5;}
  struct gpu_unlock_fwsec fw;int result=gpu_unlock_vbios_fwsec(rom,size,&fw);
  if(result){fprintf(stderr,"Production FWSEC parser rejected PROM: %d\n",result);free(rom);return 6;}
  printf("{\"image_off\":%u,\"image_size\":%u,\"imem_size\":%u,\"dmem_size\":%u,\"data_off\":%u,\"pkc_off\":%u,\"interface_off\":%u,\"engine_mask\":%u,\"ucode_id\":%u,\"sig_count\":%u,\"sig_versions\":%u,\"sig_off\":%u,\"sig_bytes\":%u,\"authorizes_unlock\":false}\n",
    fw.image_off,fw.image_size,fw.imem_size,fw.dmem_size,fw.data_off,fw.pkc_off,fw.interface_off,fw.engine_mask,fw.ucode_id,fw.sig_count,fw.sig_versions,fw.sig_off,fw.sig_bytes);
  unsigned char *image=malloc(fw.image_size);if(!image){free(rom);return 7;}
  memcpy(image,rom+fw.image_off,fw.image_size);
  /* Geometry test only; the transaction must calculate its real FRTS address. */
  result=gpu_unlock_fwsec_patch_frts(image,&fw,0x2ffe00000ULL);
  fprintf(stderr,"12 GiB geometry patch in host RAM: result=%d; no PROM file changed\n",result);
  if(!GuHashMatches(rom,size,"81915bf4d2ee2ca8c6069857b97a702d8269dbbb3767e51ec985f4ac5cf4047a"))result=-1;
  free(image);free(rom);return result?8:0;
}
