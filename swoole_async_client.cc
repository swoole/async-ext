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
#include "ext/swoole/swoole_client.h"
#include "socks5.h"
#include "mqtt.h"

#include <string>

using namespace std;

#include "ext/standard/basic_functions.h"

typedef struct
{
    zend_fcall_info_cache cache_onConnect;
    zend_fcall_info_cache cache_onReceive;
    zend_fcall_info_cache cache_onClose;
    zend_fcall_info_cache cache_onError;
    zend_fcall_info_cache cache_onBufferFull;
    zend_fcall_info_cache cache_onBufferEmpty;
#ifdef SW_USE_OPENSSL
    zend_fcall_info_cache cache_onSSLReady;
#endif
    zval _object;
} client_callback;

enum client_property
{
    client_property_callback = 0,
    client_property_coroutine = 1,
    client_property_socket = 2,
};

static zend_class_entry *swoole_async_client_ce;
static zend_object_handlers swoole_async_client_handlers;

static PHP_METHOD(swoole_async_client, __construct);
static PHP_METHOD(swoole_async_client, __destruct);
static PHP_METHOD(swoole_async_client, set);
static PHP_METHOD(swoole_async_client, connect);
static PHP_METHOD(swoole_async_client, send);
static PHP_METHOD(swoole_async_client, sendfile);
static PHP_METHOD(swoole_async_client, sendto);
static PHP_METHOD(swoole_async_client, sleep);
static PHP_METHOD(swoole_async_client, wakeup);
#ifdef SW_USE_OPENSSL
static PHP_METHOD(swoole_async_client, enableSSL);
static PHP_METHOD(swoole_async_client, getPeerCert);
static PHP_METHOD(swoole_async_client, verifyPeerCert);
#endif
static PHP_METHOD(swoole_async_client, isConnected);
static PHP_METHOD(swoole_async_client, getsockname);
static PHP_METHOD(swoole_async_client, getpeername);
static PHP_METHOD(swoole_async_client, close);
static PHP_METHOD(swoole_async_client, shutdown);
static PHP_METHOD(swoole_async_client, on);

#ifdef SWOOLE_SOCKETS_SUPPORT
static PHP_METHOD(swoole_async_client, getSocket);
#endif

static swClient* php_swoole_async_client_new(zval *zobject, char *host, int host_len, int port);
static void php_swoole_async_client_free(zval *zobject, swClient *cli);

static void client_onConnect(swClient *cli);
static void client_onReceive(swClient *cli, const char *data, uint32_t length);
static void client_onClose(swClient *cli);
static void client_onError(swClient *cli);
static void client_onBufferFull(swClient *cli);
static void client_onBufferEmpty(swClient *cli);

static sw_inline void client_execute_callback(zval *zobject, enum php_swoole_client_callback_type type)
{
    client_callback *cb = (client_callback *) swoole_get_property(zobject, client_property_callback);
    const char *callback_name;

    zend_fcall_info_cache *fci_cache;

    switch(type)
    {
    case SW_CLIENT_CB_onConnect:
        callback_name = "onConnect";
        fci_cache = &cb->cache_onConnect;
        break;
    case SW_CLIENT_CB_onError:
        callback_name = "onError";
        fci_cache = &cb->cache_onError;
        break;
    case SW_CLIENT_CB_onClose:
        callback_name = "onClose";
        fci_cache = &cb->cache_onClose;
        break;
    case SW_CLIENT_CB_onBufferFull:
        callback_name = "onBufferFull";
        fci_cache = &cb->cache_onBufferFull;
        break;
    case SW_CLIENT_CB_onBufferEmpty:
        callback_name = "onBufferEmpty";
        fci_cache = &cb->cache_onBufferEmpty;
        break;
#ifdef SW_USE_OPENSSL
    case SW_CLIENT_CB_onSSLReady:
        callback_name = "onSSLReady";
        fci_cache = &cb->cache_onSSLReady;
        break;
#endif
    default:
        abort();
        return;
    }

    if (!fci_cache->function_handler)
    {
        php_swoole_fatal_error(E_WARNING, "%s has no %s callback", SW_Z_OBJCE_NAME_VAL_P(zobject), callback_name);
        return;
    }

    if (UNEXPECTED(sw_zend_call_function_ex2(NULL, fci_cache, 1, zobject, NULL) != SUCCESS))
    {
        php_swoole_fatal_error(E_WARNING, "%s->%s handler error", SW_Z_OBJCE_NAME_VAL_P(zobject), callback_name);
    }
}

