/*******************************************************************************
* 版权所有 (C)2016, 中兴通讯股份有限公司。
* 
* 文件名称:     head.S
* 文件标识:     head.S
* 内容摘要:     版本解压启动代码
* 
* 修改日期        版本号      修改标记        修改人          修改内容
* ------------------------------------------------------------------------------
* 2016/09/12      V1.0        Create          邓宁堃          创建
* 
*******************************************************************************/

/*******************************************************************************
*                                   头文件                                     *
*******************************************************************************/

/*******************************************************************************
*                                   宏定义                                     *
*******************************************************************************/
#ifdef __thumb2__
#define ARM(x...)
#define THUMB(x...)	x
#define W(instr)	instr.w
#else
#define ARM(x...)	x
#define THUMB(x...)
#define W(instr)	instr
#endif
#define ARM_BE8(x...)

#define END(name) \
	.size name, .-name

#define ENDPROC(name) \
  .type name, %function; \
  END(name)

#define CYGOPT_HAL_ARM_MMU
#define CONFIG_CPU_CP15

#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
#define CB_BITS 0x08
#else
#define CB_BITS 0x0c
#endif

/*******************************************************************************
*                                外部函数声明                                  *
*******************************************************************************/
.extern decompress_kernel

/*******************************************************************************
*                                外部变量声明                                  *
*******************************************************************************/
.extern image_start

/*******************************************************************************
*                                全局函数实现                                  *
*******************************************************************************/

.section .start, "ax", %progbits
.align
.arm				@ Always enter in ARM state
.global _start
.type	_start, function

_start:
    .rept	8
    mov	r0, r0
    .endr
		
