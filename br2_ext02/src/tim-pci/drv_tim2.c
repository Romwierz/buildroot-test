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
#include <linux/list.h>
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

static int tim2_open(struct inode *inode, struct file *file);
static int tim2_release(struct inode *inode, struct file *file);
ssize_t tim2_read(struct file *file, char __user *buf,size_t count, loff_t *off);
ssize_t tim2_write(struct file *file, const char __user *buf,size_t count, loff_t *off);
static int tim2_mmap(struct file *file, struct vm_area_struct *vma);

struct timdev {
    struct pci_dev * pdev;
    int dev_nr;
    unsigned long phys_addr;
    void __iomem * ptr_bar0;
    struct list_head list;
    struct cdev cdev;
};

LIST_HEAD(device_list);
static struct mutex lock;
static int minor_count = 0;

struct file_operations fops = {
    .read=tim2_read,
    .write=tim2_write,
    .open = tim2_open,
    .release = tim2_release,
    .mmap = tim2_mmap,
};

static const struct pci_device_id tim2_ids_tbl[] = {
    {PCI_DEVICE(PCI_VENDOR_ID_WZAB, PCI_DEVICE_ID_WZAB_WZTIM1)},
    {}
};
MODULE_DEVICE_TABLE(pci, tim2_ids_tbl);

DECLARE_KFIFO(rd_fifo, uint64_t, 128);

static int tim2_open(struct inode *inode, struct file *file) {
    struct timdev * mydev;
    dev_t dev_nr = inode->i_rdev;

    list_for_each_entry(mydev, &device_list, list) {
        if(mydev->dev_nr == dev_nr) {
            file->private_data = mydev;
            return 0;
        }
    }
    return -ENODEV;
}

static int tim2_release(struct inode *inode, struct file *file) {
    struct timdev * mydev = file->private_data;
    volatile WzTim1Regs * regs = (volatile WzTim1Regs *) mydev->ptr_bar0;
    printk(KERN_ALERT "Releasing the device with Device Number %d:%d\n",
            MAJOR(mydev->dev_nr), MINOR(mydev->dev_nr));
    regs->divh = 0; 
    regs->divl = 0; // Disable IRQ
    regs->stat = 0; // Mask interrupt
    return 0;
}

ssize_t tim2_read(struct file *file, char __user *buf, size_t count, loff_t *off) {
    uint64_t val;
    if(count != 8) return -EINVAL; // Only 8-byte accesses allowed
    // Read pointers 
    if(!kfifo_get(&rd_fifo, &val)) return -EINVAL; 
    if(copy_to_user(buf, &val, 8)) return -EFAULT;
    return 8;
}

ssize_t tim2_write(struct file *file, const char __user *buf, size_t count, loff_t *off) {
    struct timdev * mydev = file->private_data;
    uint64_t val;
    int res = 0; // Workaround. In fact wwe should check the returned value...
    volatile WzTim1Regs * regs = (volatile WzTim1Regs *) mydev->ptr_bar0;
    if(count != 8) return -EINVAL; // Only 8-byte access allowed
    res = __copy_from_user(&val, buf, 8);
    regs->divh = val >> 32;
    regs->divl = val & 0xffffffff;  
    return 8;
}	

// @vma: a pointer to the struct describing virtual memory area
static int tim2_mmap(struct file *file, struct vm_area_struct *vma) {
    int status = 0;
    unsigned long vma_size = vma->vm_end - vma->vm_start;
    struct timdev * mydev = (struct timdev *) file->private_data;

    if(vma_size > MY_PAGE_SIZE)
        return -EINVAL;

    // Keep in mind that combined size of multiple timers' memory space may not exceed single page size
    // In such case they share the same memory page, so the virtual memory assigned to userspace app is the same
    printk(KERN_ALERT "Physical address: %lx\n", mydev->phys_addr);
    // Read the physical addr of timers' registers shifted into the page offset
    vma->vm_pgoff = mydev->phys_addr >> PAGE_SHIFT;
    printk(KERN_ALERT "Page offset (physical address shifted by PAGE_SHIFT): %lx\n", vma->vm_pgoff);

    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    status = io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, vma_size, vma->vm_page_prot);
    if(status) {
        printk(DRV_MSG_PREFIX "Can't map registers to userspace memory, aborting\n");
        return -status;
    }

    return status;
}