static sw_inline swClient* client_get_ptr(zval *zobject)
{
    swClient *cli = (swClient *) swoole_get_object(zobject);
    if (cli && cli->socket && cli->active == 1)
    {
        return cli;
    }
    else
    {
        SwooleG.error = SW_ERROR_CLIENT_NO_CONNECTION;
        zend_update_property_long(swoole_async_client_ce, zobject, ZEND_STRL("errCode"), SwooleG.error);
        php_swoole_error(E_WARNING, "client is not connected to server");
        return NULL;
    }
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_client_void, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_client_construct, 0, 0, 1)
    ZEND_ARG_INFO(0, type)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_client_set, 0, 0, 1)
    ZEND_ARG_ARRAY_INFO(0, settings, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_client_connect, 0, 0, 1)
    ZEND_ARG_INFO(0, host)
    ZEND_ARG_INFO(0, port)
    ZEND_ARG_INFO(0, timeout)
    ZEND_ARG_INFO(0, sock_flag)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_client_send, 0, 0, 1)
    ZEND_ARG_INFO(0, data)
    ZEND_ARG_INFO(0, flag)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_client_sendfile, 0, 0, 1)
    ZEND_ARG_INFO(0, filename)
    ZEND_ARG_INFO(0, offset)
    ZEND_ARG_INFO(0, length)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_client_sendto, 0, 0, 3)
    ZEND_ARG_INFO(0, ip)
    ZEND_ARG_INFO(0, port)
    ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_client_close, 0, 0, 0)
    ZEND_ARG_INFO(0, force)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_client_shutdown, 0, 0, 1)
    ZEND_ARG_INFO(0, how)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_client_on, 0, 0, 2)
    ZEND_ARG_INFO(0, event_name)
    ZEND_ARG_CALLABLE_INFO(0, callback, 0)
ZEND_END_ARG_INFO()

#ifdef SW_USE_OPENSSL
ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_async_client_enableSSL, 0, 0, 1)
    ZEND_ARG_CALLABLE_INFO(0, callback, 0)
ZEND_END_ARG_INFO()
#endif

static const zend_function_entry swoole_async_client_methods[] =
{
    PHP_ME(swoole_async_client, __construct, arginfo_swoole_async_client_construct, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_async_client, __destruct, arginfo_swoole_async_client_void, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_async_client, set, arginfo_swoole_async_client_set, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_async_client, connect, arginfo_swoole_async_client_connect, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_async_client, send, arginfo_swoole_async_client_send, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_async_client, sendfile, arginfo_swoole_async_client_sendfile, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_async_client, sendto, arginfo_swoole_async_client_sendto, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_async_client, sleep, arginfo_swoole_async_client_void, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_async_client, wakeup, arginfo_swoole_async_client_void, ZEND_ACC_PUBLIC)
    PHP_MALIAS(swoole_async_client, pause, sleep, arginfo_swoole_async_client_void, ZEND_ACC_PUBLIC)
    PHP_MALIAS(swoole_async_client, resume, wakeup, arginfo_swoole_async_client_void, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_async_client, shutdown, arginfo_swoole_async_client_shutdown, ZEND_ACC_PUBLIC)
#ifdef SW_USE_OPENSSL
    PHP_ME(swoole_async_client, enableSSL, arginfo_swoole_async_client_enableSSL, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_async_client, getPeerCert, arginfo_swoole_async_client_void, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_async_client, verifyPeerCert, arginfo_swoole_async_client_void, ZEND_ACC_PUBLIC)
#endif
    PHP_ME(swoole_async_client, isConnected, arginfo_swoole_async_client_void, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_async_client, getsockname, arginfo_swoole_async_client_void, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_async_client, getpeername, arginfo_swoole_async_client_void, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_async_client, close, arginfo_swoole_async_client_close, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_async_client, on, arginfo_swoole_async_client_on, ZEND_ACC_PUBLIC)
#ifdef SWOOLE_SOCKETS_SUPPORT
    PHP_ME(swoole_async_client, getSocket, arginfo_swoole_async_client_void, ZEND_ACC_PUBLIC)
#endif
    PHP_FE_END
};

