/* The kernel call implemented in this file:
 *   m_type:	SYS_VMCTL
 *
 * The parameters for this kernel call are:
 *   	SVMCTL_WHO	which process
 *    	SVMCTL_PARAM	set this setting (VMCTL_*)
 *    	SVMCTL_VALUE	to this value
 */

#include "../system.h"
#include "../vm.h"
#include "../debug.h"
#include <assert.h>
#include <minix/type.h>

/*===========================================================================*
 *				do_vmctl				     *
 *===========================================================================*/
PUBLIC int do_vmctl(struct proc * caller, message * m_ptr)
{
  int proc_nr;
  endpoint_t ep = m_ptr->SVMCTL_WHO;
  struct proc *p, *rp, *target;
  int err;

  if(ep == SELF) { ep = m_ptr->m_source; }

  if(!isokendpt(ep, &proc_nr)) {
	printf("do_vmctl: unexpected endpoint %d from VM\n", ep);
	return EINVAL;
  }

  p = proc_addr(proc_nr);

  switch(m_ptr->SVMCTL_PARAM) {
	case VMCTL_CLEAR_PAGEFAULT:
		assert(RTS_ISSET(p,RTS_PAGEFAULT));
		RTS_UNSET(p, RTS_PAGEFAULT);
		return OK;
	case VMCTL_MEMREQ_GET:
		/* Send VM the information about the memory request.  */
		if(!(rp = vmrequest))
			return ESRCH;
		assert(RTS_ISSET(rp, RTS_VMREQUEST));

		okendpt(rp->p_vmrequest.target, &proc_nr);
		target = proc_addr(proc_nr);

		/* Reply with request fields. */
		switch(rp->p_vmrequest.req_type) {
		case VMPTYPE_CHECK:
			m_ptr->SVMCTL_MRG_TARGET	=
				rp->p_vmrequest.target;
			m_ptr->SVMCTL_MRG_ADDR		=
				rp->p_vmrequest.params.check.start;
			m_ptr->SVMCTL_MRG_LENGTH	=
				rp->p_vmrequest.params.check.length;
			m_ptr->SVMCTL_MRG_FLAG		=
				rp->p_vmrequest.params.check.writeflag;
			m_ptr->SVMCTL_MRG_REQUESTOR	=
				(void *) rp->p_endpoint;
			break;
		case VMPTYPE_SMAP:
		case VMPTYPE_SUNMAP:
		case VMPTYPE_COWMAP:
			assert(RTS_ISSET(target,RTS_VMREQTARGET));
			RTS_UNSET(target, RTS_VMREQTARGET);
			m_ptr->SVMCTL_MRG_TARGET	=
				rp->p_vmrequest.target;
			m_ptr->SVMCTL_MRG_ADDR		=
				rp->p_vmrequest.params.map.vir_d;
			m_ptr->SVMCTL_MRG_EP2		=
				rp->p_vmrequest.params.map.ep_s;
			m_ptr->SVMCTL_MRG_ADDR2		=
				rp->p_vmrequest.params.map.vir_s;
			m_ptr->SVMCTL_MRG_LENGTH	=
				rp->p_vmrequest.params.map.length;
			m_ptr->SVMCTL_MRG_FLAG		=
				rp->p_vmrequest.params.map.writeflag;
			m_ptr->SVMCTL_MRG_REQUESTOR	=
				(void *) rp->p_endpoint;
			break;
		default:
			panic("VMREQUEST wrong type");
		}

		rp->p_vmrequest.vmresult = VMSUSPEND;

		/* Remove from request chain. */
		vmrequest = vmrequest->p_vmrequest.nextrequestor;

		return rp->p_vmrequest.req_type;
	case VMCTL_MEMREQ_REPLY:
		assert(RTS_ISSET(p, RTS_VMREQUEST));
		assert(p->p_vmrequest.vmresult == VMSUSPEND);
  		okendpt(p->p_vmrequest.target, &proc_nr);
		target = proc_addr(proc_nr);
		p->p_vmrequest.vmresult = m_ptr->SVMCTL_VALUE;
		assert(p->p_vmrequest.vmresult != VMSUSPEND);

		switch(p->p_vmrequest.type) {
		case VMSTYPE_KERNELCALL:
			/*
			 * we will have to resume execution of the kernel call
			 * as soon the scheduler picks up this process again
			 */
			p->p_misc_flags |= MF_KCALL_RESUME;
			break;
		case VMSTYPE_DELIVERMSG:
			assert(p->p_misc_flags & MF_DELIVERMSG);
			assert(p == target);
			assert(RTS_ISSET(p, RTS_VMREQUEST));
			break;
		case VMSTYPE_MAP:
			assert(RTS_ISSET(p, RTS_VMREQUEST));
			break;
		default:
			panic("strange request type: %d",p->p_vmrequest.type);
		}

		RTS_UNSET(p, RTS_VMREQUEST);
		return OK;

	case VMCTL_ENABLE_PAGING:
		if(vm_running) 
			panic("do_vmctl: paging already enabled");
		vm_init(p);
		if(!vm_running)
			panic("do_vmctl: paging enabling failed");
		return arch_enable_paging(caller, m_ptr);
	case VMCTL_KERN_PHYSMAP:
	{
		int i = m_ptr->SVMCTL_VALUE;
		return arch_phys_map(i,
			(phys_bytes *) &m_ptr->SVMCTL_MAP_PHYS_ADDR,
			(phys_bytes *) &m_ptr->SVMCTL_MAP_PHYS_LEN,
			&m_ptr->SVMCTL_MAP_FLAGS);
	}
	case VMCTL_KERN_MAP_REPLY:
	{
		return arch_phys_map_reply(m_ptr->SVMCTL_VALUE,
			(vir_bytes) m_ptr->SVMCTL_MAP_VIR_ADDR);
	}
  }

  /* Try architecture-specific vmctls. */
  return arch_do_vmctl(m_ptr, p);
}
