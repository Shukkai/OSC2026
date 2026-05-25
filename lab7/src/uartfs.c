#include "uartfs.h"
#include "mm.h"
#include "uart.h"

struct vnode_operations uartfs_vops = {
    .lookup = uartfs_lookup,
    .create = uartfs_create,
    .mkdir  = uartfs_mkdir,
};

struct file_operations uartfs_fops = {
    .open  = uartfs_open,
    .close = uartfs_close,
    .read  = uartfs_read,
    .write = uartfs_write,
};

static struct vnode *uartfs_new_vnode(void) {
    struct vnode *v = (struct vnode *)kmalloc(sizeof(struct vnode));
    if (!v) return NULL;

    v->mount    = NULL;
    v->v_ops    = &uartfs_vops;
    v->f_ops    = &uartfs_fops;
    v->internal = NULL;
    return v;
}

int uartfs_lookup(struct vnode *dir_node, struct vnode **target,
                  const char *component_name) {
    (void)dir_node;
    (void)target;
    (void)component_name;
    return -1;
}

int uartfs_create(struct vnode *dir_node, struct vnode **target,
                  const char *component_name) {
    (void)dir_node;
    (void)target;
    (void)component_name;
    return -1;
}

int uartfs_mkdir(struct vnode *dir_node, struct vnode **target,
                 const char *component_name) {
    (void)dir_node;
    (void)target;
    (void)component_name;
    return -1;
}

int uartfs_open(struct vnode *file_node, struct file **target) {
    (*target)->vnode = file_node;
    (*target)->f_ops = file_node->f_ops;
    (*target)->f_pos = 0;
    return 0;
}

int uartfs_close(struct file *file) {
    kfree(file);
    return 0;
}

int uartfs_read(struct file *file, void *buf, size_t len) {
    (void)file;
    char *out = (char *)buf;

    for (size_t i = 0; i < len; i++)
        out[i] = uart_getc();

    return (int)len;
}

int uartfs_write(struct file *file, const void *buf, size_t len) {
    (void)file;
    const char *in = (const char *)buf;

    for (size_t i = 0; i < len; i++)
        uart_putc(in[i]);

    return (int)len;
}

int uartfs_setup_mount(struct filesystem *fs, struct mount *mnt) {
    struct vnode *root = uartfs_new_vnode();
    if (!root) return -1;

    mnt->root = root;
    mnt->fs   = fs;
    return 0;
}

int uartfs_register(void) {
    struct filesystem fs;
    fs.name        = "uartfs";
    fs.setup_mount = uartfs_setup_mount;
    return register_filesystem(&fs);
}