void php_swoole_async_client_minit(int module_number)
{
    SW_INIT_CLASS_ENTRY(swoole_async_client, "Swoole\\Async\\Client", "swoole_async_client", NULL, swoole_async_client_methods);
    SW_SET_CLASS_SERIALIZABLE(swoole_async_client, zend_class_serialize_deny, zend_class_unserialize_deny);
    SW_SET_CLASS_CLONEABLE(swoole_async_client, sw_zend_class_clone_deny);
    SW_SET_CLASS_UNSET_PROPERTY_HANDLER(swoole_async_client, sw_zend_class_unset_property_deny);
    SW_SET_CLASS_CREATE_WITH_ITS_OWN_HANDLERS(swoole_async_client);

    zend_declare_property_long(swoole_async_client_ce, ZEND_STRL("errCode"), 0, ZEND_ACC_PUBLIC);
    zend_declare_property_long(swoole_async_client_ce, ZEND_STRL("sock"), -1, ZEND_ACC_PUBLIC);
    zend_declare_property_long(swoole_async_client_ce, ZEND_STRL("type"), 0, ZEND_ACC_PUBLIC);
    zend_declare_property_null(swoole_async_client_ce, ZEND_STRL("setting"), ZEND_ACC_PUBLIC);
    /**
     * event callback
     */
    zend_declare_property_null(swoole_async_client_ce, ZEND_STRL("onConnect"), ZEND_ACC_PRIVATE);
    zend_declare_property_null(swoole_async_client_ce, ZEND_STRL("onError"), ZEND_ACC_PRIVATE);
    zend_declare_property_null(swoole_async_client_ce, ZEND_STRL("onReceive"), ZEND_ACC_PRIVATE);
    zend_declare_property_null(swoole_async_client_ce, ZEND_STRL("onClose"), ZEND_ACC_PRIVATE);
    zend_declare_property_null(swoole_async_client_ce, ZEND_STRL("onBufferFull"), ZEND_ACC_PRIVATE);
    zend_declare_property_null(swoole_async_client_ce, ZEND_STRL("onBufferEmpty"), ZEND_ACC_PRIVATE);
#ifdef SW_USE_OPENSSL
    zend_declare_property_null(swoole_async_client_ce, ZEND_STRL("onSSLReady"), ZEND_ACC_PRIVATE);
#endif

    zend_declare_class_constant_long(swoole_async_client_ce, ZEND_STRL("MSG_OOB"), MSG_OOB);
    zend_declare_class_constant_long(swoole_async_client_ce, ZEND_STRL("MSG_PEEK"), MSG_PEEK);
    zend_declare_class_constant_long(swoole_async_client_ce, ZEND_STRL("MSG_DONTWAIT"), MSG_DONTWAIT);
    zend_declare_class_constant_long(swoole_async_client_ce, ZEND_STRL("MSG_WAITALL"), MSG_WAITALL);

    zend_declare_class_constant_long(swoole_async_client_ce, ZEND_STRL("SHUT_RDWR"), SHUT_RDWR);
    zend_declare_class_constant_long(swoole_async_client_ce, ZEND_STRL("SHUT_RD"), SHUT_RD);
    zend_declare_class_constant_long(swoole_async_client_ce, ZEND_STRL("SHUT_WR"), SHUT_WR);
}

static void client_onReceive(swClient *cli, const char *data, uint32_t length)
{
    zval *zobject = (zval *) cli->object;
    zend_fcall_info_cache *fci_cache = &((client_callback *) swoole_get_property(zobject, 0))->cache_onReceive;
    zval args[2];

    args[0] = *zobject;
    ZVAL_STRINGL(&args[1], data, length);

    if (UNEXPECTED(sw_zend_call_function_ex2(NULL, fci_cache, 2, args, NULL) != SUCCESS))
    {
        php_swoole_fatal_error(E_WARNING, "%s->onReceive handler error", SW_Z_OBJCE_NAME_VAL_P(zobject));
    }

    zval_ptr_dtor(&args[1]);
}

static void client_onConnect(swClient *cli)
{
    zval *zobject = (zval *) cli->object;
#ifdef SW_USE_OPENSSL
    if (cli->ssl_wait_handshake)
    {
        client_execute_callback(zobject, SW_CLIENT_CB_onSSLReady);
    }
    else
#endif
    {
        client_execute_callback(zobject, SW_CLIENT_CB_onConnect);
    }
}

static void client_onClose(swClient *cli)
{
    zval *zobject = (zval *) cli->object;
    php_swoole_async_client_free(zobject, cli);
    client_execute_callback(zobject, SW_CLIENT_CB_onClose);
    zval_ptr_dtor(zobject);
}

static void client_onError(swClient *cli)
{
    zval *zobject = (zval *) cli->object;
    zend_update_property_long(swoole_async_client_ce, zobject, ZEND_STRL("errCode"), SwooleG.error);
    php_swoole_async_client_free(zobject, cli);
    client_execute_callback(zobject, SW_CLIENT_CB_onError);
    zval_ptr_dtor(zobject);
}

static void client_onBufferFull(swClient *cli)
{
    zval *zobject = (zval *) cli->object;
    client_execute_callback(zobject, SW_CLIENT_CB_onBufferFull);
}

static void client_onBufferEmpty(swClient *cli)
{
    zval *zobject = (zval *) cli->object;
    client_execute_callback(zobject, SW_CLIENT_CB_onBufferEmpty);
}

static void php_swoole_async_client_free(zval *zobject, swClient *cli)
{
    if (cli->timer)
    {
        swoole_timer_del(cli->timer);
        cli->timer = NULL;
    }
    //socks5 proxy config
    if (cli->socks5_proxy)
    {
        efree((void* )cli->socks5_proxy->host);
        if (cli->socks5_proxy->username)
        {
            efree((void* )cli->socks5_proxy->username);
        }
        if (cli->socks5_proxy->password)
        {
            efree((void* )cli->socks5_proxy->password);
        }
        efree(cli->socks5_proxy);
    }
    //http proxy config
    if (cli->http_proxy)
    {
        efree((void* ) cli->http_proxy->proxy_host);
        if (cli->http_proxy->user)
        {
            efree((void* )cli->http_proxy->user);
        }
        if (cli->http_proxy->password)
        {
            efree((void* )cli->http_proxy->password);
        }
        efree(cli->http_proxy);
    }
    if (cli->protocol.private_data)
    {
        sw_zend_fci_cache_discard((zend_fcall_info_cache *) cli->protocol.private_data);
        efree(cli->protocol.private_data);
        cli->protocol.private_data = nullptr;
    }

    swClient_free(cli);
    efree(cli);

#ifdef SWOOLE_SOCKETS_SUPPORT
    zval *zsocket = (zval *) swoole_get_property(zobject, client_property_socket);
    if (zsocket)
    {
        sw_zval_free(zsocket);
        swoole_set_property(zobject, client_property_socket, NULL);
    }
#endif
    //unset object
    swoole_set_object(zobject, NULL);
}

