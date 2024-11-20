while true
do
	for ((i=10;$i <= 20;i++))
	do
		echo add 50 > /proc/modlist
#		sleep 0.1
	done
	echo cleanup > /proc/modlist
done
