/*
 * asid.c: ASID management
 * Copyright (c) 2007, Advanced Micro Devices, Inc.
 * Copyright (c) 2009, Citrix Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; If not, see <http://www.gnu.org/licenses/>.
 */

#include <xen/init.h>
#include <xen/lib.h>
#include <xen/param.h>
#include <xen/sched.h>
#include <xen/smp.h>
#include <xen/percpu.h>
#include <asm/hvm/asid.h>
#include <asm/hvm/nestedhvm.h> /* for nestedhvm_vcpu_in_guestmode */

/* Xen command-line option to enable ASIDs */
static bool __read_mostly opt_asid_enabled = true;
boolean_param("asid", opt_asid_enabled);

/*
 * ASIDs partition the physical TLB.  In the current implementation ASIDs are
 * introduced to reduce the number of TLB flushes.  Each time the guest's
 * virtual address space changes (e.g. due to an INVLPG, MOV-TO-{CR3, CR4}
 * operation), instead of flushing the TLB, a new ASID is assigned.  This
 * reduces the number of TLB flushes to at most 1/#ASIDs.  The biggest
 * advantage is that hot parts of the hypervisor's code and data retain in
 * the TLB.
 *
 * Sketch of the Implementation:
 *
 * ASIDs are a CPU-local resource.  As preemption of ASIDs is not possible,
 * ASIDs are assigned in a round-robin scheme.  To minimize the overhead of
 * ASID invalidation, at the time of a TLB flush,  ASIDs are tagged with a
 * 64-bit generation.  Only on a generation overflow the code needs to
 * invalidate all ASID information stored at the VCPUs with are run on the
 * specific physical processor.  This overflow appears after about 2^80
 * host processor cycles, so we do not optimize this case, but simply disable
 * ASID useage to retain correctness.
 */

/* Per-CPU ASID management. */
struct hvm_asid_data {
   uint32_t max_asid;
   bool_t disabled;
};

typedef struct { DECLARE_BITMAP(bits, 16); } flush_asidmask_t;

static DEFINE_PER_CPU(struct hvm_asid_data, hvm_asid_data);

void hvm_asid_init(int nasids)
{
    static int8_t g_disabled = -1;
    struct hvm_asid_data *data = &this_cpu(hvm_asid_data);

    data->max_asid = nasids - 1;
    data->disabled = !opt_asid_enabled || (nasids <= 1);

    if ( g_disabled != data->disabled )
    {
        printk("HVM: ASIDs %sabled.\n", data->disabled ? "dis" : "en");
        if ( g_disabled < 0 )
            g_disabled = data->disabled;
    }

}

void hvm_asid_flush_vcpu_asid(struct hvm_vcpu_asid *asid)
{
    asid->need_flush &= TLB_CTRL_FLUSH_ASID;
}

void hvm_asid_flush_all(void)
{
    struct vcpu *v = current;
    struct hvm_vcpu_asid *asid =
        nestedhvm_vcpu_in_guestmode(v)
        ? &vcpu_nestedhvm(v).nv_n2asid : &v->arch.hvm.n1asid;

    asid->need_flush = TLB_CTRL_FLUSH_ALL;
}

void hvm_asid_flush_vcpu(struct vcpu *v)
{
    hvm_asid_flush_vcpu_asid(&v->arch.hvm.n1asid);
    hvm_asid_flush_vcpu_asid(&vcpu_nestedhvm(v).nv_n2asid);
}

bool_t hvm_asid_handle_vmenter(struct vcpu *v)
{
    struct hvm_asid_data *data = &this_cpu(hvm_asid_data);
    struct hvm_vcpu_asid *p_asid =
        nestedhvm_vcpu_in_guestmode(v)
        ? &vcpu_nestedhvm(v).nv_n2asid : &v->arch.hvm.n1asid;
    struct domain *d = v->domain;
    unsigned int flags;

    if ( data->disabled )
        goto disabled;

    if ( nestedhvm_vcpu_in_guestmode(v) )
        p_asid->asid = data->max_asid; // this wants to be the max_domid + 1
    else
        p_asid->asid = d->domain_id + 1;

    /* Overflow of ASIDs never happens, but if it does disable its usage. */
    if ( unlikely(p_asid->asid > data->max_asid) )
    {
        data->disabled = 1;
        goto disabled;
    }

    flags = (p_asid->need_flush == 0xff) ? TLB_CTRL_NO_FLUSH : p_asid->need_flush;
    p_asid->need_flush = 0xff;

    printk("v%d - asid = %d need_flush = %d\n",
           v->vcpu_id, p_asid->asid, flags);
    return flags;
disabled:
    p_asid->asid = 0;
    p_asid->need_flush = TLB_CTRL_NO_FLUSH;
    return 0;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
