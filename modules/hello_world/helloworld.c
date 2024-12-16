#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("Dual BSD/GPL");

static int helloworld_init(void) {
    printk(KERN_ALERT "Hello World!");
    return 0;
}
static void helloworld_exit(void) {
    printk(KERN_ALERT "Goodbye World!");
}

module_init(helloworld_init);
module_exit(helloworld_exit);
