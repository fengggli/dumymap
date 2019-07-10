#include <linux/compat.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h> // Macros used to mark up functions e.g., __init __exit
#include <linux/kernel.h> // Contains types, macros, functions for the kernel
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h> // Core header for loading LKMs into the kernel
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 13, 0)
#include <asm/cacheflush.h>
#else
#include <asm/set_memory.h>
#endif

/* Linux 5 drops the mode atribute in access_ok */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
#define ACCESS_OK(mode, addr, size) access_ok(mode, addr, size)
#else
#define ACCESS_OK(mode, addr, size) access_ok(addr, size)
#endif

#include <asm/pgtable.h>
#include <asm/uaccess.h>

//#define DEBUG

// #include "dummymap.h"

MODULE_LICENSE("GPL");    ///< The license type -- this affects runtime behavior
MODULE_AUTHOR("Feng Li"); ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("dummy remap module."); ///< The description -- see modinfo
MODULE_VERSION("0.1");                     ///< The version of the module

static int fop_mmap(struct file *file, struct vm_area_struct *vma);

static int dummymap_dev_release(struct inode *inode, struct file *file) {
  pr_info("dummymap: release device");
  return 0;
}

static const struct file_operations dummymap_dev_ops = {
    .owner = THIS_MODULE,
    .llseek = noop_llseek,
    .release = dummymap_dev_release,
    .mmap = fop_mmap,
};

static struct miscdevice dummymap_dev = {
    MISC_DYNAMIC_MINOR,
    // XMS_MINOR,
    "dummymap",
    &dummymap_dev_ops,
};

static void vm_open(struct vm_area_struct *vma) {
  pr_info("dummymap: open vma");
}

static void vm_close(struct vm_area_struct *vma) {
  pr_info("dummymap: close vma");
}

static struct vm_operations_struct dummymap_fops = {
    .open = vm_open,
    .close = vm_close,
};

static int fop_mmap(struct file *file, struct vm_area_struct *vma) {
  size_t map_size;
  void *dma_virtaddr;
  dma_addr_t dma_handle;
  map_size = vma->vm_end - vma->vm_start;

  dma_virtaddr = dma_alloc_coherent(dummymap_dev.this_device, map_size,
                                    &dma_handle, GFP_KERNEL);
  if (!dma_virtaddr) {
    return -ENOMEM;
  }
  vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

  if (remap_pfn_range(vma, vma->vm_start,
                      virt_to_phys(dma_virtaddr) >> PAGE_SHIFT, map_size,
                      vma->vm_page_prot))
    return -ENOMEM;

  vma->vm_ops = &dummymap_fops;
  vm_open(vma);
  return 0;
}

static int __init dummymap_init(void) {
  int r;

  dummymap_dev.mode = S_IRUGO | S_IWUGO; // set permission for /dev/dummymap

  r = misc_register(&dummymap_dev);
  if (r) {
    printk(KERN_ERR "dummymap: misc device register failed\n");
  }
  printk(KERN_INFO "dummymap: loaded\n");

  return 0;
}

static void __exit dummymap_exit(void) {
  printk(KERN_INFO "dummymap: unloaded\n");
  misc_deregister(&dummymap_dev);
}

/** @brief A module must use the module_init() module_exit() macros from
 * linux/init.h, which identify the initialization function at insertion time
 * and the cleanup function (as listed above)
 */
module_init(dummymap_init);
module_exit(dummymap_exit);
