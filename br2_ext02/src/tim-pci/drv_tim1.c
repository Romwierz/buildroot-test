/* Very simplified WZTIM1 device driver
 * This driver does not allow to control multiple
 * instances of WZTIM1
 * 
 * Copyright (C) 2025 by Wojciech M. Zabolotny
 * wzab<at>ise.pw.edu.pl
 * Significantly based on multiple drivers included in
 * sources of Linux
 * Therefore this source is licensed under GPL v2
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


#define SUCCESS 0
#define DEVICE_NAME "wzab_tim1"

//PCI IDs below are not registred! Use only for experiments!
#define PCI_VENDOR_ID_WZAB 0xabba
#define PCI_DEVICE_ID_WZAB_WZTIM1 0x0123


static const struct pci_device_id tst1_pci_tbl[] = {
{PCI_DEVICE(PCI_VENDOR_ID_WZAB, PCI_DEVICE_ID_WZAB_WZTIM1)},
{}
};
MODULE_DEVICE_TABLE(pci, tst1_pci_tbl);

//Global variables used to store information about WZTIM1
//This must be changed, if we'd like to handle multiple WZTIM1 instances
int irq=0; //IRQ used by WZTIM1 
unsigned long phys_addr = 0; //Physical address of register memory 
volatile uint32_t * fmem=NULL; //Pointer to registers area
volatile void * fdata=NULL; //Pointer to data buffer

DECLARE_KFIFO(rd_fifo,uint64_t,128);

void cleanup_tst1( void );
void cleanup_tst1( void );
int init_tst1( void );
static int tst1_open(struct inode *inode, struct file *file);
static int tst1_release(struct inode *inode, struct file *file);
ssize_t tst1_read(struct file *filp,
		  char __user *buf,size_t count, loff_t *off);
ssize_t tst1_write(struct file *filp,
		   const char __user *buf,size_t count, loff_t *off);
int tst1_mmap(struct file *filp, struct vm_area_struct *vma);

int is_open = 0; //Flag informing if the device is open
dev_t my_dev=0;
struct cdev * my_cdev = NULL;
static struct class *class_my_tst = NULL;

/* Queue for reading process */
DECLARE_WAIT_QUEUE_HEAD (readqueue);

/* Interrupt service routine */
irqreturn_t tst1_irq(int irq, void * dev_id)
{
  // First we check if our device requests interrupt
  //printk(KERN_INFO "I'm in interrupt!\n");
  volatile uint32_t status; //Must be volatile to ensure 32-bit access!
  uint64_t val;
  volatile WzTim1Regs * regs;
  regs = (volatile WzTim1Regs *) fmem;
  status = regs->stat;
  if(status & 0x80000000) {
    //Yes, our device requests service
    //Read the counter 
    val = regs->cntl;
    val |= ((uint64_t) regs->cnth) << 32;
    //Put the counter into the FIFO
    kfifo_put(&rd_fifo, val);
    //Clear the interrupt
    regs->cntl = 0;
    //Wake up the reading process
    wake_up_interruptible(&readqueue);
    return IRQ_HANDLED;
  }
  return IRQ_NONE; //Our device does not request interrupt
};


struct file_operations Fops = {
  .owner = THIS_MODULE,
  .read=tst1_read, /* read */
  .write=tst1_write, /* write */
  .open=tst1_open,
  .release=tst1_release,  /* a.k.a. close */
  .llseek=noop_llseek,
  .mmap = tst1_mmap,
};

/* Cleanup resources */
void tst1_remove( struct pci_dev * pdev )
{
  if(my_dev && class_my_tst) {
    device_destroy(class_my_tst,my_dev);
  }
  if(fdata) free_pages((unsigned long)fdata,2);
  if(fmem) iounmap(fmem);
  if(my_cdev) cdev_del(my_cdev);
  my_cdev=NULL;
  unregister_chrdev_region(my_dev, 1);
  if(class_my_tst) {
    class_destroy(class_my_tst);
    class_my_tst=NULL;
  }
  pci_release_regions(pdev);
  //printk(KERN_INFO "drv_tst1 removed!\n");
}

static int tst1_open(struct inode *inode, 
		     struct file *file)
{
  int res=0;
  volatile WzTim1Regs * regs;
  if(is_open) return -EBUSY; //May be opened only once!
  regs = (volatile WzTim1Regs *) fmem;
  nonseekable_open(inode, file);
  kfifo_reset(&rd_fifo); //Remove 
  //res=request_irq(irq,tst1_irq,0,DEVICE_NAME,NULL); //Should be changed for multiple WZTIM1s
  res=request_irq(irq,tst1_irq, IRQF_SHARED | IRQF_NO_THREAD ,DEVICE_NAME,file); //Should be changed for multiple WZTIM1s
  if(res) {
    printk (KERN_INFO "wzab_tim1: I can't connect irq %i error: %d\n", irq,res);
    irq = -1;
  }
  regs->stat = 1; //Unmask interrupts
  return SUCCESS;
}

static int tst1_release(struct inode *inode, 
			struct file *file)
{
  volatile WzTim1Regs *  regs;
  regs = (volatile WzTim1Regs *) fmem;
#ifdef DEBUG
  printk (KERN_INFO "device_release(%p,%p)\n", inode, file);
#endif
  regs->divh = 0; 
  regs->divl = 0; //Disable IRQ
  regs->stat = 0; //Mask interrupt
  if(irq>=0) free_irq(irq,file); //Free interrupt
  is_open=0;
  return SUCCESS;
}

