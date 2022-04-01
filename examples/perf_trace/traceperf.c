#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
//#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <linux/perf/riscv_pmu.h>

#include <asm/sbi.h>
#include <asm/hwcap.h>
#include <asm/io.h>


struct perfCtrInfo {
	unsigned int ctrIdx;
	union {
		unsigned long ctrInfo;
		struct {
			unsigned long csr:12;
			unsigned long width:6;
#if __riscv_xlen == 32
			unsigned long reserved:13;
#else
			unsigned long reserved:45;
#endif
			unsigned long type:1;
		};
	};
};

struct perfEvent {
	unsigned int ctrIdx;
	int type;
        union {
        	int code;
                struct {
                	int cache_id;
                        int op_id;
                        int result_id;
                };
	};
	unsigned long event_data;
	unsigned long ctrInfo;
};

static void start_cntrs(void *info)
{
	struct sbiret ret;
	unsigned long cntr_mask;
	int i;

	cntr_mask = (unsigned long)*(uint32_t*)info;

	for (i = 0; cntr_mask != 0; i++) {
		if ((i != 1) && (cntr_mask & 1)) {
			ret = sbi_ecall(SBI_EXT_PMU,SBI_EXT_PMU_COUNTER_START,i,1,0,0,0,0);

//			if (ret.error == SBI_ERR_ALREADY_STARTED) {
//				printk("SBI_EXT_PMU_COUNTER_START: cntr: %d already started, value %ld\n",i,ret.value);
//			}
//                      else if (ret.error) {
//				printk("SBI_EXT_PMU_COUNTER_START: cntr: %d, error %ld\n",i,ret.error);
//			}
//			else {
//				printk("SBI_EXT_PMU_COUNTER_START: cntr: %d, value %ld\n",i,ret.value);
//			}
		}

		cntr_mask >>= 1;
	}
}

static void stop_cntrs(void *info)
{
	struct sbiret ret;
	unsigned long cntr_mask;
	int i;

	cntr_mask = (unsigned long)*(uint32_t*)info;

	for (i = 0; cntr_mask != 0; i++) {
		if (cntr_mask & 1) {
			ret = sbi_ecall(SBI_EXT_PMU,SBI_EXT_PMU_COUNTER_STOP,i,1,SBI_PMU_STOP_FLAG_RESET,0,0,0);

			if (ret.error) {
				printk("stop_cntrs: error: %ld, cntr: %d, mask: 0x%08lx\n",ret.error,i,cntr_mask);
			}
		}
		cntr_mask >>= 1;
	}
}

static void config_perf_counter(void *info)
{
	struct sbiret ret;
	unsigned long cbase;
	unsigned long cmask;
	unsigned long cflags;
	unsigned long hwc_event_base;
	unsigned long hwc_config;
	struct perfEvent *perfEvent;

	perfEvent = (struct perfEvent*)info;

	cflags = 0;

	cmask = 1;
	cbase = (unsigned long)perfEvent->ctrIdx;	// cbase specifies which bit the lsb of cmask represents. Allows more counters than fit in cmask

	switch (perfEvent->type) {
	case 0:
		if (perfEvent->code == 0x80) {
			return;
		}

		// fall through!
	case 15:
		hwc_event_base = (perfEvent->type << 16) | (perfEvent->code << 0);
		hwc_config = 0;
//		hwc_config = hwc_event_base;
		break;
	case 1:
		hwc_event_base = (perfEvent->type << 16) | (perfEvent->cache_id << 3) | (perfEvent->op_id << 1) | (perfEvent->result_id << 0);
		hwc_config = 0;
		break;
	case 2:
		hwc_event_base = (perfEvent->type << 16) | (0 << 0);
		hwc_config = perfEvent->event_data;
		break;
	default:
		return;
	}

	ret = sbi_ecall(SBI_EXT_PMU,SBI_EXT_PMU_COUNTER_CFG_MATCH,cbase,cmask,cflags,hwc_event_base,hwc_config,0);

	if (ret.error) {
		ret = sbi_ecall(SBI_EXT_PMU,SBI_EXT_PMU_COUNTER_CFG_MATCH,cbase,cmask,cflags | SBI_PMU_CFG_FLAG_SKIP_MATCH,hwc_event_base,hwc_config,0);

	}
}

static void enable_cntr_access(void *info)
{
	unsigned long cntr_mask;

	cntr_mask = (unsigned long)*(uint32_t*)info;

	cntr_mask |= csr_read(CSR_SCOUNTEREN);

	csr_write(CSR_SCOUNTEREN,cntr_mask);

	// read back what was written to see what ones are actually allowed

        cntr_mask = csr_read(CSR_SCOUNTEREN);
}

