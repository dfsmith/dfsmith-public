#!/bin/bash

LP=sendTCLudp
RUNCURRENT=/tmp/lights-state.txt
TESTDATE="now"
declare -g state pid

if [ -f $RUNCURRENT ]; then
	read <$RUNCURRENT state pid rest
else
	state=off
	pid=""
fi

stoplights() {
	declare -g state pid

	if [ "$state" = "off" ]; then return; fi
	if [ "$pid" != "" ]; then
		kill $pid
		return $?
	else
		killall $LP >/dev/null 2>&1
		return $?
	fi
}

startlights() {
	numstate=$(( 0+$1 ))
	newstate=$2
	/home/dfsmith/github/arduino/tcl/$LP xmas $numstate >/dev/null &
	echo >$RUNCURRENT "$newstate $!"
}

lights() {
	declare -g state
	
	newstate="$*"
	if [ "$state" = "$newstate" ]; then
		if [ "$newstate" = "off" ]; then
			# ensure lights off
			startlights 7 off
		fi
		return
	fi
	
	# change of state: stop the old process
	stoplights

	numstate=$newstate
	if [ "$numstate" = "off" ]; then
		numstate=7
	fi
	startlights $numstate $newstate
}

setdate() {
	date --date="$TESTDATE" +'month=%-m dom=%-d hhmm=%H%M'
}

appropriate() {
	if [ "$TESTDATE" != "now" ]; then
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

case "$1" in
"")
	echo "$state"
	;;
test)
	shift
	TESTDATE="$*"
	echo "lights `appropriate`"
	;;
appropriate)
	lights `appropriate`
	;;
*)
	lights $*
	;;
esac
