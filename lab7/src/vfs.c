#include "vfs.h"
#include "mm.h"
#include "string.h"
#include "tmpfs.h"
#include "uart.h"
#include "sched.h"
#include "ramfs.h"
#include "uartfs.h"
#include "fbdev.h"
struct mount          *rootfs;
struct filesystem      fs_list[MAX_FS];
struct file_operations dev_list[MAX_DEV];

void vfs_init() {
    rootfs = (struct mount *)kmalloc(sizeof(struct mount));
    int idx = tmpfs_register();
    fs_list[idx].setup_mount(&fs_list[idx], rootfs);
    ramfs_register();
    uartfs_register();
    fbdev_register();
    vfs_mkdir("/ramfs");
    vfs_mount("/ramfs", "ramfs");
    vfs_mkdir("/dev");
    vfs_mkdir("/dev/uart");
    vfs_mount("/dev/uart", "uartfs");
    vfs_mkdir("/dev/fb");
    vfs_mount("/dev/fb", "fbdev");
    uart_puts("vfs inited\n");
}

int register_filesystem(struct filesystem *fs) {
    for (int i = 0; i < MAX_FS; i++) {
        if (!fs_list[i].name) {
            fs_list[i].name        = fs->name;
            fs_list[i].setup_mount = fs->setup_mount;
            return i;
        }
    }
    return -1;
}

int register_device(struct file_operations *f_ops) {
    for (int i = 0; i < MAX_DEV; i++) {
        if (!dev_list[i].open) {
            dev_list[i] = *f_ops;
            return i;
        }
    }
    return -1;
}

int vfs_open(const char *pathname, int flags, struct file **target) {
    struct vnode *vnode;

    if (vfs_lookup(pathname, &vnode) != 0) {
        if (!(flags & O_CREAT))
            return -1;

        int pos = 0;
        for (int i = 0; i < (int)strlen(pathname); i++)
            if (pathname[i] == '/')
                pos = i;

        char dirname[PATH_MAX] = {0};
        strncpy(dirname, pathname, pos);
        const char *filename = pathname + pos + 1;

        if (vfs_lookup(dirname, &vnode) != 0) {
            if (vfs_mkdir(dirname) != 0)
                return -1;
            if (vfs_lookup(dirname, &vnode) != 0)
                return -1;
        }

        if (vnode->v_ops->create(vnode, &vnode, filename) != 0)
            return -1;
    }

    (*target) = kmalloc(sizeof(struct file));
    (*target)->flags = flags;
    vnode->f_ops->open(vnode, target);
    return 0;
}

int vfs_close(struct file *file) {
    return file->f_ops->close(file);
}

int vfs_read(struct file *file, void *buf, size_t len) {
    return file->f_ops->read(file, buf, len);
}

int vfs_write(struct file *file, const void *buf, size_t len) {
    return file->f_ops->write(file, buf, len);
}

int vfs_mkdir(const char *pathname) {
    if (strlen(pathname) == 0 ||
        (pathname[0] == '/' && pathname[1] == '\0')) {
        return -1;
    }

    struct vnode *node = rootfs->root;
    struct vnode *next;
    char component[PATH_MAX] = {0};
    int idx = 0;

    for (int i = 1; i <= (int)strlen(pathname); i++) {
        if (pathname[i] == '/' || pathname[i] == '\0') {
            component[idx] = '\0';
            if (idx == 0)
                continue;

            if (node->v_ops->lookup(node, &next, component) != 0) {
                if (node->v_ops->mkdir(node, &next, component) != 0)
                    return -1;
            }

            while (next->mount)
                next = next->mount->root;

            node = next;
            idx = 0;
        } else {
            component[idx++] = pathname[i];
        }
    }

    return 0;
}

