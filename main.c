#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYMBOLS 256
#define NODES 511
#define MAX_DECODED_BYTES (UINT64_C(16) * 1024 * 1024)
typedef struct { uint64_t freq; int left, right, symbol, min_symbol, active; } Node;
typedef struct { unsigned char bits[256]; unsigned length; } Code;

static int write_u64(FILE *out, uint64_t value) {
    for (int i=0;i<8;++i) { if(fputc((int)(value&255),out)==EOF) return 0; value>>=8; }
    return 1;
}
static int read_u64(FILE *in, uint64_t *value) {
    *value=0;
    for(int i=0;i<8;++i) { int b=fgetc(in); if(b==EOF) return 0; *value|=(uint64_t)(unsigned)b<<(8*i); }
    return 1;
}
static int less_node(const Node *n, int a, int b) {
    if (b<0) return 1;
    if (n[a].freq!=n[b].freq) return n[a].freq<n[b].freq;
    if (n[a].min_symbol!=n[b].min_symbol) return n[a].min_symbol<n[b].min_symbol;
    return a<b;
}
static int take_min(Node *n, int count) {
    int best=-1;
    for(int i=0;i<count;++i) if(n[i].active && less_node(n,i,best)) best=i;
    if(best>=0) n[best].active=0;
    return best;
}
static int make_tree(const uint64_t freq[SYMBOLS], Node n[NODES], int *root) {
    int count=0;
    for(int i=0;i<SYMBOLS;++i) if(freq[i]) {
        n[count]=(Node){freq[i],-1,-1,i,i,1}; count++;
    }
    if(!count) { *root=-1; return 1; }
    if(count==1) { *root=0; return 1; }
    while(1) {
        int a=take_min(n,count), b=take_min(n,count);
        if(b<0) { *root=a; return a>=0; }
        if(UINT64_MAX-n[a].freq<n[b].freq || count==NODES) return 0;
        n[count]=(Node){n[a].freq+n[b].freq,a,b,-1,
                        n[a].min_symbol<n[b].min_symbol?n[a].min_symbol:n[b].min_symbol,1};
        count++;
    }
}
static void make_codes(const Node n[NODES], int node, Code codes[SYMBOLS], unsigned char path[256], unsigned depth) {
    if(n[node].symbol>=0) {
        codes[n[node].symbol].length=depth;
        memcpy(codes[n[node].symbol].bits,path,depth);
        return;
    }
    path[depth]=0; make_codes(n,n[node].left,codes,path,depth+1);
    path[depth]=1; make_codes(n,n[node].right,codes,path,depth+1);
}
static int compress_stream(FILE *in, FILE *out) {
    uint64_t freq[SYMBOLS]={0}; Node nodes[NODES]; Code codes[SYMBOLS]={0};
    unsigned char path[256]; int c,root; uint64_t total=0;
    while((c=fgetc(in))!=EOF) {
        if(total==MAX_DECODED_BYTES) return 1;
        total++;
        freq[c]++;
    }
    if(ferror(in) || fseek(in,0,SEEK_SET)!=0 || !make_tree(freq,nodes,&root)) return 1;
    if(fwrite("HUF1",1,4,out)!=4) return 1;
    for(int i=0;i<SYMBOLS;++i) if(!write_u64(out,freq[i])) return 1;
    if(root>=0) make_codes(nodes,root,codes,path,0);
    unsigned char byte=0; unsigned bits=0;
    while((c=fgetc(in))!=EOF) {
        Code *code=&codes[c];
        for(unsigned i=0;i<code->length;++i) {
            byte=(unsigned char)((byte<<1)|code->bits[i]);
            if(++bits==8) { if(fputc(byte,out)==EOF) return 1; bits=0; byte=0; }
        }
    }
    if(ferror(in)) return 1;
    if(bits && fputc(byte<<(8-bits),out)==EOF) return 1;
    return fflush(out)==0 ? 0 : 1;
}
static int decompress_stream(FILE *in, FILE *out) {
    uint64_t freq[SYMBOLS], seen[SYMBOLS]={0}, total=0;
    Node nodes[NODES]; int root;
    unsigned char magic[4];
    if(fread(magic,1,4,in)!=4 || memcmp(magic,"HUF1",4)!=0) return 1;
    for(int i=0;i<SYMBOLS;++i) {
        if(!read_u64(in,&freq[i]) || UINT64_MAX-total<freq[i]) return 1;
        total+=freq[i];
    }
    if(total>MAX_DECODED_BYTES) return 1;
    if(!make_tree(freq,nodes,&root)) return 1;
    if(root<0 || nodes[root].symbol>=0) {
        if(fgetc(in)!=EOF || ferror(in)) return 1;
    }
    if(root>=0 && nodes[root].symbol>=0) {
        int symbol=nodes[root].symbol;
        for(uint64_t i=0;i<total;++i) if(fputc(symbol,out)==EOF) return 1;
        seen[symbol]=total;
    } else {
        int byte=0,remaining=0;
        for(uint64_t i=0;i<total;++i) {
            int node=root;
            while(nodes[node].symbol<0) {
                if(!remaining) { byte=fgetc(in); if(byte==EOF) return 1; remaining=8; }
                int bit=(byte>>(--remaining))&1;
                node=bit?nodes[node].right:nodes[node].left;
                if(node<0) return 1;
            }
            int symbol=nodes[node].symbol;
            if(seen[symbol]==UINT64_MAX || fputc(symbol,out)==EOF) return 1;
            seen[symbol]++;
        }
        if(remaining && (byte & ((1<<remaining)-1))!=0) return 1;
    }
    for(int i=0;i<SYMBOLS;++i) if(seen[i]!=freq[i]) return 1;
    if(fgetc(in)!=EOF || ferror(in)) return 1;
    return fflush(out)==0 ? 0 : 1;
}
#ifndef UNIT_TEST
int main(int argc, char **argv) {
    FILE *in,*temp,*out; int result;
    if(argc!=4 || (strcmp(argv[1],"compress") && strcmp(argv[1],"decompress"))) {
        fputs("Usage: huffman compress|decompress input output\n",stderr); return 2;
    }
    in=fopen(argv[2],"rb");
    if(!in) { perror(argv[2]); return 2; }
    temp=tmpfile();
    if(!temp) { perror("tmpfile"); fclose(in); return 2; }
    result=strcmp(argv[1],"compress")==0 ? compress_stream(in,temp) : decompress_stream(in,temp);
    fclose(in);
    if(result) { fputs("Invalid input or I/O error\n",stderr); fclose(temp); return 1; }
    if(fseek(temp,0,SEEK_SET)!=0) { perror("Cannot rewind temporary output"); fclose(temp); return 1; }
    out=fopen(argv[3],"wbx");
    if(!out) { perror(argv[3]); fclose(temp); return 2; }
    unsigned char buffer[8192]; size_t n;
    while((n=fread(buffer,1,sizeof(buffer),temp))!=0)
        if(fwrite(buffer,1,n,out)!=n) { result=1; break; }
    if(ferror(temp)) result=1;
    if(fclose(out)!=0) result=1;
    if(fclose(temp)!=0) result=1;
    if(result) {
        fputs("Output write error\n",stderr);
        if(remove(argv[3])!=0) perror("Cannot remove partial output");
        return 1;
    }
    return 0;
}
#endif
