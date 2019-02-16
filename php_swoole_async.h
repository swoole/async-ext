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

BEGIN_EXTERN_C()

PHP_MINIT_FUNCTION(swoole_async);
PHP_MSHUTDOWN_FUNCTION(swoole_async);
PHP_RINIT_FUNCTION(swoole_async);
PHP_RSHUTDOWN_FUNCTION(swoole_async);
PHP_MINFO_FUNCTION(swoole_async);

void swoole_http_client_init(int module_number);
void swoole_redis_init(int module_number);
void swoole_mysql_init(int module_number);
void swoole_ringqueue_init(int module_number);
void swoole_msgqueue_init(int module_number);
void swoole_memory_pool_init(int module_number);

END_EXTERN_C()

#endif /* PHP_SWOOLE_ASYNC_H */
