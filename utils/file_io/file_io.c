/**
 * @file file_io.c
 * @brief LittleFS wrapper for basic file operations
 */
#include "file_io.h"
#include "lfs.h"
#include "logger.h"

static const char *TAG = "file_io";

/* External LittleFS instance from block_device.c */
extern lfs_t lfs;

int file_io_read(const char *path, void *buffer, size_t max_len) {
    if (path == NULL || buffer == NULL || max_len == 0) {
        return LFS_ERR_INVAL;
    }

    lfs_file_t file;
    int err = lfs_file_open(&lfs, &file, path, LFS_O_RDONLY);
    if (err < 0) {
        /* Don't log error for normal missing files, just return */
        return err;
    }

    lfs_ssize_t read_size = lfs_file_read(&lfs, &file, buffer, max_len);
    if (read_size < 0) {
        LOGE(TAG, "Failed to read file %s: %d", path, read_size);
    }

    /* Close the file */
    int close_err = lfs_file_close(&lfs, &file);
    if (close_err < 0) {
        LOGE(TAG, "Failed to close file %s: %d", path, close_err);
    }

    return (read_size >= 0) ? (int)read_size : (int)read_size;
}

int file_io_write(const char *path, const void *data, size_t len) {
    if (path == NULL || data == NULL || len == 0) {
        return LFS_ERR_INVAL;
    }

    lfs_file_t file;
    /* Open file for writing, create if not exist, truncate to 0 */
    int err = lfs_file_open(&lfs, &file, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err < 0) {
        LOGE(TAG, "Failed to open file %s for writing: %d", path, err);
        return err;
    }

    lfs_ssize_t write_size = lfs_file_write(&lfs, &file, data, len);
    if (write_size < 0 || (size_t)write_size != len) {
        LOGE(TAG, "Failed to write file %s: %d", path, write_size);
    }

    /* Close the file */
    int close_err = lfs_file_close(&lfs, &file);
    if (close_err < 0) {
        LOGE(TAG, "Failed to close file %s: %d", path, close_err);
        return close_err;
    }

    return (write_size >= 0) ? (int)write_size : (int)write_size;
}

bool file_io_exists(const char *path) {
    if (path == NULL) return false;

    struct lfs_info info;
    int err = lfs_stat(&lfs, path, &info);
    
    return (err == 0);
}

int file_io_size(const char *path) {
    if (path == NULL) return LFS_ERR_INVAL;

    struct lfs_info info;
    int err = lfs_stat(&lfs, path, &info);
    
    if (err < 0) return err;
    return info.size;
}

int file_io_mkdir(const char *path) {
    if (path == NULL) return LFS_ERR_INVAL;

    int err = lfs_mkdir(&lfs, path);
    if (err == LFS_ERR_EXIST) {
        /* Already exists, consider it a success */
        return 0;
    } else if (err < 0) {
        LOGE(TAG, "Failed to create directory %s: %d", path, err);
        return err;
    }

    return 0;
}

int file_io_rename(const char *oldpath, const char *newpath) {
    if (oldpath == NULL || newpath == NULL) return LFS_ERR_INVAL;

    int err = lfs_rename(&lfs, oldpath, newpath);
    if (err < 0) {
        LOGE(TAG, "Failed to rename %s to %s: %d", oldpath, newpath, err);
        return err;
    }
    return 0;
}

int file_io_append(const char *path, const void *data, size_t len) {
    if (path == NULL || data == NULL || len == 0) {
        return LFS_ERR_INVAL;
    }
 
    lfs_file_t file;
    /* Open for writing, create if missing, keep the existing bytes and write at the end */
    int err = lfs_file_open(&lfs, &file, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND);
    if (err < 0) {
        LOGE(TAG, "Failed to open file %s for append: %d", path, err);
        return err;
    }
 
    lfs_ssize_t write_size = lfs_file_write(&lfs, &file, data, len);
    if (write_size < 0 || (size_t)write_size != len) {
        LOGE(TAG, "Failed to append to file %s: %d", path, write_size);
    }
 
    /* Close flushes the data: a failure here means the bytes are not stored */
    int close_err = lfs_file_close(&lfs, &file);
    if (close_err < 0) {
        LOGE(TAG, "Failed to close file %s: %d", path, close_err);
        return close_err;
    }
 
    if (write_size < 0) return (int)write_size;
    if ((size_t)write_size != len) return LFS_ERR_IO;
    return (int)write_size;
}
 
int file_io_remove(const char *path) {
    if (path == NULL) return LFS_ERR_INVAL;
 
    int err = lfs_remove(&lfs, path);
    if (err == LFS_ERR_NOENT) {
        /* Nothing to remove, consider it a success */
        return 0;
    } else if (err < 0) {
        LOGE(TAG, "Failed to remove %s: %d", path, err);
        return err;
    }
    return 0;
}
