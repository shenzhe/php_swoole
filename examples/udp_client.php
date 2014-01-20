<?php
$client = new swoole_client(SWOOLE_SOCK_UDP, SWOOLE_SOCK_ASYNC); //异步非阻塞

$client->on("connect", function($cli) {
    echo "connected\n";
    $cli->send("hello world\n");
});

$client->on("receive", function($cli){
    $data = $cli->recv();
	echo "received: $data\n";
	sleep(1);
	$cli->send("hello_".rand(1000,9999));
});

$client->connect('127.0.0.1', 9502, 0.5);
