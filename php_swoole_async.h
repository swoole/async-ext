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

#ifndef PHP_SWOOLE_ASYNC_H
#define PHP_SWOOLE_ASYNC_H

#include "ext/swoole/config.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ext/swoole/php_swoole.h"
#include "ext/swoole/include/swoole_config.h"
#include "ext/swoole/include/client.h"
#include "ext/swoole/include/swoole_api.h"

#ifndef SW_MYSQL_CONNECT_TIMEOUT
#define SW_MYSQL_CONNECT_TIMEOUT         1.0
#define SW_REDIS_CONNECT_TIMEOUT         1.0
#endif

static sw_inline enum swBool_type php_swoole_is_callable(zval *callback)
{
    if (!callback || ZVAL_IS_NULL(callback))
    {
        return SW_FALSE;
    }
    char *func_name = NULL;
    if (!sw_zend_is_callable(callback, 0, &func_name))
    {
        php_swoole_fatal_error(E_WARNING, "function '%s' is not callable", func_name);
        efree(func_name);
        return SW_FALSE;
    }
    else
    {
        efree(func_name);
        return SW_TRUE;
    }
}

static sw_inline int sw_call_user_function_ex(HashTable *function_table, zval* object_p, zval *function_name, zval **retval_ptr_ptr, uint32_t param_count, zval *params, int no_separation, HashTable* ymbol_table)
{
    static zval _retval;
    int ret;
    *retval_ptr_ptr = &_retval;
    ret = call_user_function_ex(function_table, object_p, function_name, &_retval, param_count, param_count ? params : NULL, no_separation, ymbol_table);
    if (UNEXPECTED(EG(exception)))
    {
        zend_exception_error(EG(exception), E_ERROR);
    }
    return ret;
}

BEGIN_EXTERN_C()

PHP_MINIT_FUNCTION(swoole_async);
PHP_MSHUTDOWN_FUNCTION(swoole_async);
PHP_RINIT_FUNCTION(swoole_async);
PHP_RSHUTDOWN_FUNCTION(swoole_async);
PHP_MINFO_FUNCTION(swoole_async);

void php_swoole_async_client_minit(int module_number);
void swoole_http_client_init(int module_number);
void swoole_redis_init(int module_number);
void swoole_mysql_init(int module_number);
void swoole_mmap_init(int module_number);
void swoole_channel_init(int module_number);
void swoole_ringqueue_init(int module_number);
void swoole_msgqueue_init(int module_number);
void swoole_memory_pool_init(int module_number);

END_EXTERN_C()

#endif /* PHP_SWOOLE_ASYNC_H */
