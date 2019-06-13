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
#include "ext/swoole/php_swoole_cxx.h"
#include "php_streams.h"
#include "php_network.h"

#include "ext/standard/file.h"

#include <string>
#include <unordered_map>

typedef struct
{
    zval _callback;
    zval _filename;
    zval *callback;
    zval *filename;
    uint32_t *refcount;
    off_t offset;
    uint16_t type;
    uint8_t once;
    char *content;
    uint32_t length;
} file_request;

typedef struct
{
    zval _callback;
    zval _domain;
    zval *callback;
    zval *domain;
    uint8_t useless;
    swTimer_node *timer;
} dns_request;

typedef struct
{
    zval *callback;
    pid_t pid;
    int fd;
    swString *buffer;
} process_stream;

static void aio_onFileCompleted(swAio_event *event);
static void aio_onDNSCompleted(swAio_event *event);
static void php_swoole_dns_callback(char *domain, swDNSResolver_result *result, void *data);

static void php_swoole_file_request_free(void *data);

PHP_FUNCTION(swoole_async_read);
PHP_FUNCTION(swoole_async_write);
PHP_FUNCTION(swoole_async_readfile);
PHP_FUNCTION(swoole_async_writefile);
PHP_FUNCTION(swoole_async_dns_lookup);
PHP_METHOD(swoole_async, exec);

typedef struct
{
    int fd;
    uint32_t refcount;
} open_file;

static std::unordered_map<std::string, open_file> open_write_files;

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_set, 0, 0, 1)
    ZEND_ARG_ARRAY_INFO(0, settings, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_readfile, 0, 0, 2)
    ZEND_ARG_INFO(0, filename)
    ZEND_ARG_INFO(0, callback)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_writefile, 0, 0, 2)
    ZEND_ARG_INFO(0, filename)
    ZEND_ARG_INFO(0, content)
    ZEND_ARG_INFO(0, callback)
    ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_read, 0, 0, 2)
    ZEND_ARG_INFO(0, filename)
    ZEND_ARG_INFO(0, callback)
    ZEND_ARG_INFO(0, chunk_size)
    ZEND_ARG_INFO(0, offset)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_write, 0, 0, 2)
    ZEND_ARG_INFO(0, filename)
    ZEND_ARG_INFO(0, content)
    ZEND_ARG_INFO(0, offset)
    ZEND_ARG_INFO(0, callback)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_dns_lookup, 0, 0, 2)
    ZEND_ARG_INFO(0, hostname)
    ZEND_ARG_INFO(0, callback)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_exec, 0, 0, 2)
    ZEND_ARG_INFO(0, command)
    ZEND_ARG_INFO(0, callback)
ZEND_END_ARG_INFO()

const zend_function_entry swoole_async_functions[] =
{
    PHP_FE(swoole_async_read, arginfo_swoole_async_read)
    PHP_FE(swoole_async_write, arginfo_swoole_async_write)
    PHP_FE(swoole_async_readfile, arginfo_swoole_async_readfile)
    PHP_FE(swoole_async_writefile, arginfo_swoole_async_writefile)
    PHP_FE(swoole_async_dns_lookup, arginfo_swoole_async_dns_lookup)
    PHP_FE_END /* Must be the last line in swoole_async_functions[] */
};

