/*
  +----------------------------------------------------------------------+
  | Swoole                                                               |
  +----------------------------------------------------------------------+
  | This source file is subject to version 2.0 of the Apache license,    |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.apache.org/licenses/LICENSE-2.0.html                      |
  | If you did not receive a copy of the Apache2.0 license and are unable|
  | to obtain it through the world-wide-web, please send a note to       |
  | license@swoole.com so we can mail you a copy immediately.            |
  +----------------------------------------------------------------------+
  | Author: Tianfeng Han  <mikan.tenny@gmail.com>                        |
  +----------------------------------------------------------------------+
*/

#include "php_swoole_async.h"
#include "ext/swoole/include/swoole_memory.h"
#include <sys/mman.h>

typedef struct
{
    size_t size;
    off_t offset;
    char *filename;
    void *memory;
    void *ptr;
} swMmapFile;

static size_t mmap_stream_write(php_stream * stream, const char *buffer, size_t length);
static size_t mmap_stream_read(php_stream *stream, char *buffer, size_t length);
static int mmap_stream_flush(php_stream *stream);
static int mmap_stream_seek(php_stream *stream, zend_off_t offset, int whence, zend_off_t *newoffset);
static int mmap_stream_close(php_stream *stream, int close_handle);
static PHP_METHOD(swoole_mmap, open);

zend_class_entry *swoole_mmap_ce;
static zend_object_handlers swoole_mmap_handlers;

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_mmap_open, 0, 0, 1)
    ZEND_ARG_INFO(0, filename)
    ZEND_ARG_INFO(0, size)
    ZEND_ARG_INFO(0, offset)
ZEND_END_ARG_INFO()

static const zend_function_entry swoole_mmap_methods[] =
{
    PHP_ME(swoole_mmap, open, arginfo_swoole_mmap_open, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_FE_END
};

php_stream_ops mmap_ops =
{
    mmap_stream_write,
    mmap_stream_read,
    mmap_stream_close,
    mmap_stream_flush,
    "swoole_mmap",
    mmap_stream_seek,
    NULL,
    NULL,
    NULL
};

static size_t mmap_stream_write(php_stream * stream, const char *buffer, size_t length)
{
    swMmapFile *res = (swMmapFile *) stream->abstract;

    ssize_t n_write = MIN((char* )res->memory + res->size - (char* )res->ptr, length);
    if (n_write == 0)
    {
        return 0;
    }
    memcpy(res->ptr, buffer, n_write);
    res->ptr += n_write;
    return n_write;
}

static size_t mmap_stream_read(php_stream *stream, char *buffer, size_t length)
{
    swMmapFile *res = (swMmapFile *) stream->abstract;

    ssize_t n_read = MIN((char * )res->memory + res->size - (char * )res->ptr, length);
    if (n_read == 0)
    {
        return 0;
    }
    memcpy(buffer, res->ptr, n_read);
    res->ptr += n_read;
    return n_read;
}

static int mmap_stream_flush(php_stream *stream)
{
    swMmapFile *res = (swMmapFile *) stream->abstract;
    return msync(res->memory, res->size, MS_SYNC | MS_INVALIDATE);
}

static int mmap_stream_seek(php_stream *stream, zend_off_t offset, int whence, zend_off_t *newoffset)
{
    swMmapFile *res = (swMmapFile *) stream->abstract;

    switch (whence)
    {
    case SEEK_SET:
        if (offset < 0 || offset > res->size)
        {
            *newoffset = (off_t) -1;
            return -1;
        }
        res->ptr = res->memory + offset;
        *newoffset = offset;
        return 0;
    case SEEK_CUR:
        if (res->ptr + offset < res->memory || res->ptr + offset > res->memory + res->size)
        {
            *newoffset = (off_t) -1;
            return -1;
        }
        res->ptr += offset;
        *newoffset = (char *) res->ptr - (char *) res->memory;
        return 0;
    case SEEK_END:
        if (offset > 0 || -1 * offset > res->size)
        {
            *newoffset = (off_t) -1;
            return -1;
        }
        res->ptr = res->memory + res->size + offset;
        *newoffset =  (char *) res->ptr - (char *) res->memory;
        return 0;
    default:
        *newoffset = (off_t) -1;
        return -1;
    }
}

static int mmap_stream_close(php_stream *stream, int close_handle)
{
    swMmapFile *res = (swMmapFile *) stream->abstract;
    if (close_handle)
    {
        munmap(res->memory, res->size);
    }
    efree(res);
    return 0;
}

void swoole_mmap_init(int module_number)
{
    SW_INIT_CLASS_ENTRY(swoole_mmap, "Swoole\\Mmap", "swoole_mmap", NULL, swoole_mmap_methods);
    SW_SET_CLASS_SERIALIZABLE(swoole_mmap, zend_class_serialize_deny, zend_class_unserialize_deny);
    SW_SET_CLASS_CLONEABLE(swoole_mmap, sw_zend_class_clone_deny);
    SW_SET_CLASS_UNSET_PROPERTY_HANDLER(swoole_mmap, sw_zend_class_unset_property_deny);
}

static PHP_METHOD(swoole_mmap, open)
{
    char *filename;
    size_t l_filename;
    zend_long size = -1;
    zend_long offset = 0;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s|ll", &filename, &l_filename, &size, &offset) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (l_filename == 0)
    {
        php_swoole_fatal_error(E_WARNING, "file name is required.");
        RETURN_FALSE;
    }

    int fd;
    if ((fd = open(filename, O_RDWR)) < 0)
    {
        php_swoole_sys_error(E_WARNING, "open(%s, O_RDWR) failed.", filename);
        RETURN_FALSE;
    }

    if (size <= 0)
    {
        struct stat _stat;
        if (fstat(fd, &_stat) < 0)
        {
            php_swoole_sys_error(E_WARNING, "fstat(%s) failed.", filename);
            close(fd);
            RETURN_FALSE;
        }
        if (_stat.st_size == 0)
        {
            php_swoole_sys_error(E_WARNING, "file[%s] is empty.", filename);
            close(fd);
            RETURN_FALSE;
        }
        if (offset > 0)
        {
            size = _stat.st_size - offset;
        }
        else
        {
            size = _stat.st_size;
        }
    }

    void *addr = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, offset);
    if (addr == MAP_FAILED)
    {
        php_swoole_sys_error(E_WARNING, "mmap(" ZEND_LONG_FMT ") failed.", size);
        close(fd);
        RETURN_FALSE;
    }

    swMmapFile *res = (swMmapFile *) emalloc(sizeof(swMmapFile));
    res->filename = filename;
    res->size = size;
    res->offset = offset;
    res->memory = addr;
    res->ptr = addr;

    close(fd);
    php_stream *stream = php_stream_alloc(&mmap_ops, res, NULL, "r+");
    php_stream_to_zval(stream, return_value);
}