static void disable_cntr_access(void *info)
{
	unsigned long cntr_mask;

	cntr_mask = (unsigned long)*(uint32_t*)info;

	cntr_mask = ~cntr_mask & (unsigned long)csr_read(CSR_SCOUNTEREN);

	csr_write(CSR_SCOUNTEREN,cntr_mask);

	// read back what was written to see what ones are actually allowed

        cntr_mask = (unsigned long)csr_read(CSR_SCOUNTEREN);
}

//void __iomem *ioremap(phys_addr_t addr, size_t size);
//void iounmap(void __iomem *addr);

static int __iomem *ip;
static int major;
static struct cdev *tp_cdev;
static void *sba_dma_buffer_addr;
static uint32_t sba_dma_buffer_size;

#define PERF_IOCTL_NONE				0
#define PERF_IOCTL_GET_HW_CNTR_MASK		100
#define PERF_IOCTL_START_CNTRS			101
#define PERF_IOCTL_STOP_CNTRS			102
#define PERF_IOCTL_CFG_EVENT_CNTR		103
#define PERF_IOCTL_ENABLE_HW_CNTR_ACCESS	104
#define PERF_IOCTL_DISABLE_HW_CNTR_ACCESS	105
#define PERF_IOCTL_ALLOC_SBA_DMA_BUFFER    	106
#define PERF_IOCTL_FREE_SBA_DMA_BUFFER     	107
#define PERF_IOCTL_GET_SBA_BUFFER_PHYS_ADDR	108
#define PERF_IOCTL_GET_SBA_BUFFER_SIZE		109
#define PERF_IOCTL_READ_SBA_BUFFER		110
#define PERF_IOCTL_GET_EVENT_CNTR_INFO		111