int vfs_mount(const char *target, const char *filesystem) {
    struct vnode *dir_node;
    struct filesystem *fs = NULL;

    for (int i = 0; i < MAX_FS; i++) {
        if (fs_list[i].name && strcmp(fs_list[i].name, filesystem) == 0) {
            fs = &fs_list[i];
            break;
        }
    }

    if (!fs)
        return -1;

    if (vfs_lookup(target, &dir_node) != 0) {
        if (vfs_mkdir(target) != 0)
            return -1;
        if (vfs_lookup(target, &dir_node) != 0)
            return -1;
    }

    dir_node->mount = kmalloc(sizeof(struct mount));
    fs->setup_mount(fs, dir_node->mount);
    return 0;
}

int vfs_lookup(const char *pathname, struct vnode **target) {
    if (strlen(pathname) == 0 ||
        (pathname[0] == '/' && pathname[1] == '\0')) {
        *target = rootfs->root;
        return 0;
    }

    struct vnode *node = rootfs->root;
    char component[PATH_MAX] = {0};
    int idx = 0;

    for (int i = 1; i < (int)strlen(pathname); i++) {
        if (pathname[i] == '/') {
            component[idx] = '\0';
            if (node->v_ops->lookup(node, &node, component) != 0)
                return -1;
            while (node->mount)
                node = node->mount->root;
            idx = 0;
        } else {
            component[idx++] = pathname[i];
        }
    }
    component[idx] = '\0';

    if (node->v_ops->lookup(node, &node, component) != 0)
        return -1;

    while (node->mount)
        node = node->mount->root;

    *target = node;
    return 0;
}

int vfs_mknod(char *pathname, int id) {
    struct file *file;
    vfs_open(pathname, O_CREAT, &file);
    file->vnode->f_ops = &dev_list[id];
    vfs_close(file);
    return 0;
}

int vfs_setup_stdio(struct file **fd_table) {
    for (int i = 0; i < 3; i++) {
        if (fd_table[i])
            continue;
        if (vfs_open("/dev/uart", 0, &fd_table[i]) != 0)
            return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* path_normalize: convert any path to a clean absolute path           */
/*   - absolute paths start from "/"                                   */
/*   - relative paths start from current->cwd                         */
/*   - resolves ".", "..", and consecutive slashes                     */
/* ------------------------------------------------------------------ */
void path_normalize(const char *path, char *resolved) {
    char tmp[PATH_MAX * 2];
    int  len = 0;

    /* Build working buffer: prepend cwd for relative paths */
    if (path[0] != '/') {
        const char *cwd = current->cwd;
        while (cwd[len]) { tmp[len] = cwd[len]; len++; }
        if (len == 0 || tmp[len - 1] != '/')
            tmp[len++] = '/';
        int i = 0;
        while (path[i]) tmp[len++] = path[i++];
    } else {
        tmp[len++] = '/';
        int i = 1;
        while (path[i]) tmp[len++] = path[i++];
    }
    tmp[len] = '\0';

    /* Process components into resolved */
    resolved[0] = '/';
    int out = 1;

    int i = 1; /* skip leading '/' */
    while (tmp[i]) {
        /* Extract one component */
        char comp[PATH_MAX];
        int  clen = 0;
        while (tmp[i] && tmp[i] != '/') comp[clen++] = tmp[i++];
        comp[clen] = '\0';
        if (tmp[i] == '/') i++;

        if (clen == 0 || (clen == 1 && comp[0] == '.')) {
            /* "." or empty (consecutive slashes) → skip */
        } else if (clen == 2 && comp[0] == '.' && comp[1] == '.') {
            /* ".." → strip last component */
            if (out > 1) {
                int j = out - 1;
                while (j > 0 && resolved[j] != '/') j--;
                out = (j == 0) ? 1 : j;
            }
        } else {
            /* Normal component */
            if (out > 1) resolved[out++] = '/';
            for (int j = 0; j < clen; j++) resolved[out++] = comp[j];
        }
    }
    resolved[out] = '\0';
}
