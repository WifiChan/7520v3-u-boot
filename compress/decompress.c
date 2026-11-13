#include "zconf.h"

#if defined(USE_LZMA)
#include "LzmaDec.h"

static void *SzAlloc(void *p, size_t size) {
    extern unsigned long free_mem_ptr;
    void *ptr = (void *)free_mem_ptr;
    free_mem_ptr += (size + 7) & ~7;
    return ptr;
}

static void SzFree(void *p, void *address) {}

int do_decompress(u8 *input, int len, u8 *output, void (*error)(char *x))
{
    ISzAlloc alloc;
    alloc.Alloc = SzAlloc;
    alloc.Free = SzFree;

    ELzmaStatus status;
    SizeT destLen = 0;
    SizeT srcLen;
    int i;

    if (len < 13) {
        if (error) error("LZMA input too small\n");
        return -1;
    }

    srcLen = len - 13;

    for (i = 0; i < 4; i++) {
        destLen |= ((SizeT)input[5 + i]) << (i * 8);
    }

    SRes res = LzmaDecode(output, &destLen, input + 13, &srcLen,
                          input, 5, LZMA_FINISH_ANY, &status, &alloc);
    if (res != SZ_OK) {
        if (error) error("LZMA decompress error\n");
        return -1;
    }
    return 0;
}
#else
#include "gnuzip.h"

int do_decompress(u8 *input, int len, u8 *output, void (*error)(char *x))
{
	return gunzip(input, len, NULL, NULL, output, NULL, error);
}
#endif
