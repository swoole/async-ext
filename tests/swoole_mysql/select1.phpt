--TEST--
swoole_mysql: select 1
--SKIPIF--
<?php
require __DIR__ . '/../include/skipif.inc';
skip_if_in_docker('onClose event lost');
?>
--FILE--
<?php
require __DIR__ . '/../include/bootstrap.php';
swoole_mysql_query("select 1", function (\swoole_mysql $swoole_mysql, $result) {
    echo "SUCCESS\n";
    $swoole_mysql->close();
});
Swoole\Event::wait();
?>
--EXPECT--
SUCCESS
closed
