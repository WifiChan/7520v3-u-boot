	.section .compressed_data, "a", %progbits
	.globl	input_data
input_data:
#if defined(USE_LZMA)
	.incbin	"compressed.lzma"
#else
	.incbin	"compressed.gz"
#endif
	.globl	input_data_end
input_data_end:
    .globl image_start
    .balign 4
image_start:
    .long IMAGE_START