static swClient* php_swoole_async_client_new(zval *zobject, char *host, int host_len, int port)
{
    zval *ztype = sw_zend_read_property(Z_OBJCE_P(zobject), zobject, ZEND_STRL("type"), 0);
    if (ztype == NULL || ZVAL_IS_NULL(ztype))
    {
        php_swoole_fatal_error(E_ERROR, "failed to get swoole_async_client->type");
        return NULL;
    }

    long type = Z_LVAL_P(ztype);

    enum swSocket_type client_type = php_swoole_socktype(type);
    if ((client_type == SW_SOCK_TCP || client_type == SW_SOCK_TCP6) && (port <= 0 || port > SW_CLIENT_MAX_PORT))
    {
        php_swoole_fatal_error(E_WARNING, "The port is invalid");
        SwooleG.error = SW_ERROR_INVALID_PARAMS;
        return NULL;
    }

    swClient *cli = (swClient*) emalloc(sizeof(swClient));
    if (swClient_create(cli, client_type, 1) < 0)
    {
        php_swoole_sys_error(E_WARNING, "swClient_create() failed");
        zend_update_property_long(Z_OBJCE_P(zobject), zobject, ZEND_STRL("errCode"), errno);
        return NULL;
    }

    zend_update_property_long(Z_OBJCE_P(zobject), zobject, ZEND_STRL("sock"), cli->socket->fd);

#ifdef SW_USE_OPENSSL
    if (type & SW_SOCK_SSL)
    {
        cli->open_ssl = 1;
    }
#endif

    return cli;
}

static PHP_METHOD(swoole_async_client, __construct)
{
    zend_long type = 0;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "l", &type) == FAILURE)
    {
        php_swoole_fatal_error(E_ERROR, "socket type param is required");
        RETURN_FALSE;
    }

    type |= SW_FLAG_ASYNC;
    php_swoole_check_reactor();

    int client_type = php_swoole_socktype(type);
    if (client_type < SW_SOCK_TCP || client_type > SW_SOCK_UNIX_DGRAM)
    {
        const char *space, *class_name = get_active_class_name(&space);
        zend_type_error(
            "%s%s%s() expects parameter %d to be client type, unknown type " ZEND_LONG_FMT " given",
            class_name, space, get_active_function_name(), 1, type
        );
        RETURN_FALSE;
    }

    zend_update_property_long(swoole_async_client_ce, ZEND_THIS, ZEND_STRL("type"), type);

    //init
    swoole_set_object(ZEND_THIS, NULL);
    swoole_set_property(ZEND_THIS, client_property_callback, NULL);
#ifdef SWOOLE_SOCKETS_SUPPORT
    swoole_set_property(ZEND_THIS, client_property_socket, NULL);
#endif
    RETURN_TRUE;
}

static PHP_METHOD(swoole_async_client, __destruct)
{
    SW_PREVENT_USER_DESTRUCT();

    swClient *cli = (swClient *) swoole_get_object(ZEND_THIS);
    //no keep connection
    if (cli)
    {
        sw_zend_call_method_with_0_params(ZEND_THIS, swoole_async_client_ce, NULL, "close", NULL);
    }
    //free memory
    client_callback *cb = (client_callback *) swoole_get_property(ZEND_THIS, client_property_callback);
    if (cb)
    {
        efree(cb);
        swoole_set_property(ZEND_THIS, client_property_callback, NULL);
    }
}

static PHP_METHOD(swoole_async_client, set)
{
    zval *zset;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "z", &zset) == FAILURE)
    {
        RETURN_FALSE;
    }
    if (!ZVAL_IS_ARRAY(zset))
    {
        RETURN_FALSE;
    }

    zval *zsetting = sw_zend_read_and_convert_property_array(swoole_async_client_ce, ZEND_THIS, ZEND_STRL("setting"), 0);
    php_array_merge(Z_ARRVAL_P(zsetting), Z_ARRVAL_P(zset));

    RETURN_TRUE;
}