static const zend_function_entry swoole_async_methods[] =
{
    ZEND_FENTRY(read, ZEND_FN(swoole_async_read), arginfo_swoole_async_read, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    ZEND_FENTRY(write, ZEND_FN(swoole_async_write), arginfo_swoole_async_write, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    ZEND_FENTRY(readFile, ZEND_FN(swoole_async_readfile), arginfo_swoole_async_readfile, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    ZEND_FENTRY(writeFile, ZEND_FN(swoole_async_writefile), arginfo_swoole_async_writefile, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    ZEND_FENTRY(dnsLookup, ZEND_FN(swoole_async_dns_lookup), arginfo_swoole_async_dns_lookup, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    ZEND_FENTRY(set, ZEND_FN(swoole_async_set), arginfo_swoole_async_set, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(swoole_async, exec, arginfo_swoole_async_exec, ZEND_ACC_PUBLIC| ZEND_ACC_STATIC)
    PHP_FE_END
};

static zend_class_entry *swoole_async_ce;
static zend_object_handlers swoole_async_handlers;

/* {{{ swoole_async_deps
 */
static const zend_module_dep swoole_async_deps[] = {
    ZEND_MOD_REQUIRED("swoole")
    ZEND_MOD_END
};
/* }}} */

zend_module_entry swoole_async_module_entry =
{
    STANDARD_MODULE_HEADER_EX, NULL,
    swoole_async_deps,
    "swoole_async",
    swoole_async_functions,
    PHP_MINIT(swoole_async),
    PHP_MSHUTDOWN(swoole_async),
    PHP_RINIT(swoole_async),
    PHP_RSHUTDOWN(swoole_async),
    PHP_MINFO(swoole_async),
    PHP_SWOOLE_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_SWOOLE_ASYNC
ZEND_GET_MODULE(swoole_async)
#endif

static void php_swoole_file_request_free(void *data)
{
    file_request *file_req = (file_request *) data;
    if (file_req->callback)
    {
        zval_ptr_dtor(file_req->callback);
    }
    efree(file_req->content);
    zval_ptr_dtor(file_req->filename);
    efree(file_req);
}

void swoole_async_init(int module_number)
{
    SW_INIT_CLASS_ENTRY(swoole_async, "Swoole\\Async", "swoole_async", NULL, swoole_async_methods);
    SW_SET_CLASS_SERIALIZABLE(swoole_async, zend_class_serialize_deny, zend_class_unserialize_deny);
    SW_SET_CLASS_CLONEABLE(swoole_async, zend_class_clone_deny);
    SW_SET_CLASS_UNSET_PROPERTY_HANDLER(swoole_async, sw_zend_class_unset_property_deny);
}

static void php_swoole_dns_callback(char *domain, swDNSResolver_result *result, void *data)
{
    dns_request *req = (dns_request *) data;
    zval *retval = NULL;
    zval args[2];
    char *address;

    /**
     * args[0]: host domain name
     */
    args[0] = *req->domain;
    /**
     * args[1]: IP address
     */
    if (result->num > 0)
    {
        if (SwooleG.dns_lookup_random)
        {
            address = result->hosts[rand() % result->num].address;
        }
        else
        {
            address = result->hosts[0].address;
        }
        ZVAL_STRING(&args[1], address);
    }
    else
    {
        ZVAL_STRING(&args[1], "");
    }

    zval *zcallback = req->callback;
    if (sw_call_user_function_ex(EG(function_table), NULL, zcallback, &retval, 2, args, 0, NULL) == FAILURE)
    {
        php_swoole_fatal_error(E_WARNING, "swoole_asyns_dns_lookup handler error.");
        return;
    }
    if (UNEXPECTED(EG(exception)))
    {
        zend_exception_error(EG(exception), E_ERROR);
    }

    zval_ptr_dtor(req->callback);
    zval_ptr_dtor(req->domain);
    efree(req);
    if (retval)
    {
        zval_ptr_dtor(retval);
    }
    zval_ptr_dtor(&args[1]);
}

static void aio_onDNSCompleted(swAio_event *event)
{
    int64_t ret;

    dns_request *dns_req = NULL;
    zval *retval = NULL, *zcallback = NULL;
    zval args[2];
    zval _zcontent, *zcontent = &_zcontent;

    dns_req = (dns_request *) event->req;
    zcallback = dns_req->callback;
    ZVAL_NULL(zcontent);

    ret = event->ret;
    if (ret < 0)
    {
        SwooleG.error = event->error;
        php_swoole_error(E_WARNING, "Aio Error: %s[%d]", strerror(event->error), event->error);
    }

    args[0] = *dns_req->domain;
    if (ret < 0)
    {
        ZVAL_STRING(zcontent, "");
    }
    else
    {
        ZVAL_STRING(zcontent, (char *) event->buf);
    }
    args[1] = *zcontent;

    if (sw_call_user_function_ex(EG(function_table), NULL, zcallback, &retval, 2, args, 0, NULL) == FAILURE)
    {
        php_swoole_fatal_error(E_WARNING, "swoole_async: onAsyncComplete handler error");
        return;
    }
    if (UNEXPECTED(EG(exception)))
    {
        zend_exception_error(EG(exception), E_ERROR);
    }

    zval_ptr_dtor(dns_req->callback);
    zval_ptr_dtor(dns_req->domain);
    efree(dns_req);
    efree(event->buf);

    if (!ZVAL_IS_NULL(zcontent))
    {
        zval_ptr_dtor(zcontent);
    }
    if (retval)
    {
        zval_ptr_dtor(retval);
    }
}

static void aio_onFileCompleted(swAio_event *event)
{
    int isEOF = SW_FALSE;
    int64_t ret = event->ret;
    file_request *file_req = (file_request *) event->object;

    zval *retval = NULL, *zcallback = NULL;
    zval args[2];
    zval _zcontent, *zcontent = &_zcontent;
    zval _zwriten, *zwriten = &_zwriten;

    zcallback = file_req->callback;
    ZVAL_NULL(zcontent);
    ZVAL_NULL(zwriten);

    if (ret < 0)
    {
        SwooleG.error = event->error;
        php_swoole_error(E_WARNING, "Aio Error: %s[%d]", strerror(event->error), event->error);
    }
    else
    {
        if (ret == 0)
        {
            bzero(event->buf, event->nbytes);
            isEOF = SW_TRUE;
        }
        else if (file_req->once == 1 && ret < file_req->length)
        {
            php_swoole_fatal_error(E_WARNING, "ret_length[%d] < req->length[%d].", (int ) ret, file_req->length);
        }
        else if (event->type == SW_AIO_READ)
        {
            file_req->offset += event->ret;
        }
    }

    if (event->type == SW_AIO_READ)
    {
        if (ret < 0)
        {
            ZVAL_STRING(zcontent, "");
        }
        else
        {
            ZVAL_STRINGL(zcontent, (char* )event->buf, ret);
        }
        args[0] = *file_req->filename;
        args[1] = *zcontent;
    }
    else if (event->type == SW_AIO_WRITE)
    {
        ZVAL_LONG(zwriten, ret);
        args[0] = *file_req->filename;
        args[1] = *zwriten;
    }
    else
    {
        php_swoole_fatal_error(E_WARNING, "swoole_async: onFileCompleted unknown event type[%d].", event->type);
        return;
    }

    if (zcallback)
    {
        if (sw_call_user_function_ex(EG(function_table), NULL, zcallback, &retval, 2, args, 0, NULL) == FAILURE)
        {
            php_swoole_fatal_error(E_WARNING, "swoole_async: onAsyncComplete handler error");
            return;
        }
        if (UNEXPECTED(EG(exception)))
        {
            zend_exception_error(EG(exception), E_ERROR);
        }
    }

    //file io
    if (file_req->once == 1)
    {
        close_file:
        if (file_req->refcount)
        {
            if (--(*file_req->refcount) == 0)
            {
                swTraceLog(SW_TRACE_AIO, "close file fd#%d", event->fd);
                open_write_files.erase(std::string(Z_STRVAL_P(file_req->filename), Z_STRLEN_P(file_req->filename)));
                close(event->fd);
            }
            else
            {
                swTraceLog(SW_TRACE_AIO, "delref file fd#%d, refcount=%u", event->fd, *file_req->refcount);
            }
        }
        else
        {
            close(event->fd);
        }
        php_swoole_file_request_free(file_req);
    }
    else if(file_req->type == SW_AIO_WRITE)
    {
        if (retval && !ZVAL_IS_NULL(retval) && !Z_BVAL_P(retval))
        {
            goto close_file;
        }
        else
        {
            php_swoole_file_request_free(file_req);
        }
    }
    else // if(file_req->type == SW_AIO_READ)
    {
        if ((retval && !ZVAL_IS_NULL(retval) && !Z_BVAL_P(retval)) || isEOF)
        {
            goto close_file;
        }
        //Less than expected, at the end of the file
        else if (event->ret < (int) event->nbytes)
        {
            event->ret = 0;
            aio_onFileCompleted(event);
        }
        //continue to read
        else
        {
            swAio_event ev;
            ev.canceled = 0;
            ev.fd = event->fd;
            ev.buf = event->buf;
            ev.type = SW_AIO_READ;
            ev.nbytes = event->nbytes;
            ev.offset = file_req->offset;
            ev.flags = 0;
            ev.object = file_req;
            ev.handler = swAio_handler_read;
            ev.callback = aio_onFileCompleted;

            int ret = swAio_dispatch(&ev);
            if (ret < 0)
            {
                php_swoole_fatal_error(E_WARNING, "swoole_async: continue to read failed. Error: %s[%d]", strerror(event->error), event->error);
                goto close_file;
            }
        }
    }

    if (!ZVAL_IS_NULL(zcontent))
    {
        zval_ptr_dtor(zcontent);
    }
    if (!ZVAL_IS_NULL(zwriten))
    {
        zval_ptr_dtor(zwriten);
    }
    if (retval)
    {
        zval_ptr_dtor(retval);
    }
}

PHP_FUNCTION(swoole_async_read)
{
    zval *filename;
    zval *callback;
    zend_long buf_size = SW_AIO_DEFAULT_CHUNK_SIZE;
    zend_long offset = 0;
    int open_flag = O_RDONLY;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "zz|ll", &filename, &callback, &buf_size, &offset) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (offset < 0)
    {
        php_swoole_fatal_error(E_WARNING, "offset must be greater than 0.");
        RETURN_FALSE;
    }
    if (!php_swoole_is_callable(callback))
    {
        RETURN_FALSE;
    }
    if (buf_size > SW_AIO_MAX_CHUNK_SIZE)
    {
        buf_size = SW_AIO_MAX_CHUNK_SIZE;
    }

    zend::string str_filename(filename);
    int fd = open(str_filename.val(), open_flag, 0644);
    if (fd < 0)
    {
        php_swoole_sys_error(E_WARNING, "open(%s, O_RDONLY) failed.", str_filename.val());
        RETURN_FALSE;
    }

    struct stat file_stat;
    if (fstat(fd, &file_stat) < 0)
    {
        php_swoole_sys_error(E_WARNING, "fstat(%s) failed.", str_filename.val());
        close(fd);
        RETURN_FALSE;
    }
    if (offset >= file_stat.st_size)
    {
        php_swoole_fatal_error(E_WARNING, "offset must be less than file_size[=%jd].", (intmax_t) file_stat.st_size);
        close(fd);
        RETURN_FALSE;
    }

    void *fcnt = emalloc(buf_size);
    if (fcnt == NULL)
    {
        php_swoole_sys_error(E_WARNING, "malloc failed.");
        close(fd);
        RETURN_FALSE;
    }

    file_request *req = (file_request *) emalloc(sizeof(file_request));

    req->filename = filename;
    Z_TRY_ADDREF_P(filename);
    sw_copy_to_stack(req->filename, req->_filename);

    if (!php_swoole_is_callable(callback))
    {
        RETURN_FALSE;
    }

    req->callback = callback;
    Z_TRY_ADDREF_P(callback);
    sw_copy_to_stack(req->callback, req->_callback);
    req->refcount = nullptr;
    req->content = (char*) fcnt;
    req->once = 0;
    req->type = SW_AIO_READ;
    req->length = buf_size;
    req->offset = offset;

    swAio_event ev;
    ev.canceled = 0;
    ev.fd = fd;
    ev.buf = fcnt;
    ev.type = SW_AIO_READ;
    ev.nbytes = buf_size;
    ev.offset = offset;
    ev.flags = 0;
    ev.object = req;
    ev.handler = swAio_handler_read;
    ev.callback = aio_onFileCompleted;

    php_swoole_check_reactor();
    int ret = swAio_dispatch(&ev);
    if (ret == SW_ERR)
    {
        RETURN_FALSE;
    }
    else
    {
        RETURN_TRUE;
    }
}

PHP_FUNCTION(swoole_async_write)
{
    zval *filename;
    char *fcnt;
    size_t fcnt_len;
    off_t offset = -1;
    zval *callback = NULL;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "zs|lz", &filename, &fcnt, &fcnt_len, &offset, &callback) == FAILURE)
    {
        RETURN_FALSE;
    }
    if (fcnt_len == 0)
    {
        RETURN_FALSE;
    }
    if (offset < 0)
    {
        offset = 0;
    }
    if (callback && !ZVAL_IS_NULL(callback))
    {
        if (!php_swoole_is_callable(callback))
        {
            RETURN_FALSE;
        }
    }

    zend::string str_filename(filename);

    file_request *req = (file_request *) emalloc(sizeof(file_request));

    int fd;
    std::string key(str_filename.val(), str_filename.len());
    auto file_iterator = open_write_files.find(key);
    if (file_iterator == open_write_files.end())
    {
        int open_flag = O_WRONLY | O_CREAT;
        if (offset < 0)
        {
            open_flag |= O_APPEND;
        }
        fd = open(str_filename.val(), open_flag, 0644);
        if (fd < 0)
        {
            php_swoole_fatal_error(E_WARNING, "open(%s, %d) failed. Error: %s[%d]", str_filename.val(), open_flag, strerror(errno), errno);
            RETURN_FALSE;
        }
        swTraceLog(SW_TRACE_AIO, "open write file fd#%d", fd);
        open_write_files[key] = {fd, 1};
        req->refcount = &open_write_files[key].refcount;
    }
    else
    {
        fd = file_iterator->second.fd;
        file_iterator->second.refcount++;
        req->refcount = &file_iterator->second.refcount;
        swTraceLog(SW_TRACE_AIO, "reuse write file fd#%d", fd);
    }

    char *wt_cnt = (char *) emalloc(fcnt_len);
    req->content = wt_cnt;
    req->once = 0;
    req->type = SW_AIO_WRITE;
    req->length = fcnt_len;
    req->offset = offset;
    req->filename = filename;
    Z_TRY_ADDREF_P(filename);
    sw_copy_to_stack(req->filename, req->_filename);

    if (callback && !ZVAL_IS_NULL(callback))
    {
        req->callback = callback;
        Z_TRY_ADDREF_P(callback);
        sw_copy_to_stack(req->callback, req->_callback);
    }
    else
    {
        req->callback = NULL;
    }

    memcpy(wt_cnt, fcnt, fcnt_len);

    swAio_event ev;
    ev.canceled = 0;
    ev.fd = fd;
    ev.buf = wt_cnt;
    ev.type = SW_AIO_WRITE;
    ev.nbytes = fcnt_len;
    ev.offset = offset;
    ev.flags = 0;
    ev.object = req;
    ev.handler = swAio_handler_write;
    ev.callback = aio_onFileCompleted;

    php_swoole_check_reactor();
    int ret = swAio_dispatch(&ev);
    if (ret == SW_ERR)
    {
        RETURN_FALSE;
    }
    else
    {
        RETURN_TRUE;
    }
}

PHP_FUNCTION(swoole_async_readfile)
{
    zval *callback;
    zval *filename;

    int open_flag = O_RDONLY;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "zz", &filename, &callback) == FAILURE)
    {
        RETURN_FALSE;
    }

    zend::string str_filename(filename);

    int fd = open(str_filename.val(), open_flag, 0644);
    if (fd < 0)
    {
        php_swoole_fatal_error(E_WARNING, "open file[%s] failed. Error: %s[%d]", str_filename.val(), strerror(errno), errno);
        RETURN_FALSE;
    }
    if (!php_swoole_is_callable(callback))
    {
        close(fd);
        RETURN_FALSE;
    }

    struct stat file_stat;
    if (fstat(fd, &file_stat) < 0)
    {
        php_swoole_fatal_error(E_WARNING, "fstat failed. Error: %s[%d]", strerror(errno), errno);
        close(fd);
        RETURN_FALSE;
    }
    if (file_stat.st_size <= 0)
    {
        php_swoole_fatal_error(E_WARNING, "file is empty.");
        close(fd);
        RETURN_FALSE;
    }
    if (file_stat.st_size > SW_AIO_MAX_FILESIZE)
    {
        php_swoole_fatal_error(E_WARNING, "file_size[size=%ld|max_size=%d] is too big. Please use swoole_async_read.",
                (long int) file_stat.st_size, SW_AIO_MAX_FILESIZE);
        close(fd);
        RETURN_FALSE;
    }

    size_t length = file_stat.st_size;
    file_request *req = (file_request *) emalloc(sizeof(file_request));

    req->filename = filename;
    Z_TRY_ADDREF_P(filename);
    sw_copy_to_stack(req->filename, req->_filename);

    req->callback = callback;
    Z_TRY_ADDREF_P(callback);
    sw_copy_to_stack(req->callback, req->_callback);
    req->refcount = nullptr;
    req->content = (char *) emalloc(length);
    req->once = 1;
    req->type = SW_AIO_READ;
    req->length = length;
    req->offset = 0;

    swAio_event ev;
    ev.canceled = 0;
    ev.fd = fd;
    ev.buf = req->content;
    ev.type = SW_AIO_READ;
    ev.nbytes = length;
    ev.offset = 0;
    ev.flags = 0;
    ev.object = req;
    ev.handler = swAio_handler_read;
    ev.callback = aio_onFileCompleted;

    php_swoole_check_reactor();
    int ret = swAio_dispatch(&ev);
    if (ret == SW_ERR)
    {
        RETURN_FALSE;
    }
    else
    {
        RETURN_TRUE;
    }
}

PHP_FUNCTION(swoole_async_writefile)
{
    zval *filename;
    char *fcnt;
    size_t fcnt_len;
    zval *callback = NULL;
    zend_long flags = 0;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "zs|zl", &filename, &fcnt, &fcnt_len, &callback, &flags) == FAILURE)
    {
        RETURN_FALSE;
    }
    int open_flag = O_CREAT | O_WRONLY;
    if (flags & PHP_FILE_APPEND)
    {
        open_flag |= O_APPEND;
    }
    else
    {
        open_flag |= O_TRUNC;
    }
    if (fcnt_len == 0)
    {
        RETURN_FALSE;
    }
    if (fcnt_len > SW_AIO_MAX_FILESIZE)
    {
        php_swoole_fatal_error(
            E_WARNING, "file_size[size=%zu|max_size=%d] is too big. Please use swoole_async_write.",
            fcnt_len, SW_AIO_MAX_FILESIZE
        );
        RETURN_FALSE;
    }
    if (callback && !ZVAL_IS_NULL(callback))
    {
        if (!php_swoole_is_callable(callback))
        {
            RETURN_FALSE;
        }
    }

    zend::string str_filename(filename);
    int fd = open(str_filename.val(), open_flag, 0644);
    if (fd < 0)
    {
        php_swoole_fatal_error(E_WARNING, "open file failed. Error: %s[%d]", strerror(errno), errno);
        RETURN_FALSE;
    }

    size_t memory_size = fcnt_len;
    char *wt_cnt = (char *) emalloc(memory_size);

    file_request *req = (file_request *) emalloc(sizeof(file_request));
    req->filename = filename;
    Z_TRY_ADDREF_P(filename);
    sw_copy_to_stack(req->filename, req->_filename);

    if (callback && !ZVAL_IS_NULL(callback))
    {
        req->callback = callback;
        Z_TRY_ADDREF_P(callback);
        sw_copy_to_stack(req->callback, req->_callback);
    }
    else
    {
        req->callback = NULL;
    }
    req->refcount = nullptr;
    req->type = SW_AIO_WRITE;
    req->content = wt_cnt;
    req->once = 1;
    req->length = fcnt_len;
    req->offset = 0;

    memcpy(wt_cnt, fcnt, fcnt_len);

    swAio_event ev;
    ev.canceled = 0;
    ev.fd = fd;
    ev.buf = wt_cnt;
    ev.type = SW_AIO_WRITE;
    ev.nbytes = memory_size;
    ev.offset = 0;
    ev.flags = 0;
    ev.object = req;
    ev.handler = swAio_handler_write;
    ev.callback = aio_onFileCompleted;

    php_swoole_check_reactor();
    int ret = swAio_dispatch(&ev);
    if (ret == SW_ERR)
    {
        RETURN_FALSE;
    }
    else
    {
        RETURN_TRUE;
    }
}

