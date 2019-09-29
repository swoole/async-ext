--TEST--
swoole_mysql: sql syntax error
--SKIPIF--
<?php require __DIR__ . '/../include/skipif.inc'; ?>
--FILE--
<?php
require __DIR__ . '/../include/bootstrap.php';

swoole_mysql_query("select", function($mysql, $result) {
    if ($mysql->errno === 1064) {
        fprintf(STDERR, "SUCCESS\n");
    } else {
        fprintf(STDERR, "FAIL\n");
    }
    $mysql->close();
});
Swoole\Event::wait();
?>
--EXPECT--
SUCCESS
closed