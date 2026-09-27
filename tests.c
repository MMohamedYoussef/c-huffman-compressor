#define UNIT_TEST
#include "main.c"
#include <stdlib.h>
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)
#include <string.h>

static void round_trip(const unsigned char *bytes, size_t count) {
    FILE *in = tmpfile(), *packed = tmpfile(), *out = tmpfile();
    unsigned char check[256];
    CHECK(in && packed && out);
    CHECK(fwrite(bytes, 1, count, in) == count);
    rewind(in);
    CHECK(compress_stream(in, packed) == 0);
    rewind(packed);
    CHECK(decompress_stream(packed, out) == 0);
    rewind(out);
    CHECK(fread(check, 1, sizeof(check), out) == count);
    CHECK(memcmp(bytes, check, count) == 0);
    fclose(in); fclose(packed); fclose(out);
}

int main(void) {
    const unsigned char mixed[] = {0, 255, 1, 0, 1, 255, 5, 5, 5, 5};
    const unsigned char one[] = {'z', 'z', 'z', 'z'};
    FILE *bad = tmpfile(), *out = tmpfile();
    round_trip(mixed, sizeof(mixed));
    round_trip(one, sizeof(one));
    round_trip(mixed, 0);
    CHECK(bad && out);
    fputs("HUF1", bad);
    rewind(bad);
    CHECK(decompress_stream(bad, out) != 0);
    fclose(bad); fclose(out);
    FILE *source = tmpfile(), *archive = tmpfile(), *cut = tmpfile(), *decoded = tmpfile();
    CHECK(source && archive && cut && decoded);
    CHECK(fwrite(mixed,1,sizeof(mixed),source)==sizeof(mixed)); rewind(source);
    CHECK(compress_stream(source,archive)==0);
    CHECK(fseek(archive,0,SEEK_END)==0);
    long length=ftell(archive);
    CHECK(length>2052);
    rewind(archive);
    for(long i=0;i<length-1;++i) CHECK(fputc(fgetc(archive),cut)!=EOF);
    rewind(cut);
    CHECK(decompress_stream(cut,decoded)!=0);
    fclose(source); fclose(archive); fclose(cut); fclose(decoded);
    FILE *huge=tmpfile(), *expanded=tmpfile();
    CHECK(huge && expanded);
    CHECK(fwrite("HUF1",1,4,huge)==4);
    for(int i=0;i<256;++i)
        CHECK(write_u64(huge,i=='A' ? UINT64_C(16)*1024*1024+1 : 0));
    rewind(huge);
    CHECK(decompress_stream(huge,expanded)!=0);
    CHECK(ftell(expanded)==0);
    fclose(huge); fclose(expanded);
    FILE *extra=tmpfile(), *discard=tmpfile();
    CHECK(extra && discard);
    CHECK(fwrite("HUF1",1,4,extra)==4);
    for(int i=0;i<256;++i) CHECK(write_u64(extra,i=='z' ? 4 : 0));
    CHECK(fputc(1,extra)!=EOF);
    rewind(extra);
    CHECK(decompress_stream(extra,discard)!=0);
    CHECK(ftell(discard)==0);
    fclose(extra); fclose(discard);
    puts("huffman tests passed");
    return 0;
}