PHP_FUNCTION(swoole_async_set)
{
    if (SwooleG.main_reactor != NULL)
    {
        php_swoole_fatal_error(E_ERROR, "eventLoop has already been created. unable to change settings.");
        RETURN_FALSE;
    }

    zval *zset = NULL;
    HashTable *vht;
    zval *v;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ARRAY(zset)
    ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

    vht = Z_ARRVAL_P(zset);
    if (php_swoole_array_get_value(vht, "enable_signalfd", v))
    {
        SwooleG.enable_signalfd = zval_is_true(v);
    }
    if (php_swoole_array_get_value(vht, "dns_cache_refresh_time", v))
    {
          SwooleG.dns_cache_refresh_time = zval_get_double(v);
    }
    if (php_swoole_array_get_value(vht, "socket_buffer_size", v))
    {
        SwooleG.socket_buffer_size = zval_get_long(v);
        if (SwooleG.socket_buffer_size <= 0 || SwooleG.socket_buffer_size > INT_MAX)
        {
            SwooleG.socket_buffer_size = INT_MAX;
        }
    }
    if (php_swoole_array_get_value(vht, "log_level", v))
    {
        zend_long level = zval_get_long(v);
        SwooleG.log_level = (uint32_t) (level < 0 ? UINT32_MAX : level);
    }
    if (php_swoole_array_get_value(vht, "thread_num", v) || php_swoole_array_get_value(vht, "min_thread_num", v))
    {
        SwooleAIO.max_thread_count = SwooleAIO.min_thread_count = zval_get_long(v);
    }
    if (php_swoole_array_get_value(vht, "max_thread_num", v))
    {
        SwooleAIO.max_thread_count = zval_get_long(v);
    }
    if (php_swoole_array_get_value(vht, "display_errors", v))
    {
        SWOOLE_G(display_errors) = zval_is_true(v);
    }
    if (php_swoole_array_get_value(vht, "socket_dontwait", v))
    {
        SwooleG.socket_dontwait = zval_is_true(v);
    }
    if (php_swoole_array_get_value(vht, "dns_lookup_random", v))
    {
        SwooleG.dns_lookup_random = zval_is_true(v);
    }
    if (php_swoole_array_get_value(vht, "dns_server", v))
    {
        zend::string str_v(v);
        SwooleG.dns_server_v4 = sw_strndup(str_v.val(), str_v.len());
    }
    if (php_swoole_array_get_value(vht, "use_async_resolver", v))
    {
        SwooleG.use_async_resolver = zval_is_true(v);
    }
    if (php_swoole_array_get_value(vht, "enable_coroutine", v))
    {
        SwooleG.enable_coroutine = zval_is_true(v);
    }
#if defined(HAVE_REUSEPORT) && defined(HAVE_EPOLL)
    //reuse port
    if (php_swoole_array_get_value(vht, "enable_reuse_port", v))
    {
        if (zval_is_true(v) && swoole_version_compare(SwooleG.uname.release, "3.9.0") >= 0)
        {
            SwooleG.reuse_port = 1;
        }
    }
#endif
}

