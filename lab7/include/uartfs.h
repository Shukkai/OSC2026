#pragma once

#include <stddef.h>
#include "vfs.h"

extern struct vnode_operations uartfs_vops;
extern struct file_operations  uartfs_fops;

int uartfs_register(void);
int uartfs_setup_mount(struct filesystem *fs, struct mount *mnt);

int uartfs_lookup(struct vnode *dir_node, struct vnode **target,
                  const char *component_name);
int uartfs_create(struct vnode *dir_node, struct vnode **target,
                  const char *component_name);
int uartfs_mkdir(struct vnode *dir_node, struct vnode **target,
                 const char *component_name);

int uartfs_open(struct vnode *file_node, struct file **target);
int uartfs_close(struct file *file);
int uartfs_read(struct file *file, void *buf, size_t len);
int uartfs_write(struct file *file, const void *buf, size_t len);
