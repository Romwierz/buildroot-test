/* Extended version of WZTIM1 device driver
 * Allows to control multiple instances of WZTIM1.

 * Copyright (C) 2025 Wojciech M. Zabolotny wzab<at>ise.pw.edu.pl
 * Copyright (C) 2026 Michal Romsicki
 *
 * Licensed under GPL v2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
MODULE_LICENSE("GPL v2");
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/kfifo.h>
#include "wzab_tim1.h"

#define DEVICE_NAME "wzab_tim1"
#define DEVICE_NR 64

#define PCI_VENDOR_ID_WZAB 0xabba
#define PCI_DEVICE_ID_WZAB_WZTIM1 0x0123

#define DRV_MSG_PREFIX "drv_tim2: "

#define BAR0 0

static int tim2_mmap(struct file *file, struct vm_area_struct *vma);

struct timdev {
    struct pci_dev * pdev;
} mydev;

struct file_operations fops = {
  .mmap = tim2_mmap,
};

static const struct pci_device_id tim2_ids_tbl[] = {
    {PCI_DEVICE(PCI_VENDOR_ID_WZAB, PCI_DEVICE_ID_WZAB_WZTIM1)},
    {}
};
MODULE_DEVICE_TABLE(pci, tim2_ids_tbl);

// @vma: a pointer to the struct describing virtual memory area
static int tim2_mmap(struct file *file, struct vm_area_struct *vma) {
    int status;
    unsigned long vma_size = vma->vm_end - vma->vm_start;
    unsigned long phys_addr = pci_resource_start(mydev.pdev, BAR0);

    if(vma_size > MY_PAGE_SIZE)
        return -EINVAL;

    // Read the physical addr (shifted) of timers' registers into page offset
    vma->vm_pgoff = phys_addr >> PAGE_SHIFT;

    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    status = io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, vma_size, vma->vm_page_prot);
    if(status) {
        printk(DRV_MSG_PREFIX "Can't map registers to userspace memory, aborting\n");
        return -status;
    }

    return 0;
}

// @ent: an entry from the device id table
static int tim2_probe(struct pci_dev * pdev, const struct pci_device_id * ent) {
    int status;
    void __iomem * ptr_bar0;
    unsigned long regs_phy_addr;
    volatile WzTim1Regs * regs;

    mydev.pdev = pdev;
    
    // Create and register a char device
    status = register_chrdev(DEVICE_NR, DEVICE_NAME, &fops);
    if(status < 0) {
        printk(DRV_MSG_PREFIX "Can't allocate device number, aborting\n");
        goto err1;
    }

    // Enable access to the memory space of the device
    status = pcim_enable_device(pdev);
    if(status) {
        dev_err(&pdev->dev, DRV_MSG_PREFIX "Can't enable PCI device, aborting\n");
        // status = -ENODEV;
        goto err1;
    }

    // Get the physical address of timers' registers
    regs_phy_addr = pci_resource_start(pdev, BAR0);
    // Map PCI's BAR0 to the pointer (virtual address)
    ptr_bar0 = pcim_iomap(pdev, BAR0, pci_resource_len(pdev, BAR0));
    if(!regs_phy_addr || !ptr_bar0) {
        dev_err(&pdev->dev, DRV_MSG_PREFIX "Can't map BAR0 of PCI device, aborting\n");
        status = -ENODEV;
        goto err1;
    }
    printk(KERN_ALERT "Connected registers at %lx\n", regs_phy_addr);

    regs = (volatile WzTim1Regs *) ptr_bar0;
    printk(KERN_ALERT "Timer ID=0x%08X\n", regs->id);
    printk(KERN_ALERT "Timer STAT=0x%08X\n", regs->stat);

err1:
    return status;
}

static void tim2_remove(struct pci_dev * pdev) {
    printk(KERN_ALERT "Removing the device\n");
    unregister_chrdev(DEVICE_NR, DEVICE_NAME);
}

struct pci_driver my_driver = {
  .name = DEVICE_NAME,
  .id_table = tim2_ids_tbl,
  .probe = tim2_probe,
  .remove = tim2_remove,
};

static int __init my_init(void) {
  return pci_register_driver(&my_driver);
}

static void __exit my_exit(void) {
  pci_unregister_driver(&my_driver);
}

module_init(my_init);
module_exit(my_exit);