PHP_FUNCTION(swoole_async_dns_lookup)
{
    zval *domain;
    zval *cb;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "zz", &domain, &cb) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (Z_TYPE_P(domain) != IS_STRING)
    {
        php_swoole_fatal_error(E_WARNING, "invalid domain name.");
        RETURN_FALSE;
    }

    if (Z_STRLEN_P(domain) == 0)
    {
        php_swoole_fatal_error(E_WARNING, "domain name empty.");
        RETURN_FALSE;
    }

    if (!php_swoole_is_callable(cb))
    {
        RETURN_FALSE;
    }

    dns_request *req = (dns_request *) emalloc(sizeof(dns_request));
    req->callback = cb;
    sw_copy_to_stack(req->callback, req->_callback);
    Z_TRY_ADDREF_P(req->callback);

    req->domain = domain;
    sw_copy_to_stack(req->domain, req->_domain);
    Z_TRY_ADDREF_P(req->domain);

    /**
     * Use asynchronous IO
     */
    if (SwooleG.use_async_resolver)
    {
        php_swoole_check_reactor();
        SW_CHECK_RETURN(swDNSResolver_request(Z_STRVAL_P(domain), php_swoole_dns_callback, (void *) req));
    }

    /**
     * Use thread pool
     */
    int buf_size;
    if (Z_STRLEN_P(domain) < SW_IP_MAX_LENGTH)
    {
        buf_size = SW_IP_MAX_LENGTH + 1;
    }
    else
    {
        buf_size = Z_STRLEN_P(domain) + 1;
    }

    void *buf = emalloc(buf_size);
    bzero(buf, buf_size);
    memcpy(buf, Z_STRVAL_P(domain), Z_STRLEN_P(domain));

    swAio_event ev;
    ev.canceled = 0;
    ev.fd = 0;
    ev.buf = buf;
    ev.type = SW_AIO_WRITE;
    ev.nbytes = buf_size;
    ev.offset = 0;
    ev.flags = 0;
    ev.object = req;
    ev.req = req;
    ev.handler = swAio_handler_gethostbyname;
    ev.callback = aio_onDNSCompleted;

    php_swoole_check_reactor();
    SW_CHECK_RETURN(swAio_dispatch(&ev));
}

