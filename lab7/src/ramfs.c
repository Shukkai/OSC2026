#include "ramfs.h"
#include "initrd.h"
#include "mm.h"
#include "string.h"
#include "utils.h"
#include "uart.h"

struct vnode_operations ramfs_vops = {
    .lookup = ramfs_lookup,
    .create = ramfs_create,
    .mkdir  = ramfs_mkdir,
};

struct file_operations ramfs_fops = {
    .open  = ramfs_open,
    .close = ramfs_close,
    .read  = ramfs_read,
    .write = ramfs_write,
};

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/* cpio entries from `find . | cpio` are prefixed with "./" (root = ".").
 * Return the path relative to the cpio root, with that prefix stripped. */
static const char *cpio_relpath(const char *name) {
    if (name[0] == '.' && name[1] == '/') return name + 2;
    if (name[0] == '.' && name[1] == '\0') return name + 1; /* root "." -> "" */
    if (name[0] == '/')                    return name + 1;
    return name;
}

static struct vnode *ramfs_new_vnode(int type, const char *relpath,
                                     const char *data, unsigned long size) {
    struct vnode *v = (struct vnode *)kmalloc(sizeof(struct vnode));
    if (!v) return NULL;

    struct ramfs_inode *n =
        (struct ramfs_inode *)kmalloc(sizeof(struct ramfs_inode));
    if (!n) { kfree(v); return NULL; }

    n->type = type;
    n->data = data;
    n->size = size;
    int i = 0;
    for (; i < RAMFS_MAX_NAME - 1 && relpath[i]; i++) n->name[i] = relpath[i];
    n->name[i] = '\0';

    v->mount    = NULL;
    v->v_ops    = &ramfs_vops;
    v->f_ops    = &ramfs_fops;
    v->internal = n;
    return v;
}

/* ------------------------------------------------------------------ */
/* vnode_operations (read-only)                                         */
/* ------------------------------------------------------------------ */

int ramfs_lookup(struct vnode *dir_node, struct vnode **target,
                 const char *component_name) {
    struct ramfs_inode *dir = (struct ramfs_inode *)dir_node->internal;
    if (dir->type != RAMFS_TYPE_DIR) return -1;

    /* Build the cpio-relative path we are searching for. */
    char want[RAMFS_MAX_NAME];
    int w = 0;
    for (int i = 0; dir->name[i] && w < RAMFS_MAX_NAME - 1; i++)
        want[w++] = dir->name[i];
    if (w > 0 && w < RAMFS_MAX_NAME - 1) want[w++] = '/';
    for (int i = 0; component_name[i] && w < RAMFS_MAX_NAME - 1; i++)
        want[w++] = component_name[i];
    want[w] = '\0';
    unsigned long want_len = (unsigned long)w;

    char *archive = (char *)get_initrd_base();
    if (!archive) return -1;

    char *p = archive;
    while (1) {
        struct cpio_newc_header *h = (struct cpio_newc_header *)p;
        if (strncmp(h->c_magic, CPIO_NEWC_MAGIC, 6) != 0) break;

        unsigned long namesize = parse_hex8(h->c_namesize);
        unsigned long filesize = parse_hex8(h->c_filesize);
        unsigned long mode     = parse_hex8(h->c_mode);
        char *name = p + sizeof(struct cpio_newc_header);

        if (strcmp(name, "TRAILER!!!") == 0) break;

        const char *rel = cpio_relpath(name);
        uintptr_t content = align_up(
            (uintptr_t)p + sizeof(struct cpio_newc_header) + namesize, 4);

        /* Exact match: this is the file/dir being looked up. */
        if (strcmp(rel, want) == 0) {
            int type = ((mode & 0xF000) == 0x4000)
                           ? RAMFS_TYPE_DIR : RAMFS_TYPE_FILE;
            struct vnode *v = ramfs_new_vnode(type, want,
                                              (const char *)content, filesize);
            if (!v) return -1;
            *target = v;
            return 0;
        }

        /* Implicit directory: an entry lives under "want/..." but the
         * directory itself was not packed as its own cpio record. */
        if (strncmp(rel, want, want_len) == 0 && rel[want_len] == '/') {
            struct vnode *v = ramfs_new_vnode(RAMFS_TYPE_DIR, want, NULL, 0);
            if (!v) return -1;
            *target = v;
            return 0;
        }

        uintptr_t next = align_up(content + filesize, 4);
        p = (char *)next;
    }
    return -1;
}

/* read-only filesystem: mutating operations are rejected */
int ramfs_create(struct vnode *dir_node, struct vnode **target,
                 const char *component_name) {
    (void)dir_node; (void)target; (void)component_name;
    return -1;
}

int ramfs_mkdir(struct vnode *dir_node, struct vnode **target,
                const char *component_name) {
    (void)dir_node; (void)target; (void)component_name;
    return -1;
}

/* ------------------------------------------------------------------ */
/* file_operations                                                      */
/* ------------------------------------------------------------------ */

int ramfs_open(struct vnode *file_node, struct file **target) {
    (*target)->vnode = file_node;
    (*target)->f_ops = file_node->f_ops;
    (*target)->f_pos = 0;
    return 0;
}

int ramfs_close(struct file *file) {
    kfree(file);
    return 0;
}

int ramfs_read(struct file *file, void *buf, size_t len) {
    struct ramfs_inode *inode = (struct ramfs_inode *)file->vnode->internal;
    if (inode->type != RAMFS_TYPE_FILE) return -1;

    size_t avail = inode->size - file->f_pos;
    if (len > avail) len = avail;
    if (len == 0)    return 0;

    memcpy(buf, inode->data + file->f_pos, len);
    file->f_pos += len;
    return (int)len;
}

int ramfs_write(struct file *file, const void *buf, size_t len) {
    (void)file; (void)buf; (void)len;
    return -1; /* read-only */
}

/* ------------------------------------------------------------------ */
/* setup_mount / registration                                           */
/* ------------------------------------------------------------------ */

int ramfs_setup_mount(struct filesystem *fs, struct mount *mnt) {
    struct vnode *root = ramfs_new_vnode(RAMFS_TYPE_DIR, "", NULL, 0);
    if (!root) return -1;
    mnt->root = root;
    mnt->fs   = fs;
    return 0;
}

int ramfs_register(void) {
    struct filesystem fs;
    fs.name        = "ramfs";
    fs.setup_mount = ramfs_setup_mount;
    return register_filesystem(&fs);
}