static PHP_METHOD(swoole_async_client, connect)
{
    char *host;
    size_t host_len;
    zend_long port = 0;
    double timeout = SW_CLIENT_CONNECT_TIMEOUT;
    zend_long sock_flag = 0;

    ZEND_PARSE_PARAMETERS_START(1, 4)
        Z_PARAM_STRING(host, host_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(port)
        Z_PARAM_DOUBLE(timeout)
        Z_PARAM_LONG(sock_flag)
    ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

    if (host_len == 0)
    {
        php_swoole_fatal_error(E_WARNING, "The host is empty");
        RETURN_FALSE;
    }

    swClient *cli = (swClient *) swoole_get_object(ZEND_THIS);
    if (cli)
    {
        php_swoole_fatal_error(E_WARNING, "connection to the server has already been established");
        RETURN_FALSE;
    }

    cli = php_swoole_async_client_new(ZEND_THIS, host, host_len, port);
    if (cli == NULL)
    {
        RETURN_FALSE;
    }
    swoole_set_object(ZEND_THIS, cli);

    if (cli->type == SW_SOCK_TCP || cli->type == SW_SOCK_TCP6)
    {
        //for tcp: nonblock
        //for udp: have udp connect
        sock_flag = 1;
    }

    if (cli->active == 1)
    {
        php_swoole_fatal_error(E_WARNING, "connection to the server has already been established");
        RETURN_FALSE;
    }

    zval *zset = sw_zend_read_property(swoole_async_client_ce, ZEND_THIS, ZEND_STRL("setting"), 0);
    if (zset && ZVAL_IS_ARRAY(zset))
    {
        php_swoole_client_check_setting(cli, zset);
    }

    client_callback *cb = (client_callback *) swoole_get_property(ZEND_THIS, 0);
    if (!cb)
    {
        php_swoole_fatal_error(E_ERROR, "no event callback function");
        RETURN_FALSE;
    }
    if (!cb->cache_onReceive.function_handler)
    {
        php_swoole_fatal_error(E_ERROR, "no 'onReceive' callback function");
        RETURN_FALSE;
    }
    if (swSocket_is_stream(cli->type))
    {
        if (!cb->cache_onConnect.function_handler)
        {
            php_swoole_fatal_error(E_ERROR, "no 'onConnect' callback function");
            RETURN_FALSE;
        }
        if (!cb->cache_onError.function_handler)
        {
            php_swoole_fatal_error(E_ERROR, "no 'onError' callback function");
            RETURN_FALSE;
        }
        if (!cb->cache_onClose.function_handler)
        {
            php_swoole_fatal_error(E_ERROR, "no 'onClose' callback function");
            RETURN_FALSE;
        }
        cli->onConnect = client_onConnect;
        cli->onClose = client_onClose;
        cli->onError = client_onError;
        cli->onReceive = client_onReceive;
        if (cb->cache_onBufferFull.function_handler)
        {
            cli->onBufferFull = client_onBufferFull;
        }
        if (cb->cache_onBufferEmpty.function_handler)
        {
            cli->onBufferEmpty = client_onBufferEmpty;
        }
    }
    else
    {
        if (cb->cache_onConnect.function_handler)
        {
            cli->onConnect = client_onConnect;
        }
        if (cb->cache_onClose.function_handler)
        {
            cli->onClose = client_onClose;
        }
        if (cb->cache_onError.function_handler)
        {
            cli->onError = client_onError;
        }
        cli->onReceive = client_onReceive;
    }

    zval *zobject = ZEND_THIS;
    cli->object = zobject;
    sw_copy_to_stack(cli->object, cb->_object);
    Z_TRY_ADDREF_P(zobject);

    if (cli->connect(cli, host, port, timeout, sock_flag) < 0)
    {
        if (errno == 0)
        {
            if (SwooleG.error == SW_ERROR_DNSLOOKUP_RESOLVE_FAILED)
            {
                php_swoole_error(E_WARNING, "connect to server[%s:%d] failed. Error: %s[%d]", host, (int ) port,
                        swoole_strerror(SwooleG.error), SwooleG.error);
            }
            zend_update_property_long(swoole_async_client_ce, ZEND_THIS, ZEND_STRL("errCode"), SwooleG.error);
        }
        else
        {
            php_swoole_sys_error(E_WARNING, "connect to server[%s:%d] failed", host, (int )port);
            zend_update_property_long(swoole_async_client_ce, ZEND_THIS, ZEND_STRL("errCode"), errno);
        }

        swClient *cli = (swClient *) swoole_get_object(ZEND_THIS);
        if (cli && cli->onError == NULL)
        {
            php_swoole_async_client_free(ZEND_THIS, cli);
            zval_ptr_dtor(ZEND_THIS);
        }

        RETURN_FALSE;
    }
    RETURN_TRUE;
}

static PHP_METHOD(swoole_async_client, send)
{
    char *data;
    size_t data_len;
    zend_long flags = 0;

    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(data, data_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(flags)
    ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

    if (data_len == 0)
    {
        php_swoole_fatal_error(E_WARNING, "data to send is empty");
        RETURN_FALSE;
    }

    swClient *cli = client_get_ptr(ZEND_THIS);
    if (!cli)
    {
        RETURN_FALSE;
    }

    //clear errno
    SwooleG.error = 0;
    int ret = cli->send(cli, data, data_len, flags);
    if (ret < 0)
    {
        php_swoole_sys_error(E_WARNING, "failed to send(%d) %zu bytes", cli->socket->fd, data_len);
        zend_update_property_long(swoole_async_client_ce, ZEND_THIS, ZEND_STRL("errCode"), SwooleG.error);
        RETVAL_FALSE;
    }
    else
    {
        RETURN_LONG(ret);
    }
}

static PHP_METHOD(swoole_async_client, sendto)
{
    char* ip;
    size_t ip_len;
    long port;
    char *data;
    size_t len;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "sls", &ip, &ip_len, &port, &data, &len) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (len == 0)
    {
        php_swoole_error(E_WARNING, "data to send is empty");
        RETURN_FALSE;
    }

    swClient *cli = (swClient *) swoole_get_object(ZEND_THIS);
    if (!cli)
    {
        cli = php_swoole_async_client_new(ZEND_THIS, ip, ip_len, port);
        if (cli == NULL)
        {
            RETURN_FALSE;
        }
        cli->active = 1;
        swoole_set_object(ZEND_THIS, cli);
    }

    int ret;
    if (cli->type == SW_SOCK_UDP)
    {
        ret = swSocket_udp_sendto(cli->socket->fd, ip, port, data, len);
    }
    else if (cli->type == SW_SOCK_UDP6)
    {
        ret = swSocket_udp_sendto6(cli->socket->fd, ip, port, data, len);
    }
    else if (cli->type == SW_SOCK_UNIX_DGRAM)
    {
        ret = swSocket_unix_sendto(cli->socket->fd, ip, data, len);
    }
    else
    {
        php_swoole_fatal_error(E_WARNING, "only supports SWOOLE_SOCK_(UDP/UDP6/UNIX_DGRAM)");
        RETURN_FALSE;
    }
    SW_CHECK_RETURN(ret);
}

static PHP_METHOD(swoole_async_client, sendfile)
{
    char *file;
    size_t file_len;
    zend_long offset = 0;
    zend_long length = 0;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s|ll", &file, &file_len, &offset, &length) == FAILURE)
    {
        RETURN_FALSE;
    }
    if (file_len == 0)
    {
        php_swoole_fatal_error(E_WARNING, "file to send is empty");
        RETURN_FALSE;
    }

    swClient *cli = client_get_ptr(ZEND_THIS);
    if (!cli)
    {
        RETURN_FALSE;
    }
    //only stream socket can sendfile
    if (!(cli->type == SW_SOCK_TCP || cli->type == SW_SOCK_TCP6 || cli->type == SW_SOCK_UNIX_STREAM))
    {
        php_swoole_error(E_WARNING, "dgram socket cannot use sendfile");
        RETURN_FALSE;
    }
    //clear errno
    SwooleG.error = 0;
    int ret = cli->sendfile(cli, file, offset, length);
    if (ret < 0)
    {
        SwooleG.error = errno;
        php_swoole_fatal_error(E_WARNING, "sendfile() failed. Error: %s [%d]", strerror(SwooleG.error), SwooleG.error);
        zend_update_property_long(swoole_async_client_ce, ZEND_THIS, ZEND_STRL("errCode"), SwooleG.error);
        RETVAL_FALSE;
    }
    else
    {
        RETVAL_TRUE;
    }
}

static PHP_METHOD(swoole_async_client, isConnected)
{
    swClient *cli = (swClient *) swoole_get_object(ZEND_THIS);
    if (!cli)
    {
        RETURN_FALSE;
    }
    if (!cli->socket)
    {
        RETURN_FALSE;
    }
    RETURN_BOOL(cli->active);
}

static PHP_METHOD(swoole_async_client, getsockname)
{
    swClient *cli = client_get_ptr(ZEND_THIS);
    if (!cli)
    {
        RETURN_FALSE;
    }

    if (cli->type == SW_SOCK_UNIX_STREAM || cli->type == SW_SOCK_UNIX_DGRAM)
    {
        php_swoole_fatal_error(E_WARNING, "getsockname() only support AF_INET family socket");
        RETURN_FALSE;
    }

    cli->socket->info.len = sizeof(cli->socket->info.addr);
    if (getsockname(cli->socket->fd, (struct sockaddr*) &cli->socket->info.addr, &cli->socket->info.len) < 0)
    {
        php_swoole_sys_error(E_WARNING, "getsockname() failed");
        RETURN_FALSE;
    }

    array_init(return_value);
    if (cli->type == SW_SOCK_UDP6 || cli->type == SW_SOCK_TCP6)
    {
        add_assoc_long(return_value, "port", ntohs(cli->socket->info.addr.inet_v6.sin6_port));
        char tmp[INET6_ADDRSTRLEN];
        if (inet_ntop(AF_INET6, &cli->socket->info.addr.inet_v6.sin6_addr, tmp, sizeof(tmp)))
        {
            add_assoc_string(return_value, "host", tmp);
        }
        else
        {
            php_swoole_fatal_error(E_WARNING, "inet_ntop() failed");
        }
    }
    else
    {
        add_assoc_long(return_value, "port", ntohs(cli->socket->info.addr.inet_v4.sin_port));
        add_assoc_string(return_value, "host", inet_ntoa(cli->socket->info.addr.inet_v4.sin_addr));
    }
}

#ifdef SWOOLE_SOCKETS_SUPPORT
static PHP_METHOD(swoole_async_client, getSocket)
{
    zval *zsocket = (zval *) swoole_get_property(ZEND_THIS, client_property_socket);
    if (zsocket)
    {
        RETURN_ZVAL(zsocket, 1, NULL);
    }
    swClient *cli = client_get_ptr(ZEND_THIS);
    if (!cli)
    {
        RETURN_FALSE;
    }
    if (cli->keep)
    {
        php_swoole_fatal_error(E_WARNING, "the 'getSocket' method can't be used on persistent connection");
        RETURN_FALSE;
    }
    php_socket *socket_object = swoole_convert_to_socket(cli->socket->fd);
    if (!socket_object)
    {
        RETURN_FALSE;
    }
    SW_ZEND_REGISTER_RESOURCE(return_value, (void * ) socket_object, php_sockets_le_socket());
    zsocket = sw_zval_dup(return_value);
    Z_TRY_ADDREF_P(zsocket);
    swoole_set_property(ZEND_THIS, client_property_socket, zsocket);
}
#endif

static PHP_METHOD(swoole_async_client, getpeername)
{
    swClient *cli = client_get_ptr(ZEND_THIS);
    if (!cli)
    {
        RETURN_FALSE;
    }

    if (cli->type == SW_SOCK_UDP)
    {
        array_init(return_value);
        add_assoc_long(return_value, "port", ntohs(cli->remote_addr.addr.inet_v4.sin_port));
        add_assoc_string(return_value, "host", inet_ntoa(cli->remote_addr.addr.inet_v4.sin_addr));
    }
    else if (cli->type == SW_SOCK_UDP6)
    {
        array_init(return_value);
        add_assoc_long(return_value, "port", ntohs(cli->remote_addr.addr.inet_v6.sin6_port));
        char tmp[INET6_ADDRSTRLEN];

        if (inet_ntop(AF_INET6, &cli->remote_addr.addr.inet_v6.sin6_addr, tmp, sizeof(tmp)))
        {
            add_assoc_string(return_value, "host", tmp);
        }
        else
        {
            php_swoole_fatal_error(E_WARNING, "inet_ntop() failed");
        }
    }
    else if (cli->type == SW_SOCK_UNIX_DGRAM)
    {
        add_assoc_string(return_value, "host", cli->remote_addr.addr.un.sun_path);
    }
    else
    {
        php_swoole_fatal_error(E_WARNING, "only supports SWOOLE_SOCK_(UDP/UDP6/UNIX_DGRAM)");
        RETURN_FALSE;
    }
}

static PHP_METHOD(swoole_async_client, close)
{
    int ret = 1;
    zend_bool force = 0;

    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(force)
    ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

    swClient *cli = (swClient *) swoole_get_object(ZEND_THIS);
    if (!cli || !cli->socket)
    {
        php_swoole_fatal_error(E_WARNING, "client is not connected to the server");
        RETURN_FALSE;
    }
    if (cli->closed)
    {
        php_swoole_error(E_WARNING, "client socket is closed");
        RETURN_FALSE;
    }
    if (cli->active == 0)
    {
        zval *zobject = ZEND_THIS;
        zval_ptr_dtor(zobject);
    }
    if (sw_unlikely(SWOOLE_G(req_status) != PHP_SWOOLE_CALL_USER_SHUTDOWNFUNC_BEGIN))
    {
        ret = cli->close(cli);
    }
    SW_CHECK_RETURN(ret);
}

static PHP_METHOD(swoole_async_client, on)
{
    char *cb_name;
    size_t cb_name_len;
    zval *zcallback;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "sz", &cb_name, &cb_name_len, &zcallback) == FAILURE)
    {
        RETURN_FALSE;
    }

    client_callback *cb = (client_callback *) swoole_get_property(ZEND_THIS, client_property_callback);
    if (!cb)
    {
        cb = (client_callback *) emalloc(sizeof(client_callback));
        bzero(cb, sizeof(client_callback));
        swoole_set_property(ZEND_THIS, client_property_callback, cb);
    }

    char *func_name = NULL;
    zend_fcall_info_cache func_cache;
    if (!sw_zend_is_callable_ex(zcallback, NULL, 0, &func_name, NULL, &func_cache, NULL))
    {
        php_swoole_fatal_error(E_ERROR, "function '%s' is not callable", func_name);
        return;
    }
    efree(func_name);

    if (strncasecmp("connect", cb_name, cb_name_len) == 0)
    {
        zend_update_property(swoole_async_client_ce, ZEND_THIS, ZEND_STRL("onConnect"), zcallback);
        cb->cache_onConnect = func_cache;
    }
    else if (strncasecmp("receive", cb_name, cb_name_len) == 0)
    {
        zend_update_property(swoole_async_client_ce, ZEND_THIS, ZEND_STRL("onReceive"), zcallback);
        cb->cache_onReceive = func_cache;
    }
    else if (strncasecmp("close", cb_name, cb_name_len) == 0)
    {
        zend_update_property(swoole_async_client_ce, ZEND_THIS, ZEND_STRL("onClose"), zcallback);
        cb->cache_onClose = func_cache;
    }
    else if (strncasecmp("error", cb_name, cb_name_len) == 0)
    {
        zend_update_property(swoole_async_client_ce, ZEND_THIS, ZEND_STRL("onError"), zcallback);
        cb->cache_onError = func_cache;
    }
    else if (strncasecmp("bufferFull", cb_name, cb_name_len) == 0)
    {
        zend_update_property(swoole_async_client_ce, ZEND_THIS, ZEND_STRL("onBufferFull"), zcallback);
        cb->cache_onBufferFull = func_cache;
    }
    else if (strncasecmp("bufferEmpty", cb_name, cb_name_len) == 0)
    {
        zend_update_property(swoole_async_client_ce, ZEND_THIS, ZEND_STRL("onBufferEmpty"), zcallback);
        cb->cache_onBufferEmpty = func_cache;
    }
    else
    {
        php_swoole_fatal_error(E_WARNING, "Unknown event callback type name '%s'", cb_name);
        RETURN_FALSE;
    }
    RETURN_TRUE;
}