static int process_stream_onRead(swReactor *reactor, swEvent *event)
{
    process_stream *ps = (process_stream *) event->socket->object;
    char *buf = ps->buffer->str + ps->buffer->length;
    size_t len = ps->buffer->size - ps->buffer->length;

    int ret = read(event->fd, buf, len);
    if (ret > 0)
    {
        ps->buffer->length += ret;
        if (ps->buffer->length == ps->buffer->size && swString_extend(ps->buffer, ps->buffer->size * 2) == 0)
        {
            return SW_OK;
        }
    }
    else if (ret < 0)
    {
        swSysError("read() failed.");
        return SW_OK;
    }

    zval *retval = NULL;
    zval args[2];

    SwooleG.main_reactor->del(SwooleG.main_reactor, ps->fd);

    if (ps->buffer->length == 0)
    {
        ZVAL_EMPTY_STRING(&args[0]);
    }
    else
    {
        ZVAL_STRINGL(&args[0], ps->buffer->str, ps->buffer->length);
    }
    swString_free(ps->buffer);

    int status;
    pid_t pid = swWaitpid(ps->pid, &status, WNOHANG);
    if (pid > 0)
    {
        array_init(&args[1]);
        add_assoc_long(&args[1], "code", WEXITSTATUS(status));
        add_assoc_long(&args[1], "signal", WTERMSIG(status));
    }
    else
    {
        ZVAL_FALSE(&args[1]);
    }

    zval *zcallback = ps->callback;
    if (sw_call_user_function_ex(EG(function_table), NULL, zcallback, &retval, 2, args, 0, NULL) == FAILURE)
    {
        php_swoole_fatal_error(E_WARNING, "swoole_async::exec callback error");
    }
    sw_zval_free(zcallback);

    if (UNEXPECTED(EG(exception)))
    {
        zend_exception_error(EG(exception), E_ERROR);
    }
    if (retval)
    {
        zval_ptr_dtor(retval);
    }
    zval_ptr_dtor(&args[0]);
    zval_ptr_dtor(&args[1]);
    close(ps->fd);
    efree(ps);

    return SW_OK;
}

