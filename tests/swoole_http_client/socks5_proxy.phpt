--TEST--
swoole_http_client: socks5 proxy
--SKIPIF--
<?php
require __DIR__ . '/../include/skipif.inc';
skip_if_no_socks5_proxy();
?>
--FILE--
<?php
require __DIR__ . '/../include/bootstrap.php';

$domain = 'www.facebook.com';
$cli = new Swoole\Http\Client($domain, 80);
$cli->setHeaders(['Host' => $domain]);
$cli->set([
    'timeout' => 5,
    'socks5_host' => SOCKS5_PROXY_HOST,
    'socks5_port' => SOCKS5_PROXY_PORT
]);
$ret = $cli->get('/', function ($cli) {
    assert($cli->statusCode == 302);
    assert($cli->headers['location'] == 'https://www.facebook.com/');
    $cli->close();
});
assert($ret);
swoole_event::wait();
?>
--EXPECT--
