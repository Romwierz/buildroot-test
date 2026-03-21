#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include "fir_dev.h"
#include "fir_ioctl.h"

MODULE_LICENSE("GPL");


/* Parametr definiujący liczbę tworzonych urządzeń */
int ndevs = 4;
module_param(ndevs,int,0);
MODULE_PARM_DESC(ndevs,"Liczba tworzonych emulowanych filtrów");
/* Wskaźnik na tablicę przechowującą stany urządzeń */
struct fir_dev * fir_ctx = NULL;

#define SUCCESS 0
#define DEVICE_NAME "emu_fir"
#define BUF_LEN 100
char my_buf[BUF_LEN];

dev_t my_dev=0;
struct cdev *my_cdev = NULL;
static struct class *my_class = NULL;

long my_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int res = 0;
    /* Uwaga! W kodzie produkcyjnym bezwzględnie powinniśmy sprawdzić,
    czy wskaźniki ff i fd wskazują na sensowne dane! */
    struct fir_file * ff = filp->private_data;
    struct fir_dev * fd = ff->dev_ctx;
    printk(KERN_ERR "IOCTL cmd: %x , arg: %lx, fd: %p\n", cmd,arg,fd);
    /* testy są uproszczone, ponieważ obsługujemy tylko trzy proste komendy */

    switch(cmd) {
    case FIR_IO_DEL:
        /* Ta komenda ustawia opóźnienie przetwarzania danych w milisekundach - na poziomie urządzenia! */
        fd->delay = ((int) arg * HZ)/1000;
        break;
    case FIR_IO_COEFFS:
        /* Ta komenda ustawia współczynniki filtru */
    {
        int16_t * dta = (int16_t *) arg;
        int16_t ncoeffs;
        ff->ntaps = 0;
        if(ff->coeffs) {
            kfree(ff->coeffs);
            ff->coeffs = NULL;
        }
        if(ff->samples) {
            kfree(ff->samples);
            ff->samples = NULL;
        }

        if(get_user(ncoeffs,dta))
            return -EFAULT;
        ff->ntaps = ncoeffs;
        ff->first = 0;
        /* Poniżej używamy funkcji alokacji pamięci w sterowniku */
        ff->coeffs = kzalloc(ncoeffs*sizeof(int16_t),GFP_KERNEL);
        if(!ff->coeffs) return -ENOMEM;
        ff->samples = kzalloc(ncoeffs*sizeof(int16_t),GFP_KERNEL);
        if(!ff->samples) return -ENOMEM;
        if(copy_from_user(ff->coeffs,&dta[1],ncoeffs*sizeof(int16_t)))
            return -EFAULT;
        ff->ntaps = ncoeffs;
    }
    break;
    case FIR_IO_SAMPLES:
        /* Tu pozwalam sobie na poważne uproszczenie - obliczenia przeprowadzamy natychmiast,
         *  działając w kontekście procesu. Dzięki temu możemy wprost odwoływać się do pamięci. */
    {
        int16_t * dta = (int16_t *) arg;
        int16_t nsamples;
        int i;
        if(ff->ntaps == 0) {
            printk(KERN_ERR "Filtr niezainicjalizowany?");
            return -EINVAL;
        }
        /* Blokujemy innym procesom dostęp do filtru */
        down(&fd->lock);
        if(get_user(nsamples,dta)) {
            up(&fd->lock);
            return -EFAULT;
        }
        for(i=0; i < nsamples; i++) {
            int16_t smp,val;
            int j,k;
            if(get_user(smp,&dta[i+1])) {
                up(&fd->lock);
                return -EFAULT;
            }
            ff->samples[ff->first] = smp;
            k = ff->first;
            val = 0;
            for(j=0; j<ff->ntaps; j++) {
                val += ff->coeffs[j] * ff->samples[k];
                k--;
                if(k<0) k = ff->ntaps-1;
            }
            /* skalujemy wynik */
            val >>= 8;
            if(put_user(val,&dta[i+1])) {
                up(&fd->lock);
                return -EFAULT;
            }
            ff->first --;
            if(ff->first<0)
                ff->first=ff->ntaps-1;
        }
        /* opóźniamy wykonanie komendy */
        set_current_state(TASK_UNINTERRUPTIBLE);
        schedule_timeout(fd->delay);
        /* odblokowujemy dostęp innym procesom */
        up(&fd->lock);
    }
    break;
    default:
        /* Błędna komenda */
        return -EINVAL;
    }
    return res;
}

