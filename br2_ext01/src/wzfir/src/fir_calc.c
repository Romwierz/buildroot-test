#include <linux/init.h>
#include <linux/module.h>
/*
 * Ten plik jest na razie nieużywany, ale jest tu i jest kompilowany,
   żeby pokazać możliwość tworzenia modułów jądra kompilowanych
   z kilku plików źródłowych.
 */
void fir_calc(void)
{
   printk(KERN_ERR "To ja - CALC");
}