ssize_t tst1_read(struct file *filp,
		  char __user *buf,size_t count, loff_t *off)
{
  uint64_t val;
  if (count != 8) return -EINVAL; //Only 8-byte accesses allowed
  {
    ssize_t res;
    //Interrupts are on, so we should sleep and wait for interrupt
    res=wait_event_interruptible(readqueue,!kfifo_is_empty(&rd_fifo));
    if(res) return res; //Signal received!
  }
  //Read pointers 
  if(!kfifo_get(&rd_fifo,&val)) return -EINVAL; 
  if(copy_to_user(buf,&val,8)) return -EFAULT;
  return 8;
}

ssize_t tst1_write(struct file *filp,
		   const char __user *buf,size_t count, loff_t *off)
{
  uint64_t val;
  int res = 0; // Workaround. In fact wwe should check the returned value...
  volatile WzTim1Regs * regs;
  if (count != 8) return -EINVAL; //Only 8-byte access allowed
  res = __copy_from_user(&val,buf,8);
  regs = (volatile WzTim1Regs * ) fmem;
  regs->divh = val >> 32;
  regs->divl = val & 0xffffffff;  
  return 8;
}	


void tst1_vma_open (struct vm_area_struct * area)
{  }

void tst1_vma_close (struct vm_area_struct * area)
{  }

static struct vm_operations_struct tst1_vm_ops = {
  .open=tst1_vma_open,
  .close=tst1_vma_close,
};

int tst1_mmap(struct file *filp,
	      struct vm_area_struct *vma)
{
    //unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
    //Mapping of registers
    unsigned long physical = phys_addr;
    unsigned long vsize = vma->vm_end - vma->vm_start;
    unsigned long psize = 0x1000; //One page is enough
    //printk(KERN_INFO "start mmap of registers\n");
    if(vsize>psize)
      return -EINVAL;
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    remap_pfn_range(vma,vma->vm_start, physical >> PAGE_SHIFT , vsize, vma->vm_page_prot);
    vma->vm_ops = &tst1_vm_ops;
    tst1_vma_open(vma); //This time no open(vma) was called
    //printk(KERN_INFO "mmap of registers succeeded!\n");
    return 0;
}


static int tst1_probe(struct pci_dev * pdev, const struct pci_device_id *ent)
{
  unsigned long mmio_start, mmio_end, mmio_flags, mmio_len;
  int res = 0;
  //struct resource * resptr = NULL;
  res =  pci_enable_device(pdev);
  if (res) {
    dev_err(&pdev->dev, "Can't enable PCI device, aborting\n");
    res = -ENODEV;
    goto err1;
  }
  mmio_start = pci_resource_start(pdev, 0);
  mmio_end = pci_resource_end(pdev, 0);
  mmio_flags = pci_resource_flags(pdev, 0);
  mmio_len = pci_resource_len(pdev, 0);
  irq = pdev->irq;
  if(irq<0) {
    printk(KERN_ERR "Error reading the IRQ number: %d.\n",irq);
    res=irq;
    goto err1;
  }
  printk(KERN_ALERT "Connected IRQ=%d\n",irq);
  //printk(KERN_INFO "mmio_start=%x, mmio_end=%x, mmio_len=%x, 	irq=%d\n",mmio_start,mmio_end,mmio_len,irq);
  /* make sure PCI base addr 1 is MMIO */
  if (!(mmio_flags & IORESOURCE_MEM)) {
    dev_err(&pdev->dev, "region #1 not an MMIO resource, aborting\n");
    res = -ENODEV;
    goto err1;
  }
  res = pci_request_regions(pdev, DEVICE_NAME);
  if(res) {
    printk(KERN_ERR "Error requesting PCI regions.\n");
    goto err1;
  }
  pci_set_master(pdev);
  class_my_tst = class_create("my_tst_pci");
  if (IS_ERR(class_my_tst)) {
    printk(KERN_ERR "Error creating my_tst class.\n");
    res=PTR_ERR(class_my_tst);
    goto err1;
  }
  /* Alocate device number */
  res=alloc_chrdev_region(&my_dev, 0, 1, DEVICE_NAME);
  if(res) {
    printk (KERN_INFO "Alocation of the device number for %s failed\n",
            DEVICE_NAME);
    goto err1; 
  };
  my_cdev = cdev_alloc( );
  if(my_cdev == NULL) {
    printk (KERN_INFO "Allocation of cdev for %s failed\n",
            DEVICE_NAME);
    goto err1;
  }
  my_cdev->ops = &Fops;
  my_cdev->owner = THIS_MODULE;
  /* Add character device */
  res=cdev_add(my_cdev, my_dev, 1);
  if(res) {
    printk (KERN_INFO "Registration of the device number for %s failed\n",
            DEVICE_NAME);
    goto err1;
  };
  /* Create pointer needed to access registers */
  phys_addr = mmio_start; 
  printk(KERN_ALERT "Connected registers at %lx\n",phys_addr);
  fmem = ioremap(phys_addr, 0x1000); //One page should be enough
  if(!fmem) {
    printk (KERN_INFO "Mapping of memory for %s registers failed\n",
	    DEVICE_NAME);
    res= -ENOMEM;
    goto err1;
  }
  device_create(class_my_tst,NULL,my_dev,NULL,"my_tim%d",MINOR(my_dev));
  printk (KERN_INFO "%s The major device number is %d.\n",
	  "Successful registration.",
	  MAJOR(my_dev));
  return 0;
 err1:
  tst1_remove(pdev);
  return res;
}

struct pci_driver my_driver = {
  .name = DEVICE_NAME,
  .id_table = tst1_pci_tbl,
  .probe = tst1_probe,
  .remove = tst1_remove,
};

static int __init my_init(void)
{
  return pci_register_driver(&my_driver);
}
static void __exit my_exit(void)
{
  pci_unregister_driver(&my_driver);
}

module_init(my_init);
module_exit(my_exit);

