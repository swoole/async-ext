--TEST--
swoole_mysql: connect timeout
--SKIPIF--
<?php require __DIR__ . '/../include/skipif.inc'; ?>
--FILE--
<?php
require __DIR__ . '/../include/bootstrap.php';
$swoole_mysql = new \swoole_mysql();
$r = $swoole_mysql->connect([
    "host" => "11.11.11.11",
    "port" => 9000,
    "user" => "root",
    "password" => "admin",
    "database" => "test",
    "charset" => "utf8mb4",
    'timeout' => 1.0,
], function (\swoole_mysql $swoole_mysql, $result) {
    assert($result === false);
});
swoole_event_wait();
echo "END\n";
?>
--EXPECT--
END
