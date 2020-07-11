dnl $Id$
dnl config.m4 for extension swoole_async

dnl  +----------------------------------------------------------------------+
dnl  | Swoole                                                               |
dnl  +----------------------------------------------------------------------+
dnl  | This source file is subject to version 2.0 of the Apache license,    |
dnl  | that is bundled with this package in the file LICENSE, and is        |
dnl  | available through the world-wide-web at the following url:           |
dnl  | http://www.apache.org/licenses/LICENSE-2.0.html                      |
dnl  | If you did not receive a copy of the Apache2.0 license and are unable|
dnl  | to obtain it through the world-wide-web, please send a note to       |
dnl  | license@swoole.com so we can mail you a copy immediately.            |
dnl  +----------------------------------------------------------------------+
dnl  | Author: Tianfeng Han  <mikan.tenny@gmail.com>                        |
dnl  +----------------------------------------------------------------------+

PHP_ARG_ENABLE(swoole_async, swoole_async support,
[  --enable-swoole_async           Enable swoole_async support], [enable_swoole_async="yes"])

PHP_ARG_ENABLE(asan, whether to enable asan,
[  --enable-asan             Enable asan], no, no)

AC_MSG_CHECKING([if compiling with clang])
AC_COMPILE_IFELSE([
    AC_LANG_PROGRAM([], [[
        #ifndef __clang__
            not clang
        #endif
    ]])],
    [CLANG=yes], [CLANG=no]
)
AC_MSG_RESULT([$CLANG])

if test "$CLANG" = "yes"; then
    CFLAGS="$CFLAGS -std=gnu89"
fi

if test "$PHP_SWOOLE_ASYNC" != "no"; then

    PHP_ADD_LIBRARY(pthread)
    PHP_SUBST(SWOOLE_ASYNC_SHARED_LIBADD)

    AC_ARG_ENABLE(debug,
        [  --enable-debug,         compile with debug symbols],
        [PHP_DEBUG=$enableval],
        [PHP_DEBUG=0]
    )

    if test "$PHP_DEBUG_LOG" != "no"; then
        AC_DEFINE(SW_DEBUG, 1, [do we enable swoole debug])
        PHP_DEBUG=1
    fi

    if test "$PHP_ASAN" != "no"; then
        PHP_DEBUG=1
        CFLAGS="$CFLAGS -fsanitize=address -fno-omit-frame-pointer"
    fi

    if test "$PHP_TRACE_LOG" != "no"; then
        AC_DEFINE(SW_LOG_TRACE_OPEN, 1, [enable trace log])
    fi

    CFLAGS="-Wall -pthread $CFLAGS"
    LDFLAGS="$LDFLAGS -lpthread"

    PHP_ADD_LIBRARY(pthread, 1, SWOOLE_SHARED_LIBADD)

    swoole_source_file=" \
        swoole_async.cc \
        swoole_async_client.cc \
        swoole_buffer.cc \
    	swoole_channel.cc \
        swoole_http_client.cc \
        swoole_memory_pool.cc \
    	swoole_mmap.cc \
        swoole_msgqueue.cc \
        swoole_mysql.cc \
        swoole_redis.cc \
        swoole_ringqueue.cc \
    "

    PHP_NEW_EXTENSION(swoole_async, $swoole_source_file, $ext_shared,,, cxx)

    PHP_ADD_INCLUDE([$ext_srcdir])
    PHP_ADD_INCLUDE([$ext_srcdir/include])
    PHP_ADD_INCLUDE([$phpincludedir/ext/swoole])
    PHP_ADD_INCLUDE([$phpincludedir/ext/swoole/include])
    
    PHP_ADD_EXTENSION_DEP(swoole_async, swoole)

    PHP_REQUIRE_CXX()
    
    CXXFLAGS="$CXXFLAGS -Wall -Wno-unused-function -Wno-deprecated -Wno-deprecated-declarations -std=c++11"
fi
