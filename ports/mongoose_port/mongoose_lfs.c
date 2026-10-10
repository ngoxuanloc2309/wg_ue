/**
 * @file mongoose_lfs.c
 * @brief LittleFS adapter for Mongoose HTTP server
 */
#include "mongoose_lfs.h"
#include "mongoose.h"
#include "block_device.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "MongooseLFS";

/* External LittleFS instance */
extern lfs_t lfs;

/* Track open file handles */
#define MAX_OPEN_FILES 4
static struct {
    lfs_file_t file;
    bool open;
    char path[64];
} s_open_files[MAX_OPEN_FILES];

static int find_free_fd(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!s_open_files[i].open) return i;
    }
    return -1;
}

static int mg_lfs_stat(const char *path, size_t *size, time_t *mtime) {
    struct lfs_info info;
    int err = lfs_stat(&lfs, path, &info);
    if (err < 0) {
        return 0;
    }
    if (size) *size = info.size;
    if (mtime) *mtime = 0;
    
    if (info.type == LFS_TYPE_DIR) {
        return MG_FS_DIR;
    } else {
        return MG_FS_READ | MG_FS_WRITE;
    }
}

static void mg_lfs_list(const char *path, void (*fn)(const char *, void *), void *userdata) {
    lfs_dir_t dir;
    int err = lfs_dir_open(&lfs, &dir, path);
    if (err < 0) return;

    struct lfs_info info;
    while (true) {
        err = lfs_dir_read(&lfs, &dir, &info);
        if (err <= 0) break;
        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) continue;
        fn(info.name, userdata);
    }
    lfs_dir_close(&lfs, &dir);
}

static void *mg_lfs_open(const char *path, int flags) {
    int fd = find_free_fd();
    if (fd < 0) {
        LOGE(TAG, "Too many open files");
        return NULL;
    }

    int lfs_flags = 0;
    if ((flags & MG_FS_READ) && !(flags & MG_FS_WRITE)) {
        lfs_flags = LFS_O_RDONLY;
    } else if (flags & MG_FS_WRITE) {
        lfs_flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND;
    } else {
        lfs_flags = LFS_O_RDWR | LFS_O_CREAT;
    }

    int err = lfs_file_open(&lfs, &s_open_files[fd].file, path, lfs_flags);
    if (err < 0) {
        return NULL;
    }

    s_open_files[fd].open = true;
    strncpy(s_open_files[fd].path, path, sizeof(s_open_files[fd].path) - 1);

    return (void *)(intptr_t)(fd + 1);
}

static void mg_lfs_close(void *fd) {
    if (!fd) return;
    int fdn = (int)(intptr_t)fd - 1;
    if (fdn >= 0 && fdn < MAX_OPEN_FILES && s_open_files[fdn].open) {
        lfs_file_close(&lfs, &s_open_files[fdn].file);
        s_open_files[fdn].open = false;
    }
}

static size_t mg_lfs_read(void *fd, void *buf, size_t len) {
    if (!fd) return 0;
    int fdn = (int)(intptr_t)fd - 1;
    if (fdn < 0 || fdn >= MAX_OPEN_FILES || !s_open_files[fdn].open) return 0;

    lfs_ssize_t ret = lfs_file_read(&lfs, &s_open_files[fdn].file, buf, len);
    return ret > 0 ? (size_t)ret : 0;
}

static size_t mg_lfs_write(void *fd, const void *buf, size_t len) {
    if (!fd) return 0;
    int fdn = (int)(intptr_t)fd - 1;
    if (fdn < 0 || fdn >= MAX_OPEN_FILES || !s_open_files[fdn].open) return 0;

    lfs_ssize_t ret = lfs_file_write(&lfs, &s_open_files[fdn].file, buf, len);
    return ret > 0 ? (size_t)ret : 0;
}

static size_t mg_lfs_seek(void *fd, size_t offset) {
    if (!fd) return 0;
    int fdn = (int)(intptr_t)fd - 1;
    if (fdn < 0 || fdn >= MAX_OPEN_FILES || !s_open_files[fdn].open) return 0;

    lfs_file_seek(&lfs, &s_open_files[fdn].file, offset, LFS_SEEK_SET);
    return offset;
}

static bool mg_lfs_mv(const char *from, const char *to) {
    return lfs_rename(&lfs, from, to) == 0;
}

static bool mg_lfs_rm(const char *path) {
    return lfs_remove(&lfs, path) == 0;
}

static bool mg_lfs_mkd(const char *path) {
    return lfs_mkdir(&lfs, path) == 0;
}

struct mg_fs mg_fs_lfs = {
    mg_lfs_stat,
    mg_lfs_list,
    mg_lfs_open,
    mg_lfs_close,
    mg_lfs_read,
    mg_lfs_write,
    mg_lfs_seek,
    mg_lfs_mv,
    mg_lfs_rm,
    mg_lfs_mkd
};

struct mg_fs* mongoose_lfs_get_fs(void) {
    return &mg_fs_lfs;
}
