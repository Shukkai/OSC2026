#pragma once

#include <stddef.h>
#include "vfs.h"

#define TMPFS_MAX_NAME     16     /* component name: 15 chars + NUL */
#define TMPFS_MAX_ENTRIES  16     /* max directory entries          */
#define TMPFS_MAX_FILE     4096   /* max regular file size          */

enum tmpfs_type { TMPFS_TYPE_DIR, TMPFS_TYPE_FILE };

struct tmpfs_entry {
    char          name[TMPFS_MAX_NAME];
    struct vnode *vnode;
};

struct tmpfs_inode {
    int type;
    union {
        struct {
            int                count;
            struct tmpfs_entry entries[TMPFS_MAX_ENTRIES];
        } dir;
        struct {
            size_t size;
            char   data[TMPFS_MAX_FILE];
        } file;
    };
};

extern struct vnode_operations tmpfs_vops;
extern struct file_operations  tmpfs_fops;

int  tmpfs_register(void);
int  tmpfs_setup_mount(struct filesystem *fs, struct mount *mnt);

int  tmpfs_lookup(struct vnode *dir_node, struct vnode **target,
                  const char *component_name);
int  tmpfs_create(struct vnode *dir_node, struct vnode **target,
                  const char *component_name);
int  tmpfs_mkdir (struct vnode *dir_node, struct vnode **target,
                  const char *component_name);

int  tmpfs_open (struct vnode *file_node, struct file **target);
int  tmpfs_close(struct file *file);
int  tmpfs_read (struct file *file, void *buf, size_t len);
int  tmpfs_write(struct file *file, const void *buf, size_t len);