PHP_METHOD(swoole_async, exec)
{
    char *command;
    size_t command_len;
    zval *callback;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "sz", &command, &command_len, &callback) == FAILURE)
    {
        RETURN_FALSE;
    }

    php_swoole_check_reactor();
    if (!swReactor_isset_handler(SwooleG.main_reactor, PHP_SWOOLE_FD_PROCESS_STREAM))
    {
        swReactor_set_handler(SwooleG.main_reactor, PHP_SWOOLE_FD_PROCESS_STREAM | SW_EVENT_READ, process_stream_onRead);
        swReactor_set_handler(SwooleG.main_reactor, PHP_SWOOLE_FD_PROCESS_STREAM | SW_EVENT_ERROR, process_stream_onRead);
    }

    pid_t pid;
    int fd = swoole_shell_exec(command, &pid, 0);
    if (fd < 0)
    {
        php_swoole_error(E_WARNING, "Unable to execute '%s'", command);
        RETURN_FALSE;
    }

    swString *buffer = swString_new(1024);
    if (buffer == NULL)
    {
        RETURN_FALSE;
    }

    process_stream *ps = ( process_stream *) emalloc(sizeof(process_stream));
    ps->callback = sw_zval_dup(callback);
    Z_TRY_ADDREF_P(ps->callback);

    ps->fd = fd;
    ps->pid = pid;
    ps->buffer = buffer;

    if (SwooleG.main_reactor->add(SwooleG.main_reactor, ps->fd, PHP_SWOOLE_FD_PROCESS_STREAM | SW_EVENT_READ) < 0)
    {
        sw_zval_free(ps->callback);
        efree(ps);
        RETURN_FALSE;
    }
    else
    {
        swConnection *_socket = swReactor_get(SwooleG.main_reactor, ps->fd);
        _socket->object = ps;
        RETURN_LONG(pid);
    }
}