static int my_open(struct inode *inode, struct file *file)
{
    /* Tworzymy strukturę opisującą użycie akceleratora */
    struct fir_file * ff = kzalloc(sizeof(*ff),GFP_KERNEL);
    printk(KERN_ERR "Open wywołane.\n");
    if(!ff) {
        printk(KERN_ERR "Brak pamięci na strukturę fir_file.\n");
        return -ENOMEM;
    }
    {
        int ndev = MINOR(file->f_inode->i_rdev);
        ff->dev_ctx = &fir_ctx[ndev];
    }
    file -> private_data = ff;
    printk(KERN_ERR "Adres ff=%p, fd=%p\n",ff,ff->dev_ctx);
    return SUCCESS;
}

static int my_release(struct inode *inode, struct file *file)
{
    struct fir_file * ff = file->private_data;
    if(ff) {
        if(ff->coeffs) kfree(ff->coeffs);
        if(ff->samples) kfree(ff->samples);
        kfree(ff);
        file->private_data = NULL;
    }
    return SUCCESS;
}

struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = my_ioctl,
    .open=my_open,
    .release=my_release,
};

static void my_cleanup(void)
{
    int i;
    printk(KERN_ALERT "my_fir usunięty\n");
    /* Usuwamy pliki specjalne urządzeń */
    if(my_dev && my_class) {
        for (i=0; i<ndevs; i++) {
            device_destroy(my_class,MKDEV(MAJOR(my_dev),MINOR(my_dev)+i));
        }
    }
    /* Usuwamy urządzenie z klasy */
    if(my_cdev) cdev_del(my_cdev);
    my_cdev=NULL;
    /* Zwalniamy numer urządzenia */
    unregister_chrdev_region(my_dev, ndevs);
    /* Wyrejestrowujemy klasę */
    if(my_class) {
        class_destroy(my_class);
        my_class=NULL;
    }
    /* Zwalniamy dane ze stanami urządzeń */
    kfree(fir_ctx);
    fir_ctx = NULL;
}


static int my_init(void)
{
    int res;
    int i;
    printk(KERN_ALERT "my_fir załadowany\n");
    /* Tworzymy tablicę na stany urządzeń */
    fir_ctx = kzalloc(ndevs*sizeof(*fir_ctx),GFP_KERNEL);
    if (!fir_ctx) {
        printk(KERN_ERR "Brak pamięci na utworzenie tablicy ze stanami urządzeń");
        res = -ENOMEM;
        goto err1;
    }
    /* Inicjalizujemy stany urządzeń */
    for(i=0; i<ndevs; i++) {
        fir_ctx[i].delay = 2*HZ; //2 sekundy
        sema_init(&fir_ctx[i].lock,1);
    }
    /* Tworzymy klasę opisującą nasze urządzenie - aby móc współpracować z systemem udev */
    my_class = class_create("fir_class");
    if (IS_ERR(my_class)) {
        printk(KERN_ERR "Błąd tworzenia klasy fir_class.\n");
        res=PTR_ERR(my_class);
        goto err1;
    }  /* Alokujemy numer urządzenia */
    res=alloc_chrdev_region(&my_dev, 0, ndevs, DEVICE_NAME);
    if(res) {
        printk ("<1>Alokacja numerów urządzeń %s nieudana.\n",
                DEVICE_NAME);
        goto err1;
    };
    my_cdev = cdev_alloc();
    if (my_cdev==NULL) {
        printk (KERN_ERR "Alokacja struktury cdev dla %s nieudana.\n", DEVICE_NAME);
        res = -ENODEV;
        goto err1;
    }
    my_cdev->ops = &fops;
    my_cdev->owner = THIS_MODULE;
    /* Dodajemy urządzenie znakowe do systemu */
    res=cdev_add(my_cdev, my_dev, ndevs);
    if(res) {
        printk (KERN_ERR "Rejestracja numerów urządzeń dla %s nieudana.\n",
                DEVICE_NAME);
        goto err1;
    };
    /* Tworzymy nasze urządzenia */
    for(i = 0; i < ndevs ; i++ ) {
        dev_t curr_dev = MKDEV(MAJOR(my_dev), MINOR(my_dev) + i);
        device_create(my_class,NULL,curr_dev,NULL,"fir%d",MINOR(my_dev)+i);
    }
    printk (KERN_ALERT "Rejestracja udana. Główny numer urządzenia %s to %d.\n",
            DEVICE_NAME,
            MAJOR(my_dev));
    return SUCCESS;
err1:
    my_cleanup();
    return res;
}




module_init(my_init);
module_exit(my_cleanup);

