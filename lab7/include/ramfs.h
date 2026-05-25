#pragma once

#include <stddef.h>
#include "vfs.h"

#define RAMFS_MAX_NAME 256

enum ramfs_type { RAMFS_TYPE_DIR, RAMFS_TYPE_FILE };

/* A ramfs node is a thin, read-only view over the initramfs cpio image.
 * For files, `data`/`size` point directly into the cpio archive (no copy). */
struct ramfs_inode {
    int           type;
    char          name[RAMFS_MAX_NAME]; /* path relative to cpio root, "" = root */
    const char   *data;                 /* file content inside the cpio image     */
    unsigned long size;
};

extern struct vnode_operations ramfs_vops;
extern struct file_operations  ramfs_fops;

int ramfs_register(void);
int ramfs_setup_mount(struct filesystem *fs, struct mount *mnt);

int ramfs_lookup(struct vnode *dir_node, struct vnode **target,
                 const char *component_name);
int ramfs_create(struct vnode *dir_node, struct vnode **target,
                 const char *component_name);
int ramfs_mkdir (struct vnode *dir_node, struct vnode **target,
                 const char *component_name);

int ramfs_open (struct vnode *file_node, struct file **target);
int ramfs_close(struct file *file);
int ramfs_read (struct file *file, void *buf, size_t len);
int ramfs_write(struct file *file, const void *buf, size_t len);
