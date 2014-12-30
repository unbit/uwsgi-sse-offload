use Redis;

my $redis = Redis->new;

while(1) {
	sleep(1);
	$redis->publish('clock', time);
}
