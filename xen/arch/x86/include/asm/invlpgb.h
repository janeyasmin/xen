#ifndef _ASM_X86_INVLPGB_H_
#define _ASM_X86_INVLPGB_H_

#include <asm/hvm/support.h>
#include <xen/types.h>

#define X86_INVLPGB_ASID_NON_GLOBAL   4
#define X86_INVLPGB_ADDR_GLOBAL       9
#define X86_INVLPGB_ASID_GLOBAL       12
#define X86_INVLPGB_ASID_ADDR_GLOBAL  13
#define X86_INVLPGB_ALL_GLOBAL        15 /* {ASID, PCID, VA} */


static inline void tlb_sync(void)
{
    asm volatile (
#ifdef HAVE_AS_TLBSYNC
	"tlbsync"
#else
	".byte 0x0f, 0x01, 0xff"
#endif
	);
}

/* Only for ASID */
static inline void invlpgb(unsigned long linear, uint32_t asid,
			   uint32_t pcid, unsigned long type)
{
    union {
	struct {
	    uint64_t type      :4;
	    bool_t final       :1;
	    bool_t nested      :1;
	    uint64_t reserved  :6;
	    uint64_t addr      :52;
	};
	uint64_t raw;
    } rax = { .type = type, .addr = linear };

    union {
	struct {
	    uint32_t asid      :16;
	    uint32_t pcid      :12;
	    uint64_t reserved  :4;
	};
	uint64_t raw;
    } edx = { .asid = asid, .pcid = pcid};

    asm volatile(
#ifdef HAVE_AS_INVLPGB
	"invlpgb"
#else
	".byte 0x0f, 0x01, 0x0fe"
#endif
	: /* output */
	: "a" (rax), "d" (edx) /* input */
	);
    tlb_sync();
}

static inline void invlpgb_asid_one(unsigned long addr, uint32_t asid)
{
    invlpgb(addr, asid, 0, X86_INVLPGB_ASID_ADDR_GLOBAL);
}

static inline void invlpgb_addr(unsigned long addr)
{
    invlpgb(addr, 0, 0, X86_INVLPGB_ADDR_GLOBAL);
}

#endif  /* _ASM_X86_INVLPGB_H_ */
