#include "tmpfs.h"
#include "mm.h"
#include "string.h"
#include "vfs.h"
#include "uart.h"

struct vnode_operations tmpfs_vops = {
    .lookup = tmpfs_lookup,
    .create = tmpfs_create,
    .mkdir  = tmpfs_mkdir,
};

struct file_operations tmpfs_fops = {
    .open  = tmpfs_open,
    .close = tmpfs_close,
    .read  = tmpfs_read,
    .write = tmpfs_write,
};

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static struct vnode *tmpfs_new_vnode(void) {
    struct vnode *v = (struct vnode *)kmalloc(sizeof(struct vnode));
    if (!v) return NULL;
    v->mount    = NULL;
    v->v_ops    = &tmpfs_vops;
    v->f_ops    = &tmpfs_fops;
    v->internal = NULL;
    return v;
}

static struct tmpfs_inode *tmpfs_new_inode(int type) {
    struct tmpfs_inode *n =
        (struct tmpfs_inode *)kmalloc(sizeof(struct tmpfs_inode));
    if (!n) return NULL;
    memset(n, 0, sizeof(struct tmpfs_inode));
    n->type = type;
    return n;
}

/* Copy at most TMPFS_MAX_NAME-1 chars, always NUL-terminated. */
static void copy_name(char *dst, const char *src) {
    int i;
    for (i = 0; i < TMPFS_MAX_NAME - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* ------------------------------------------------------------------ */
/* vnode_operations                                                     */
/* ------------------------------------------------------------------ */

int tmpfs_lookup(struct vnode *dir_node, struct vnode **target,
                        const char *component_name) {
    struct tmpfs_inode *inode = (struct tmpfs_inode *)dir_node->internal;
    if (inode->type != TMPFS_TYPE_DIR) return -1;

    for (int i = 0; i < inode->dir.count; i++) {
        if (strcmp(inode->dir.entries[i].name, component_name) == 0) {
            *target = inode->dir.entries[i].vnode;
            return 0;
        }
    }
    return -1;
}

static int tmpfs_add_child(struct vnode *dir_node, struct vnode **target,
                           const char *name, int type) {
    struct tmpfs_inode *parent = (struct tmpfs_inode *)dir_node->internal;
    if (parent->type != TMPFS_TYPE_DIR)          return -1;
    if (parent->dir.count >= TMPFS_MAX_ENTRIES)  return -1;

    struct vnode *new_v = tmpfs_new_vnode();
    if (!new_v) return -1;

    struct tmpfs_inode *new_i = tmpfs_new_inode(type);
    if (!new_i) { kfree(new_v); return -1; }

    new_v->internal = new_i;

    int idx = parent->dir.count++;
    copy_name(parent->dir.entries[idx].name, name);
    parent->dir.entries[idx].vnode = new_v;

    *target = new_v;
    return 0;
}

int tmpfs_create(struct vnode *dir_node, struct vnode **target,
                        const char *component_name) {
    return tmpfs_add_child(dir_node, target, component_name, TMPFS_TYPE_FILE);
}

int tmpfs_mkdir(struct vnode *dir_node, struct vnode **target,
                       const char *component_name) {
    return tmpfs_add_child(dir_node, target, component_name, TMPFS_TYPE_DIR);
}

/* ------------------------------------------------------------------ */
/* file_operations                                                      */
/* ------------------------------------------------------------------ */

int tmpfs_open(struct vnode *file_node, struct file **target) {
    (*target)->vnode = file_node;
    (*target)->f_ops = file_node->f_ops;
    (*target)->f_pos = 0;
    return 0;
}

int tmpfs_close(struct file *file) {
    kfree(file);
    return 0;
}

int tmpfs_read(struct file *file, void *buf, size_t len) {
    struct tmpfs_inode *inode = (struct tmpfs_inode *)file->vnode->internal;
    if (inode->type != TMPFS_TYPE_FILE) return -1;

    size_t avail = inode->file.size - file->f_pos;
    if (len > avail) len = avail;
    if (len == 0)    return 0;

    memcpy(buf, inode->file.data + file->f_pos, len);
    file->f_pos += len;
    return (int)len;
}

int tmpfs_write(struct file *file, const void *buf, size_t len) {
    struct tmpfs_inode *inode = (struct tmpfs_inode *)file->vnode->internal;
    if (inode->type != TMPFS_TYPE_FILE) return -1;

    size_t remaining = TMPFS_MAX_FILE - file->f_pos;
    if (len > remaining) len = remaining;
    if (len == 0)        return 0;

    memcpy(inode->file.data + file->f_pos, buf, len);
    file->f_pos += len;
    if (file->f_pos > inode->file.size)
        inode->file.size = file->f_pos;
    return (int)len;
}

/* ------------------------------------------------------------------ */
/* setup_mount                                                          */
/* ------------------------------------------------------------------ */

int tmpfs_setup_mount(struct filesystem *fs, struct mount *mnt) {
    struct vnode *root = tmpfs_new_vnode();
    if (!root) return -1;

    struct tmpfs_inode *inode = tmpfs_new_inode(TMPFS_TYPE_DIR);
    if (!inode) { kfree(root); return -1; }

    root->internal = inode;
    mnt->root      = root;
    mnt->fs        = fs;
    return 0;
}

/* ------------------------------------------------------------------ */
/* tmpfs_register: register the filesystem, return its fs_list index    */
/* (vfs_init() allocates rootfs and calls setup_mount itself)           */
/* ------------------------------------------------------------------ */

int tmpfs_register(void) {
    struct filesystem fs;
    fs.name        = "tmpfs";
    fs.setup_mount = tmpfs_setup_mount;
    return register_filesystem(&fs);
}