static PHP_METHOD(swoole_async_client, sleep)
{
    swClient *cli = client_get_ptr(ZEND_THIS);
    if (!cli)
    {
        RETURN_FALSE;
    }
    SW_CHECK_RETURN(swClient_sleep(cli));
}

static PHP_METHOD(swoole_async_client, wakeup)
{
    swClient *cli = client_get_ptr(ZEND_THIS);
    if (!cli)
    {
        RETURN_FALSE;
    }
    SW_CHECK_RETURN(swClient_wakeup(cli));
}

#ifdef SW_USE_OPENSSL

static PHP_METHOD(swoole_async_client, enableSSL)
{
    swClient *cli = client_get_ptr(ZEND_THIS);
    if (!cli)
    {
        RETURN_FALSE;
    }
    if (cli->type != SW_SOCK_TCP && cli->type != SW_SOCK_TCP6)
    {
        php_swoole_fatal_error(E_WARNING, "cannot use enableSSL");
        RETURN_FALSE;
    }
    if (cli->socket->ssl)
    {
        php_swoole_fatal_error(E_WARNING, "SSL has been enabled");
        RETURN_FALSE;
    }
    cli->open_ssl = 1;
    zval *zset = sw_zend_read_property(swoole_client_ce, ZEND_THIS, ZEND_STRL("setting"), 0);
    if (ZVAL_IS_ARRAY(zset))
    {
        php_swoole_client_check_ssl_setting(cli, zset);
    }
    if (swClient_enable_ssl_encrypt(cli) < 0)
    {
        RETURN_FALSE;
    }

    zval *zcallback;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "z", &zcallback) == FAILURE)
    {
        RETURN_FALSE;
    }
    char *func_name = NULL;
    zend_fcall_info_cache func_cache;
    if (!sw_zend_is_callable_ex(zcallback, NULL, 0, &func_name, NULL, &func_cache, NULL))
    {
        php_swoole_fatal_error(E_ERROR, "function '%s' is not callable", func_name);
        return;
    }
    efree(func_name);

    client_callback *cb = (client_callback *) swoole_get_property(ZEND_THIS, client_property_callback);
    if (!cb)
    {
        php_swoole_fatal_error(E_WARNING, "the object is not an instance of swoole_client");
        RETURN_FALSE;
    }
    zend_update_property(swoole_client_ce, ZEND_THIS, ZEND_STRL("onSSLReady"), zcallback);
    cb->cache_onSSLReady = func_cache;
    cli->ssl_wait_handshake = 1;
    cli->socket->ssl_state = SW_SSL_STATE_WAIT_STREAM;

    swoole_event_set(cli->socket, SW_EVENT_WRITE);

    RETURN_TRUE;
}

