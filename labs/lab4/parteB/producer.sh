#!/bin/bash
for ((i=1;$i <= 10;i++))
do
	echo "insertando $i"
	echo $i > /dev/prodcons
	sleep 0.5
done