/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(swoole_async)
{
//    ZEND_INIT_MODULE_GLOBALS(swoole, php_swoole_async_init_globals, NULL);
//    REGISTER_INI_ENTRIES();

    swoole_http_client_init(module_number);
    swoole_async_init(module_number);
    swoole_mysql_init(module_number);
    swoole_mmap_init(module_number);
    swoole_channel_init(module_number);
    swoole_redis_init(module_number);
    swoole_ringqueue_init(module_number);
    swoole_msgqueue_init(module_number);
    swoole_memory_pool_init(module_number);

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(swoole_async)
{
    return SUCCESS;
}
/* }}} */


/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(swoole_async)
{
    char buf[64];
    php_info_print_table_start();
    php_info_print_table_header(2, "Swoole", "enabled");
    php_info_print_table_row(2, "Author", "Swoole Team <team@swoole.com>");
    php_info_print_table_row(2, "Version", SWOOLE_VERSION);
    snprintf(buf, sizeof(buf), "%s %s", __DATE__, __TIME__);
    php_info_print_table_row(2, "Built", buf);

#ifdef SW_DEBUG
    php_info_print_table_row(2, "debug", "enabled");
#endif
#ifdef SW_LOG_TRACE_OPEN
    php_info_print_table_row(2, "trace_log", "enabled");
#endif

    php_info_print_table_row(2, "mysqlnd", "enabled");

    php_info_print_table_row(2, "async_redis", "enabled");

    php_info_print_table_end();

    DISPLAY_INI_ENTRIES();
}
/* }}} */

PHP_RINIT_FUNCTION(swoole_async)
{
    return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(swoole_async)
{
    return SUCCESS;
}