static PHP_METHOD(swoole_async_client, getPeerCert)
{
    swClient *cli = client_get_ptr(ZEND_THIS);
    if (!cli)
    {
        RETURN_FALSE;
    }
    if (!cli->socket->ssl)
    {
        php_swoole_fatal_error(E_WARNING, "SSL is not ready");
        RETURN_FALSE;
    }
    char buf[8192];
    int n = swSSL_get_peer_cert(cli->socket->ssl, buf, sizeof(buf));
    if (n < 0)
    {
        RETURN_FALSE;
    }
    RETURN_STRINGL(buf, n);
}

static PHP_METHOD(swoole_async_client, verifyPeerCert)
{
    swClient *cli = client_get_ptr(ZEND_THIS);
    if (!cli)
    {
        RETURN_FALSE;
    }
    if (!cli->socket->ssl)
    {
        php_swoole_fatal_error(E_WARNING, "SSL is not ready");
        RETURN_FALSE;
    }
    zend_bool allow_self_signed = 0;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "|b", &allow_self_signed) == FAILURE)
    {
        RETURN_FALSE;
    }
    SW_CHECK_RETURN(swClient_ssl_verify(cli, allow_self_signed));
}
#endif


static PHP_METHOD(swoole_async_client, shutdown)
{
    swClient *cli = client_get_ptr(ZEND_THIS);
    if (!cli)
    {
        RETURN_FALSE;
    }
    long __how;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "l", &__how) == FAILURE)
    {
        RETURN_FALSE;
    }
    SW_CHECK_RETURN(swClient_shutdown(cli, __how));
}

