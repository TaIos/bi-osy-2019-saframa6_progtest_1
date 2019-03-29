#!/bin/bash
CNT=1
BAD=0
SEG=0
RES=""
ERR=0

while :
do
	RES=$(./test)
	ERR="$?"

	if [ ! -z "$(echo "$RES" | grep "fail")" ]
	then
		BAD=$(($BAD+1))
	fi

	if [ "$ERR" -eq 139 ]
	then
		SEG=$(($SEG+1))
	fi

	echo "RUN:$CNT, BAD:$BAD, SEG:$SEG TIME:$(date +%T)"
	CNT=$(($CNT+1))
done