// @ent: an entry from the device id table
static int tim2_probe(struct pci_dev * pdev, const struct pci_device_id * ent) {
    int status = 0;
    volatile WzTim1Regs * regs;
    struct timdev * mydev = devm_kzalloc(&pdev->dev, sizeof(*mydev), GFP_KERNEL);
    if(!mydev)
        return -ENOMEM;

    // Add character device
    mutex_lock(&lock);
    cdev_init(&mydev->cdev, &fops);
    mydev->cdev.owner = THIS_MODULE;
    mydev->dev_nr = MKDEV(DEVICE_NR, minor_count++);
    status = cdev_add(&mydev->cdev, mydev->dev_nr, 1);
    if(status < 0) {
        printk(DRV_MSG_PREFIX "Can't add chardev, aborting\n");
        return status;
    }
    list_add_tail(&mydev->list, &device_list);
    mutex_unlock(&lock);
    printk(KERN_ALERT "Added the device with Device Number %d:%d\n",
            MAJOR(mydev->dev_nr), MINOR(mydev->dev_nr));

    mydev->pdev = pdev;
    
    // Enable access to the memory space of the device
    // todo: if it fails, delete the device from the list, decrement the count, delete cdev
    status = pcim_enable_device(pdev);
    if(status) {
        dev_err(&pdev->dev, DRV_MSG_PREFIX "Can't enable PCI device, aborting\n");
        // status = -ENODEV;
        goto err1;
    }

    pci_set_master(pdev);

    // Get the physical address of timers' registers
    mydev->phys_addr = pci_resource_start(mydev->pdev, BAR0);
    // Map PCI's BAR0 to the pointer (virtual address)
    mydev->ptr_bar0 = pcim_iomap(pdev, BAR0, pci_resource_len(pdev, BAR0));
    if(!mydev->phys_addr || !mydev->ptr_bar0) {
        dev_err(&pdev->dev, DRV_MSG_PREFIX "Can't map BAR0 of PCI device, aborting\n");
        status = -ENODEV;
        goto err1;
    }
    printk(KERN_ALERT "Connected registers at %lx\n", mydev->phys_addr);

    pci_set_drvdata(pdev, mydev);

    regs = (volatile WzTim1Regs *) mydev->ptr_bar0;
    printk(KERN_ALERT "Timer ID=0x%08X\n", regs->id);
    printk(KERN_ALERT "Timer STAT=0x%08X\n", regs->stat);

err1:
    return status;
}

static void tim2_remove(struct pci_dev * pdev) {
    struct timdev * mydev = (struct timdev *) pci_get_drvdata(pdev);
    printk(KERN_ALERT "Removing the device with Device Number %d:%d\n",
            MAJOR(mydev->dev_nr), MINOR(mydev->dev_nr));
    if(mydev) {
        mutex_lock(&lock);
        list_del(&mydev->list);
        cdev_del(&mydev->cdev);
        mutex_unlock(&lock);
    }
}

struct pci_driver my_driver = {
    .name = DEVICE_NAME,
    .id_table = tim2_ids_tbl,
    .probe = tim2_probe,
    .remove = tim2_remove,
};

static int __init my_init(void) {
    int status = 0;
    dev_t dev_nr = MKDEV(DEVICE_NR, 0);

    // Allocate a range of device numbers
    status = register_chrdev_region(dev_nr, MINORMASK + 1, DEVICE_NAME);
    if(status < 0) {
        printk(DRV_MSG_PREFIX "Can't register the device number, aborting\n");
        return status;
    }

    mutex_init(&lock);
    status = pci_register_driver(&my_driver);
    if(status < 0) {
        printk(DRV_MSG_PREFIX "Can't register the device driver, aborting\n");
        unregister_chrdev_region(dev_nr, MINORMASK + 1);
        return status;
    }

    return status;
}

static void __exit my_exit(void) {
    dev_t dev_nr = MKDEV(DEVICE_NR, 0);
	unregister_chrdev_region(dev_nr, MINORMASK + 1);
    pci_unregister_driver(&my_driver);
}

module_init(my_init);
module_exit(my_exit);
