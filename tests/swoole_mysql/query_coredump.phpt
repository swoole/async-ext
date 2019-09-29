--TEST--
swoole_mysql: query coredump
--SKIPIF--
<?php require __DIR__ . '/../include/skipif.inc'; ?>
--FILE--
<?php
require __DIR__ . '/../include/bootstrap.php';

swoole_mysql_query("select 1", function($swoole_mysql, $result) {
    fprintf(STDERR, "SUCCESS\n");
    $swoole_mysql->close();
});

Swoole\Event::wait();
?>
--EXPECT--
SUCCESS
closed
