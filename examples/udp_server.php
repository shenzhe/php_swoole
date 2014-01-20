<?php
$serv = new swoole_server("127.0.0.1", 9502, SWOOLE_PROCESS, SWOOLE_SOCK_UDP);
$serv->set(array( 
    'worker_num' => 4,    //worker process num
));

function my_onStart($serv)
{
    echo "MasterPid={$serv->master_pid}|Manager_pid={$serv->manager_pid}\n";
    echo "Server: start.Swoole version is [".SWOOLE_VERSION."]\n";
}

function my_onReceive($serv, $fd, $from_id, $data)
{
	//var_dump($serv->connection_info($fd, $from_id));
	//echo "worker_pid=".posix_getpid().PHP_EOL;
	$serv->send($fd, 'Swoole: ' . $data, $from_id);
}

$serv->on('Start', 'my_onStart');
$serv->on('Receive', 'my_onReceive');
$serv->start();