static long tp_ioctl(struct file *filp,unsigned int cmd,unsigned long arg)
{
	uint32_t cm;
	unsigned long sba_bus_addr;
	struct sbiret ret;
	unsigned long c;

	switch (cmd) {
	case PERF_IOCTL_NONE:
		// this is a nop
//		printk("PERF_IOCTL_NONE\n");
		break;
	case PERF_IOCTL_GET_HW_CNTR_MASK:
		uint32_t hwCtrMask;
		unsigned int nCtrs;
		int i;

//		printk("PERF_IOCTL_GET_HW_CNTR_MASK\n");

		if (access_ok(arg,sizeof(uint32_t)) == 0) {
			return -EFAULT;
		}

		// First, get the number of counters

		ret = sbi_ecall(SBI_EXT_PMU,SBI_EXT_PMU_NUM_COUNTERS,0,0,0,0,0,0);
		if (ret.error) {
			nCtrs = 0;

			return sbi_err_map_linux_errno(ret.error);
		}

		nCtrs = ret.value;
		hwCtrMask = 0;

		// Cycle through each counter and determine if it is HW or FW

		for (i = 0; i < nCtrs; i++) {
			ret = sbi_ecall(SBI_EXT_PMU,SBI_EXT_PMU_COUNTER_GET_INFO,i,0,0,0,0,0);

			if (ret.error == 0) {
				struct perfCtrInfo cinfo;

				cinfo.ctrInfo = ret.value;
				if (cinfo.type == SBI_PMU_CTR_TYPE_HW) {
					hwCtrMask |= 1 << i;
				}
			}
		}

		put_user(hwCtrMask,(uint32_t*)arg);
		break;
	case PERF_IOCTL_START_CNTRS:
//		printk("PERF_IOCTL_START_CNTRS\n");

		if (access_ok(arg,sizeof(uint32_t)) == 0) {
			return -EFAULT;
		}

		get_user(cm,(uint32_t*)arg);
		
		on_each_cpu(start_cntrs,&cm,1);
		break;
	case PERF_IOCTL_STOP_CNTRS:
//		printk("PERF_IOCTL_STOP_CNTRS\n");

		if (access_ok(arg,sizeof(uint32_t)) == 0) {
			return -EFAULT;
		}

		get_user(cm,(uint32_t*)arg);
		
		on_each_cpu(stop_cntrs,&cm,1);
		break;
	case PERF_IOCTL_CFG_EVENT_CNTR:
		struct perfEvent perfEvent;
		int rc;

//		printk("PERF_IOCTL_CFG_EVENT_CNTR\n");

		rc = copy_from_user(&perfEvent,(void*)arg,sizeof(struct perfEvent));
		if (rc != 0) {
			return -EAGAIN;
		}
		
		on_each_cpu(config_perf_counter,&perfEvent,1);

		if (perfEvent.ctrIdx != 1) {
			ret = sbi_ecall(SBI_EXT_PMU,SBI_EXT_PMU_COUNTER_GET_INFO,perfEvent.ctrIdx,0,0,0,0,0);
			if (ret.error) {
				printk("Error: SBI_EXT_PMU_COUNTER_GET_INFO: counter %d\n",perfEvent.ctrIdx);
				return -EFAULT;
			}

			perfEvent.ctrInfo = ret.value;
		}
		else {
			perfEvent.ctrInfo = (1UL << 63) | (63UL << 12) | (0xc01UL);
		}

		c = copy_to_user((void*)arg,&perfEvent,sizeof(struct perfEvent));
		break;
	case PERF_IOCTL_ENABLE_HW_CNTR_ACCESS:
//		printk("PERF_IOCTL_ENABLE_CNTR_ACCESS\n");

		if (access_ok(arg,sizeof(uint32_t)) == 0) {
			return -EFAULT;
		}

		get_user(cm,(uint32_t*)arg);

		on_each_cpu(enable_cntr_access,&cm,1);

		cm = (unsigned int)csr_read(CSR_SCOUNTEREN);

		put_user(cm,(unsigned int*)arg);
		break;
	case PERF_IOCTL_DISABLE_HW_CNTR_ACCESS:
//		printk("PERF_IOCTL_DISABLE_CNTR_ACCESS\n");

		if (access_ok(arg,sizeof(uint32_t)) == 0) {
			return -EFAULT;
		}

		get_user(cm,(uint32_t*)arg);

		on_each_cpu(disable_cntr_access,&cm,1);

		cm = (unsigned int)csr_read(CSR_SCOUNTEREN);

		put_user(cm,(unsigned int*)arg);
		break;
	case PERF_IOCTL_ALLOC_SBA_DMA_BUFFER:
		if (access_ok(arg,sizeof(uint64_t)) == 0) {
			return -EFAULT;
		}

//		printk("ALLOC_SBA_DMA_BUFFER\n");

		// need to check if we need to unmap mem!

		if (sba_dma_buffer_addr != NULL) {
			kfree(sba_dma_buffer_addr);
			sba_dma_buffer_addr = NULL;
			sba_dma_buffer_size = 0;
		}

		get_user(sba_dma_buffer_size,(uint32_t*)arg);
		
//printk("requesting %d bytes\n",sba_dma_buffer_size);

		sba_dma_buffer_addr = kmalloc((size_t)sba_dma_buffer_size,GFP_KERNEL | __GFP_NORETRY | __GFP_DMA);
		if (sba_dma_buffer_addr == NULL) {
			printk("PERF_IOCTL_ALLOC_SBA_DMA_BUFFER: kmalloc failed\n");

			sba_dma_buffer_size = 0;

			return -EAGAIN;
		}

		sba_bus_addr = (unsigned long)virt_to_phys(sba_dma_buffer_addr);

		put_user((unsigned long)sba_bus_addr,(unsigned long*)arg);

		break;
	case PERF_IOCTL_FREE_SBA_DMA_BUFFER:
		if (sba_dma_buffer_addr != NULL) {
			kfree(sba_dma_buffer_addr);
		}

		sba_dma_buffer_addr = NULL;
		sba_dma_buffer_size = 0;
		break;
	case PERF_IOCTL_GET_SBA_BUFFER_PHYS_ADDR:
		if (access_ok(arg,sizeof(unsigned long)) == 0) {
			return -EFAULT;
		}

		sba_bus_addr = (unsigned long)virt_to_phys(sba_dma_buffer_addr);

		put_user(sba_bus_addr,(unsigned long*)arg);
		break;
	case PERF_IOCTL_GET_SBA_BUFFER_SIZE:
		if (access_ok(arg,sizeof(uint32_t)) == 0) {
			return -EFAULT;
		}

		put_user(sba_dma_buffer_size,(uint32_t*)arg);
		break;
	case PERF_IOCTL_READ_SBA_BUFFER:
		void *ksrc;

		struct addrnsize {
			unsigned long addr;
			uint32_t size;
		} udst;

//		printk("IOCTL_READ_SBA_BUFFER\n");

		c = copy_from_user(&udst,(void*)arg,sizeof udst);

//printk("READ_SBA_BUFFER: requesting %u bytes to address %08lx\n",udst.size,udst.addr);

		ksrc = sba_dma_buffer_addr;

		if (udst.size > sba_dma_buffer_size) {
			udst.size = sba_dma_buffer_size;
		}

		c = copy_to_user((void*)udst.addr,ksrc,udst.size);

//printk("READ_SBA_BUFFER: copyied %u bytes\n",udst.size);

		c = copy_to_user((void*)arg,&udst,sizeof udst);
		break;
	case PERF_IOCTL_GET_EVENT_CNTR_INFO:
		struct perfCtrInfo cinfo;

//		printk("PERF_IOCTL_GET_EVENT_CNTR_INFO\n");

		if (access_ok(arg,sizeof(struct perfCtrInfo)) == 0) {
			return -EFAULT;
		}

		c = copy_from_user(&cinfo,(void*)arg,sizeof(struct perfCtrInfo));
			
		ret = sbi_ecall(SBI_EXT_PMU,SBI_EXT_PMU_COUNTER_GET_INFO,cinfo.ctrIdx,0,0,0,0,0);
		if (ret.error) {
			printk("Error: SBI_EXT_PMU_COUNTER_GET_INFO: counter %d\n",cinfo.ctrIdx);
			return -EFAULT;
		}

		cinfo.ctrInfo = ret.value;

		c = copy_to_user((void*)arg,&cinfo,sizeof(struct perfCtrInfo));
		break;
	default:
		printk("traceperf: Unknown ioctl() cmd: %d\n",cmd);
		return -ENOTTY;
	}

	return 0;
}

