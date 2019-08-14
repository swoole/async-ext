--TEST--
Swoole\Async\Client: connect twice
--SKIPIF--
<?php require __DIR__ . '/../include/skipif.inc'; ?>
--FILE--
<?php
require __DIR__ . '/../include/bootstrap.php';

use Swoole\Async\Client;

$start = microtime(true);

$cli = new  Client(SWOOLE_SOCK_TCP);
$cli->on("connect", function (Client $cli) {
    Assert::true(false, 'never here');
});
$cli->on("receive", function (Client $cli, $data) {
    Assert::true(false, 'never here');
});
$cli->on("error", function (Client $cli) {
    echo "error\n";
});
$cli->on("close", function (Client $cli) {
    echo "close\n";
});

function refcount($var)
{
    ob_start();
    debug_zval_dump($var);
    preg_match('/refcount\((?<refcount>\d)\)/', ob_get_clean(), $matches);
    return intval($matches["refcount"]) - 3;
}

@$cli->connect("11.11.11.11", 9000, 0.1);
@$cli->connect("11.11.11.11", 9000, 0.1);
@$cli->connect("11.11.11.11", 9000, 0.1);
@$cli->connect("11.11.11.11", 9000, 0.1);
@$cli->connect("11.11.11.11", 9000, 0.1);
Swoole\Event::wait();
?>
--EXPECT--
error
