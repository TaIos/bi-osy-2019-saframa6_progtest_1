#!/bin/bash
EXECUTABLE="test"
SEGMENTATION_ERR_CODE=139
CNT=1
BAD=0
SEG=0
ERR=0
RES=""

while :
do
	echo -e "RUN:$CNT, BAD:$BAD, SEG:$SEG TIME:$(date +%T)"

	RES=$(./"$EXECUTABLE")
	echo "$RES"
	#RES=$(strace -s 99 -ff ./"$EXECUTABLE")

	ERR="$?"

	if [[ ! -z "$(echo "$RES" | grep "fail")" ]]
	then
		BAD=$(($BAD+1))
	fi

	if [[ "$ERR" -eq  "$SEGMENTATION_ERR_CODE" ]]
	then
		SEG=$(($SEG+1))
	fi

	CNT=$(($CNT+1))
	if [[ "$CNT" -eq "1000" ]]
	then
        break
	fi
	echo
done