static void tp_vma_open(struct vm_area_struct *vma)
{
//	printk("traceperf VMA open, virt %lx, phys %lx\n",vma->vm_start, vma->vm_pgoff << PAGE_SHIFT);
}

static void tp_vma_close(struct vm_area_struct *vma)
{
//	printk("traceperf vma close.\n");
}

static struct vm_operations_struct tp_remap_vm_ops = {
	.open = tp_vma_open,
	.close = tp_vma_close,
};

static int tp_remap_mmap(struct file *filp,struct vm_area_struct *vma)
{
//	 printk("tp_remap_mmap()\n");

        // should this be io_remap????
        // or maybe ioremap_pfn_range(), and also request_mem_region()???

        // check vp_page_prot for cacheable flag?? struct vm_area_struct mv_flags and vm_page_prot flags!!
        // look for ways to make the memory non-cachable!!
        // look at pg_prot_t

        // also, check out __get_free_pages() for direct use.
        // unsigned long __get_free_pages(int priority,unsigned long gfporder,int dma);

        // try checking mm.h, asm/page.h, asm/pgtable.h
        // also funciton pgprot_noncached();
        // as in: vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot); right before the call
        // to [io_]remap_pfn_range();

        // if needed, check asm/cacheflush.h for possible ways to flush the cache

	if (remap_pfn_range(vma,vma->vm_start,vma->vm_pgoff,vma->vm_end - vma->vm_start,vma->vm_page_prot)) {
		printk("remap_pfn_rang(): failed. start: 0x%08lx end: 0x%08lx\n",(unsigned long)vma->vm_start,(unsigned long)vma->vm_end);
		return -EAGAIN;
	}

	vma->vm_ops = &tp_remap_vm_ops;
	tp_vma_open(vma);

	return 0;
}

static struct file_operations tp_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = tp_ioctl,
	.mmap = tp_remap_mmap,
};

static void trace_release(struct device *dev)
{
//	printk("trace release\n");
}

int init_module(void)
{
    int rc;
    dev_t tp_dev;

    sba_dma_buffer_addr = NULL;
    sba_dma_buffer_size = 0;

    // setup the major/minor numbers

    tp_dev = MKDEV(0,0);

    rc = alloc_chrdev_region(&tp_dev,0,1,"traceperf");
    if (rc < 0) {
        printk("traceperf: alloc_chardev_region() failed\n");
        return rc;
    }

    major = MAJOR(tp_dev);

//    printk("traceper: major: %d, minor: %d\n",major,MINOR(tp_dev));

    printk("Loaded traceperf\n");

    // setup the cdev struct

    tp_cdev = cdev_alloc();
    cdev_init(tp_cdev,&tp_fops);
    tp_cdev->owner = THIS_MODULE;
    rc = cdev_add(tp_cdev,tp_dev,1);

    if (rc < 0) {
        printk("cdev_add() failed\n");
        return rc;
    }

    // the device is not live

    return 0;
}

void cleanup_module(void)
{
	unsigned long cntr_mask;

	// unregister device

	cdev_del(tp_cdev);

	// device is now unusable - cleanup

        cntr_mask = csr_read(CSR_SCOUNTEREN);

	// third arg to on_each_cpu() is a wait flag. True waits for all or each?? to finisho
	// before proceeding

	on_each_cpu(disable_cntr_access,&cntr_mask,1);

	if (sba_dma_buffer_addr != NULL) {
		kfree(sba_dma_buffer_addr);
		sba_dma_buffer_addr = NULL;
		sba_dma_buffer_size = 0;
	}

	printk("Unloaded traceperf (cntrmask: 0x%08lx)\n",cntr_mask);

	if (ip != NULL) {
		iounmap(ip);
	}

	printk("unregistering major: %d\n",major);

	unregister_chrdev_region(MKDEV(major,0),1);
}

MODULE_LICENSE("GPL");
