<?php
$q = new Swoole\MsgQueue(0x95001);
var_dump($q->push("hello world"));
var_dump($q->push("php is the best"));
var_dump($q->stats());
var_dump($q->pop());
var_dump($q->pop());
var_dump($q->destroy());
