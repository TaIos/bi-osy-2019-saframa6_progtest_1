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
	RES=$(./"$EXECUTABLE")
	ERR="$?"

	if [[ ! -z "$(echo "$RES" | grep "fail")" ]]
	then
		BAD=$(($BAD+1))
	fi

	if [[ "$ERR" -eq  "$SEGMENTATION_ERR_CODE" ]]
	then
		SEG=$(($SEG+1))
	fi

	echo -e "RUN:$CNT, BAD:$BAD, SEG:$SEG TIME:$(date +%T)"
	#echo "$RES"
	CNT=$(($CNT+1))
	echo
done
