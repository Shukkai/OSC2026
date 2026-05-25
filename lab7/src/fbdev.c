#include "fbdev.h"
#include "fb.h"
#include "mm.h"
#include "string.h"
#include "uart.h"

#define FB_BPP   4   /* XRGB8888: 4 bytes per pixel */
#define FB_SIZE  ((unsigned long)SCREEN_WIDTH * SCREEN_HEIGHT * FB_BPP)

struct vnode_operations fbdev_vops = {
    .lookup = fbdev_lookup,
    .create = fbdev_create,
    .mkdir  = fbdev_mkdir,
};

struct file_operations fbdev_fops = {
    .open    = fbdev_open,
    .close   = fbdev_close,
    .read    = fbdev_read,
    .write   = fbdev_write,
    .lseek64 = fbdev_lseek64,
    .ioctl   = fbdev_ioctl,
};

static struct vnode *fbdev_new_vnode(void) {
    struct vnode *v = (struct vnode *)kmalloc(sizeof(struct vnode));
    if (!v) return NULL;
    v->mount    = NULL;
    v->v_ops    = &fbdev_vops;
    v->f_ops    = &fbdev_fops;
    v->internal = NULL;
    return v;
}

/* directory ops are unused for this single-node device */
int fbdev_lookup(struct vnode *dir_node, struct vnode **target,
                 const char *component_name) {
    (void)dir_node; (void)target; (void)component_name;
    return -1;
}
int fbdev_create(struct vnode *dir_node, struct vnode **target,
                 const char *component_name) {
    (void)dir_node; (void)target; (void)component_name;
    return -1;
}
int fbdev_mkdir(struct vnode *dir_node, struct vnode **target,
                const char *component_name) {
    (void)dir_node; (void)target; (void)component_name;
    return -1;
}

int fbdev_open(struct vnode *file_node, struct file **target) {
    (*target)->vnode = file_node;
    (*target)->f_ops = file_node->f_ops;
    (*target)->f_pos = 0;
    return 0;
}

int fbdev_close(struct file *file) {
    kfree(file);
    return 0;
}

/* write-only device */
int fbdev_read(struct file *file, void *buf, size_t len) {
    (void)file; (void)buf; (void)len;
    return -1;
}

int fbdev_write(struct file *file, const void *buf, size_t len) {
    unsigned long pos = file->f_pos;
    if (pos >= FB_SIZE) return 0;
    if (pos + len > FB_SIZE) len = FB_SIZE - pos;
    if (len == 0) return 0;

    char *dst = (char *)(FB_ADDR + pos);
    const char *src = (const char *)buf;
    for (size_t i = 0; i < len; i++)
        dst[i] = src[i];

    /* flush the written bytes from cache to the framebuffer */
    __sync_synchronize();
    unsigned long start = (unsigned long)dst & ~(64UL - 1);
    unsigned long end   = (unsigned long)dst + len;
    for (unsigned long a = start; a < end; a += 64)
        asm volatile("cbo.clean 0(%0)" :: "r"(a) : "memory");
    __sync_synchronize();

    file->f_pos += len;
    return (int)len;
}

long fbdev_lseek64(struct file *file, long offset, int whence) {
    if (whence != SEEK_SET) return -1;
    if (offset < 0 || (unsigned long)offset > FB_SIZE) return -1;
    file->f_pos = (size_t)offset;
    return offset;
}

int fbdev_ioctl(struct file *file, unsigned long request, void *arg) {
    (void)file;
    if (request != FB_IOCTL_GET_INFO || !arg) return -1;

    struct framebuffer_info *info = (struct framebuffer_info *)arg;
    info->width  = SCREEN_WIDTH;
    info->height = SCREEN_HEIGHT;
    info->bpp    = FB_BPP;
    return 0;
}

int fbdev_setup_mount(struct filesystem *fs, struct mount *mnt) {
    struct vnode *root = fbdev_new_vnode();
    if (!root) return -1;
    mnt->root = root;
    mnt->fs   = fs;
    return 0;
}

int fbdev_register(void) {
    struct filesystem fs;
    fs.name        = "fbdev";
    fs.setup_mount = fbdev_setup_mount;
    return register_filesystem(&fs);
}