.text
    /* move to SVC MODE */
    mrs     r0, cpsr
    bic     r0, #0x1f
    orr     r0, r0, #0xd3
    bic     r0, #(1<<8)                     /* unmask Asynchronous abort */
    msr     cpsr_cxsf, r0

    /* Control Register Setup */
    mrc     p15, 0, r0, c1, c0, 0
    bic     r0, r0, #(1<<0)         /* MMU disabled */
    orr     r0, r0, #(1<<1)         /* Alignment fault checking enabled */
    bic     r0, r0, #(1<<2)         /* Data Cache disabled */
    orr     r0, r0, #(1<<11)        /* Branch prediction enabled */
    bic     r0, r0, #(1<<12)        /* Instruction Cache disabled */
    bic     r0, r0, #(1<<13)        /* USE VBAR to set the vector base address */
    DSB                             /* Ensure all previous loads/stores have completed */
    mcr     p15, 0, r0, c1, c0, 0
    ISB
        
    adr	r0, LC0
    ldmia   r0, {r1, r2, r3, r6, r10, r11, r12}
    ldr	sp, [r0, #28]

    /*
     * We might be running at a different address.  We need
     * to fix up various pointers.
     */
    sub	r0,  r0,  r1    @ calculate the delta offset
    add r2,  r2,  r0    @ __bss_start
    add r3,  r3,  r0    @ __bss_end
    add	r6,  r6,  r0    @ _edata
    add	r10, r10, r0    @ inflated kernel size location
    add r11, r11, r0    @ got_start
    add r12, r12, r0    @ got_end
    add	sp,  sp,  r0    @ sp

    /*　解压后内核的启动地址 */
    ldr r4, =image_start
    add r4,  r4,  r0
    ldr r4, [r4]

    /*
     * The kernel build system appends the size of the
     * decompressed kernel at the end of the compressed data
     * in little-endian form.
     */
    ldrb	r9, [r10, #0]
    ldrb	lr, [r10, #1]
    orr	    r9, r9, lr, lsl #8
    ldrb	lr, [r10, #2]
    ldrb	r10, [r10, #3]
    orr	    r9, r9, lr, lsl #16
    orr	    r9, r9, r10, lsl #24

    add     r10, r4, r9
    adr     r9, _clear_bss
    cmp	    r10, r9
dead_loop:
    bgt     dead_loop

    /*
     * Relocate all entries in the GOT table.
     * Bump bss entries to _edata + dtb size
     */
    mov r5, #0
1:		
    ldr	  r1, [r11, #0]     @ relocate entries in the GOT
    add	  r1, r1, r0        @ This fixes up C references
    cmp	  r1, r2            @ if entry >= bss_start &&
    cmphs r3, r1            @ bss_end > entry
    addhi r1, r1, r5        @ entry += dtb size
    str	  r1, [r11], #4     @ next entry
    cmp	  r11, r12
    blo	  1b

    /* bump our bss pointers too */
    add	r2, r2, r5
    add	r3, r3, r5

    /*
     * BSS段清零
     */
    mov     r0, #0
_clear_bss:
    str     r0, [r2], #4
    cmp     r3, r2
    bhi     _clear_bss

    bl  cache_on
    /*
     * decompress kernel
     */
    mov	r0, r4
    mov	r1, sp			    @ malloc space above stack
    add	r2, sp, #0x10000	@ 64k max
    bl  decompress_kernel
    bl  cache_clean_flush
    bl  cache_off

    /*
     * enter kernel
     */
    mov	r0, #0
    bx	r4

.align	2
.type	LC0, #object

LC0:		
    .word	LC0                 @ r1
    .word	__bss_start         @ r2
    .word	__bss_end           @ r3
    .word	_edata              @ r6
#if defined(USE_LZMA)
    .word	input_data + 5      @ r10 (inflated size location in LZMA header)
#else
    .word	input_data_end - 4  @ r10 (inflated size location in gzip trailer)
#endif
    .word	_got_start          @ r11
    .word	_got_end            @ ip    
    .word	.L_user_stack_end   @ sp
.size	LC0, . - LC0

__setup_mmu:
		lsr r3, r4, #0x14
		lsl r3, r3, #0x14
        sub	r3, r3, #16384		@ Page directory size
		bic	r3, r3, #0xff		@ Align the pointer
		bic	r3, r3, #0x3f00
/*
 * Initialise the page tables, turning on the cacheable and bufferable
 * bits for the RAM area only.
 */
		mov	r0, r3
		mov	r9, r0, lsr #18
		mov	r9, r9, lsl #18		@ start of RAM
		add	r10, r9, #0x10000000	@ a reasonable RAM size
		mov	r1, #0x12		@ XN|U + section mapping
		orr	r1, r1, #3 << 10	@ AP=11
		add	r2, r3, #16384
1:		cmp	r1, r9			@ if virt > start of RAM
		cmphs	r10, r1			@   && end of RAM > virt
		bic	r1, r1, #0x1c		@ clear XN|U + C + B
		orrlo	r1, r1, #0x10		@ Set XN|U for non-RAM
		orrhs	r1, r1, r6		@ set RAM section settings
		str	r1, [r0], #4		@ 1:1 mapping
		add	r1, r1, #1048576
		teq	r0, r2
		bne	1b
/*
 * If ever we are running from Flash, then we surely want the cache
 * to be enabled also for our execution instance...  We map 2MB of it
 * so there is no map overlap problem for up to 1 MB compressed kernel.
 * If the execution is in RAM then we would only be duplicating the above.
 */
		orr	r1, r6, #0x04		@ ensure B is set for this
		orr	r1, r1, #3 << 10
		mov	r2, pc
		mov	r2, r2, lsr #20
		orr	r1, r1, r2, lsl #20
		add	r0, r3, r2, lsl #2
		str	r1, [r0], #4
		add	r1, r1, #1048576
		str	r1, [r0]
		mov	pc, lr
ENDPROC(__setup_mmu)

__armv7_mmu_cache_on:
		mov	r12, lr
#ifdef CYGOPT_HAL_ARM_MMU
		mrc	p15, 0, r11, c0, c1, 4	@ read ID_MMFR0
		tst	r11, #0xf		@ VMSA
		movne	r6, #CB_BITS | 0x02	@ !XN
		blne	__setup_mmu
		mov	r0, #0
		mcr	p15, 0, r0, c7, c10, 4	@ drain write buffer
		tst	r11, #0xf		@ VMSA
		mcrne	p15, 0, r0, c8, c7, 0	@ flush I,D TLBs
#endif
		mrc	p15, 0, r0, c1, c0, 0	@ read control reg
		bic	r0, r0, #1 << 28	@ clear SCTLR.TRE
		orr	r0, r0, #0x5000		@ I-cache enable, RR cache replacement
		orr	r0, r0, #0x003c		@ write buffer
		bic	r0, r0, #2		@ A (no unaligned access fault)
		orr	r0, r0, #1 << 22	@ U (v6 unaligned access model)
						@ (needed for ARM1176)
#ifdef CYGOPT_HAL_ARM_MMU
 ARM_BE8(	orr	r0, r0, #1 << 25 )	@ big-endian page tables
		mrcne   p15, 0, r6, c2, c0, 2   @ read ttb control reg
		orrne	r0, r0, #1		@ MMU enabled
		movne	r1, #0xfffffffd		@ domain 0 = client
		bic     r6, r6, #1 << 31        @ 32-bit translation system
		bic     r6, r6, #3 << 0         @ use only ttbr0
		mcrne	p15, 0, r3, c2, c0, 0	@ load page table pointer
		mcrne	p15, 0, r1, c3, c0, 0	@ load domain access control
		mcrne   p15, 0, r6, c2, c0, 2   @ load ttb control
#endif
		mcr	p15, 0, r0, c7, c5, 4	@ ISB
		mcr	p15, 0, r0, c1, c0, 0	@ load control register
		mrc	p15, 0, r0, c1, c0, 0	@ and read it back
		mov	r0, #0
		mcr	p15, 0, r0, c7, c5, 4	@ ISB
		mov	pc, r12

#define PROC_ENTRY_SIZE (4*5)

/* cache on */
		.align	5
cache_on:	mov	r3, #8			@ cache_on function
		b	call_cache_fn

call_cache_fn:	adr	r12, proc_types
#ifdef CONFIG_CPU_CP15
		mrc	p15, 0, r9, c0, c0	@ get processor ID
#elif defined(CONFIG_CPU_V7M)
		/*
		 * On v7-M the processor id is located in the V7M_SCB_CPUID
		 * register, but as cache handling is IMPLEMENTATION DEFINED on
		 * v7-M (if existant at all) we just return early here.
		 * If V7M_SCB_CPUID were used the cpu ID functions (i.e.
		 * __armv7_mmu_cache_{on,off,flush}) would be selected which
		 * use cp15 registers that are not implemented on v7-M.
		 */
		bx	lr
#else
		ldr	r9, =CONFIG_PROCESSOR_ID
#endif
1:		ldr	r1, [r12, #0]		@ get value
		ldr	r2, [r12, #4]		@ get mask
		eor	r1, r1, r9		@ (real ^ match)
		tst	r1, r2			@       & mask
 ARM(		addeq	pc, r12, r3		) @ call cache function
 THUMB(		addeq	r12, r3			)
 THUMB(		moveq	pc, r12			) @ call cache function
		add	r12, r12, #PROC_ENTRY_SIZE
		b	1b

/*
 * Table for cache operations.  This is basically:
 *   - CPU ID match
 *   - CPU ID mask
 *   - 'cache on' method instruction
 *   - 'cache off' method instruction
 *   - 'cache flush' method instruction
 *
 * We match an entry using: ((real_id ^ match) & mask) == 0
 *
 * Writethrough caches generally only need 'on' and 'off'
 * methods.  Writeback caches _must_ have the flush method
 * defined.
 */
		.align	2
		.type	proc_types,#object
proc_types:
		.word	0x000f0000		@ new CPU Id
		.word	0x000f0000
		W(b)	__armv7_mmu_cache_on
		W(b)	__armv7_mmu_cache_off
		W(b)	__armv7_mmu_cache_flush

		.word	0			@ unrecognised type
		.word	0
		mov	pc, lr
 THUMB(		nop				)
		mov	pc, lr
 THUMB(		nop				)
		mov	pc, lr
 THUMB(		nop				)

		.size	proc_types, . - proc_types

		/*
		 * If you get a "non-constant expression in ".if" statement"
		 * error from the assembler on this line, check that you have
		 * not accidentally written a "b" instruction where you should
		 * have written W(b).
		 */
		.if (. - proc_types) % PROC_ENTRY_SIZE != 0
		.error "The size of one or more proc_types entries is wrong."
		.endif

		.align	5
cache_off:	mov	r3, #12			@ cache_off function
		b	call_cache_fn

__armv7_mmu_cache_off:
		mrc	p15, 0, r0, c1, c0
#ifdef CYGOPT_HAL_ARM_MMU
		bic	r0, r0, #0x000d
#else
		bic	r0, r0, #0x000c
#endif
		mcr	p15, 0, r0, c1, c0	@ turn MMU and cache off
		mov	r12, lr
		bl	__armv7_mmu_cache_flush
		mov	r0, #0
#ifdef CYGOPT_HAL_ARM_MMU
		mcr	p15, 0, r0, c8, c7, 0	@ invalidate whole TLB
#endif
		mcr	p15, 0, r0, c7, c5, 6	@ invalidate BTC
		mcr	p15, 0, r0, c7, c10, 4	@ DSB
		mcr	p15, 0, r0, c7, c5, 4	@ ISB
		mov	pc, r12

		.align	5
cache_clean_flush:
		mov	r3, #16
		b	call_cache_fn

__armv7_mmu_cache_flush:
		tst	r4, #1
		bne	_iflush
		mrc	p15, 0, r10, c0, c1, 5	@ read ID_MMFR1
		tst	r10, #0xf << 16		@ hierarchical cache (ARMv7)
		mov	r10, #0
		beq	hierarchical
		mcr	p15, 0, r10, c7, c14, 0	@ clean+invalidate D
		b	_iflush
hierarchical:
		mcr	p15, 0, r10, c7, c10, 5	@ DMB
		stmfd	sp!, {r0-r7, r9-r11}
		mrc	p15, 1, r0, c0, c0, 1
		ands	r3, r0, #0x7000000
		mov	r3, r3, lsr #23
		beq	_finished
		mov	r10, #0
_loop1:
		add	r2, r10, r10, lsr #1
		mov	r1, r0, lsr r2
		and	r1, r1, #7
		cmp	r1, #2
		blt	_skip
		mcr	p15, 2, r10, c0, c0, 0
		mcr	p15, 0, r10, c7, c5, 4
		mrc	p15, 1, r1, c0, c0, 0
		and	r2, r1, #7
		add	r2, r2, #4
		ldr	r4, =0x3ff
		ands	r4, r4, r1, lsr #3
		clz	r5, r4
		ldr	r7, =0x7fff
		ands	r7, r7, r1, lsr #13
_loop2:
		mov	r9, r4
_loop3:
 ARM(		orr	r11, r10, r9, lsl r5	)
 ARM(		orr	r11, r11, r7, lsl r2	)
 THUMB(		lsl	r6, r9, r5		)
 THUMB(		orr	r11, r10, r6		)
 THUMB(		lsl	r6, r7, r2		)
 THUMB(		orr	r11, r11, r6		)
		mcr	p15, 0, r11, c7, c14, 2
		subs	r9, r9, #1
		bge	_loop3
		subs	r7, r7, #1
		bge	_loop2
_skip:
		add	r10, r10, #2
		cmp	r3, r10
		bgt	_loop1
_finished:
		ldmfd	sp!, {r0-r7, r9-r11}
		mov	r10, #0			@ swith back to cache level 0
		mcr	p15, 2, r10, c0, c0, 0	@ select current cache level in cssr
_iflush:
		mcr	p15, 0, r10, c7, c10, 4	@ DSB
		mcr	p15, 0, r10, c7, c5, 0	@ invalidate I+BTB
		mcr	p15, 0, r10, c7, c10, 4	@ DSB
		mcr	p15, 0, r10, c7, c5, 4	@ ISB
		mov	pc, lr

.align
.section ".stack", "aw", %nobits

.L_user_stack:	.space	4096
.L_user_stack_end:

