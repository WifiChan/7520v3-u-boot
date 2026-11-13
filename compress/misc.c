
#include "zconf.h"

extern char input_data[];
extern char input_data_end[];

unsigned char *output_data     = NULL;

unsigned long free_mem_ptr     = 0;
unsigned long free_mem_end_ptr = 0;

void error(char *x)
{
    while(1);
}

extern int do_decompress(u8 *input, int len, u8 *output, void (*error)(char *x));

void decompress_kernel(unsigned long output_start, unsigned long free_mem_ptr_p, unsigned long free_mem_ptr_end_p)
{
    output_data      = (unsigned char *)output_start;
    free_mem_ptr     = free_mem_ptr_p;
    free_mem_end_ptr = free_mem_ptr_end_p;

    do_decompress((u8 *)input_data, input_data_end - input_data + 1, output_data, error);
}

typedef unsigned long CYG_WORD;

#define CYG_STR_UNALIGNED(X, Y) \
     (((CYG_WORD)(X) & (sizeof (CYG_WORD) - 1)) | \
      ((CYG_WORD)(Y) & (sizeof (CYG_WORD) - 1)))

#define CYG_STR_OPT_BIGBLOCKSIZE     (sizeof(CYG_WORD) << 2)

#define CYG_STR_OPT_LITTLEBLOCKSIZE (sizeof (CYG_WORD))

void *memcpy( void *s1, const void *s2, size_t n )
{
    char *dst = (char *) s1;
    const char *src = (const char *) s2;
    CYG_WORD *aligned_dst;
    const CYG_WORD *aligned_src;    

    if (dst == src)
        return s1;

    if (n < sizeof(CYG_WORD) || CYG_STR_UNALIGNED (src, dst))
    {
        while (n--)
            *dst++ = *src++;
        return s1;
    } 
    
    aligned_dst = (CYG_WORD *)dst;
    aligned_src = (const CYG_WORD *)src;
    
    while (n >= CYG_STR_OPT_BIGBLOCKSIZE)
    {
        *aligned_dst++ = *aligned_src++;
        *aligned_dst++ = *aligned_src++;
        *aligned_dst++ = *aligned_src++;
        *aligned_dst++ = *aligned_src++;
        n -= CYG_STR_OPT_BIGBLOCKSIZE;
    }
    
    while (n >= CYG_STR_OPT_LITTLEBLOCKSIZE)
    {
        *aligned_dst++ = *aligned_src++;
        n -= CYG_STR_OPT_LITTLEBLOCKSIZE;
    } 
    
    dst = (char*)aligned_dst;
    src = (const char*)aligned_src;
    while (n--)
        *dst++ = *src++;
    
    return s1;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--)
        *p++ = (unsigned char)c;
    return s;
}

