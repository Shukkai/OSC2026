#pragma once

#include <stddef.h>
#include "vfs.h"

#define FB_IOCTL_GET_INFO 0

struct framebuffer_info {
    unsigned int width;
    unsigned int height;
    unsigned int bpp;   /* bytes per pixel */
};

extern struct vnode_operations fbdev_vops;
extern struct file_operations  fbdev_fops;

int fbdev_register(void);
int fbdev_setup_mount(struct filesystem *fs, struct mount *mnt);

int  fbdev_lookup(struct vnode *dir_node, struct vnode **target,
                  const char *component_name);
int  fbdev_create(struct vnode *dir_node, struct vnode **target,
                  const char *component_name);
int  fbdev_mkdir (struct vnode *dir_node, struct vnode **target,
                  const char *component_name);

int  fbdev_open (struct vnode *file_node, struct file **target);
int  fbdev_close(struct file *file);
int  fbdev_read (struct file *file, void *buf, size_t len);
int  fbdev_write(struct file *file, const void *buf, size_t len);
long fbdev_lseek64(struct file *file, long offset, int whence);
int  fbdev_ioctl(struct file *file, unsigned long request, void *arg);
