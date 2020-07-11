/*
 +----------------------------------------------------------------------+
 | Swoole                                                               |
 +----------------------------------------------------------------------+
 | Copyright (c) 2012-2015 The Swoole Group                             |
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
#include "ext/swoole/include/channel.h"

static PHP_METHOD(swoole_channel, __construct);
static PHP_METHOD(swoole_channel, __destruct);
static PHP_METHOD(swoole_channel, push);
static PHP_METHOD(swoole_channel, pop);
static PHP_METHOD(swoole_channel, peek);
static PHP_METHOD(swoole_channel, stats);

zend_class_entry *swoole_channel_ce;
static zend_object_handlers swoole_channel_handlers;

using namespace swoole;

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_channel_construct, 0, 0, 1)
    ZEND_ARG_INFO(0, size)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_channel_push, 0, 0, 1)
    ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_void, 0, 0, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry swoole_channel_methods[] =
{
    PHP_ME(swoole_channel, __construct, arginfo_swoole_channel_construct, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_channel, __destruct, arginfo_swoole_void, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_channel, push, arginfo_swoole_channel_push, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_channel, pop, arginfo_swoole_void, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_channel, peek, arginfo_swoole_void, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_channel, stats, arginfo_swoole_void, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

void swoole_channel_init(int module_number)
{
    SW_INIT_CLASS_ENTRY(swoole_channel, "Swoole\\Channel", "swoole_channel", NULL, swoole_channel_methods);
    SW_SET_CLASS_SERIALIZABLE(swoole_channel, zend_class_serialize_deny, zend_class_unserialize_deny);
    SW_SET_CLASS_CLONEABLE(swoole_channel, sw_zend_class_clone_deny);
    SW_SET_CLASS_UNSET_PROPERTY_HANDLER(swoole_channel, sw_zend_class_unset_property_deny);
}

static PHP_METHOD(swoole_channel, __construct)
{
    long size;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "l", &size) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (size < SW_BUFFER_SIZE_STD)
    {
        size = SW_BUFFER_SIZE_STD;
    }

    auto *chan = swoole::Channel::make(size, SW_BUFFER_SIZE_STD, swoole::SW_CHAN_LOCK | swoole::SW_CHAN_SHM);
    if (chan == NULL)
    {
        zend_throw_exception(swoole_exception_ce, "failed to create channel.", SW_ERROR_MALLOC_FAIL);
        RETURN_FALSE;
    }
    swoole_set_object(ZEND_THIS, chan);
}

static PHP_METHOD(swoole_channel, __destruct)
{
    SW_PREVENT_USER_DESTRUCT();

    swoole_set_object(ZEND_THIS, NULL);
}

static PHP_METHOD(swoole_channel, push)
{
    Channel *chan = (Channel *) swoole_get_object(ZEND_THIS);
    zval *zdata;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "z", &zdata) == FAILURE)
    {
        RETURN_FALSE;
    }

    swEventData buf;
    if (php_swoole_task_pack(&buf, zdata) < 0)
    {
        RETURN_FALSE;
    }
    SW_CHECK_RETURN(chan->push(&buf, sizeof(buf.info) + buf.info.len));
}

static PHP_METHOD(swoole_channel, pop)
{
    Channel *chan = (Channel *) swoole_get_object(ZEND_THIS);
    swEventData buf;

    int n = chan->pop(&buf, sizeof(buf));
    if (n < 0)
    {
        RETURN_FALSE;
    }

    zval *ret_data = php_swoole_task_unpack(&buf);
    if (ret_data == NULL)
    {
        RETURN_FALSE;
    }

    RETVAL_ZVAL(ret_data, 0, NULL);
    efree(ret_data);
}

static PHP_METHOD(swoole_channel, peek)
{
    Channel *chan = (Channel *) swoole_get_object(ZEND_THIS);
    swEventData buf;

    int n = chan->peek(&buf, sizeof(buf));
    if (n < 0)
    {
        RETURN_FALSE;
    }

    swTask_type(&buf) |= SW_TASK_PEEK;
    zval *ret_data = php_swoole_task_unpack(&buf);
    if (ret_data == NULL)
    {
        RETURN_FALSE;
    }

    RETVAL_ZVAL(ret_data, 0, NULL);
    efree(ret_data);
}

static PHP_METHOD(swoole_channel, stats)
{
    Channel *chan = (Channel *) swoole_get_object(ZEND_THIS);
    array_init(return_value);

    add_assoc_long_ex(return_value, ZEND_STRL("queue_num"), chan->count());
    add_assoc_long_ex(return_value, ZEND_STRL("queue_bytes"), chan->get_bytes());
}
