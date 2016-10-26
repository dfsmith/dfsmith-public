#!/bin/bash
LP=sendTCLudp
RUNCURRENT=/tmp/lights-state.txt
TESTDATE=""

if [ -f $RUNCURRENT ]; then
	read <$RUNCURRENT state pid rest
else
	state=off
	pid=""
fi

lights() {
	newstate="$*"
	if [ "$state" = "$newstate" ]; then return; fi
	
	numstate=$newstate
	if [ "$numstate" = "off" ]; then numstate=7; fi
	/home/dfsmith/github/arduino/tcl/$LP xmas $numstate >/dev/null &
	echo >$RUNCURRENT "$newstate $!"
}

stoplights() {
	if [ "$state" = "off" ]; then return; fi
	if [ "$pid" != "" ]; then
		kill $pid
	else
		killall $LP >/dev/null 2>&1
	fi
}

setdate() {
	date "$TESTDATE" +"month=%-m dom=%-d hhmm=%H%M"
}

appropriate() {
	if [ "$TESTDATE" != "" ]; then
		echo >&2 "condition on `setdate`"
	fi
	eval `setdate`
	hm="$((10#$hhmm))"
	
	case $month in
	10)	# halloween
		start=1700; stop=2330
		pattern=3
		;;
	12)	# Christmas
		start=1600; stop=2330
		pattern=5
		;;
	*)	start=0; stop=0
		pattern=7
		;;
	esac

	if [ $hm -lt $start -o $hm -ge $stop ]; then
		echo "off"
	else
		echo $pattern
	fi
}

#######################

stoplights
case "$1" in
"")
	echo "$state"
	;;
test)
	shift
	TESTDATE="--date=$*"
	echo "lights `appropriate`"
	;;
appropriate)
	lights `appropriate`
	;;
*)
	lights $*
	;;
esac
