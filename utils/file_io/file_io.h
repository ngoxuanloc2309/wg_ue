/**
 * @file file_io.h
 * @brief LittleFS wrapper for basic file operations
 */
#ifndef __FILE_IO_H__
#define __FILE_IO_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read file contents into a buffer
 * @param path: Path to the file
 * @param buffer: Buffer to store the read data
 * @param max_len: Maximum length of the buffer
 * @return Number of bytes read on success, negative error code on failure
 */
int file_io_read(const char *path, void *buffer, size_t max_len);

/**
 * @brief Write data to a file (creates if not exists, overwrites if exists)
 * @param path: Path to the file
 * @param data: Data to write
 * @param len: Length of data to write
 * @return Number of bytes written on success, negative error code on failure
 */
int file_io_write(const char *path, const void *data, size_t len);

/**
 * @brief Check if a file exists
 * @param path: Path to the file
 * @return true if file exists, false otherwise
 */
bool file_io_exists(const char *path);

/**
 * @brief Get size of a file
 * @param path: Path to the file
 * @return Size in bytes, or negative error code on failure
 */
int file_io_size(const char *path);

/**
 * @brief Create a directory
 * @param path: Path to the directory
 * @return 0 on success (or if already exists), negative error code on failure
 */
int file_io_mkdir(const char *path);

/**
 * @brief Rename or move a file
 * @param oldpath: Old path
 * @param newpath: New path
 * @return 0 on success, negative error code on failure
 */
int file_io_rename(const char *oldpath, const char *newpath);

/**
 * @brief Append data to a file (creates it if it does not exist)
 * @param path: Path to the file
 * @param data: Data to append
 * @param len: Length of data to append
 * @return Number of bytes appended on success, negative error code on failure
 */
int file_io_append(const char *path, const void *data, size_t len);
 
/**
 * @brief Remove a file or an empty directory
 * @param path: Path to remove
 * @return 0 on success (also when it does not exist), negative error code on failure
 */
int file_io_remove(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* __FILE_IO_H__ */
