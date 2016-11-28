#!/bin/bash
# needs pkill/pgrep

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

	if [ "$pid" = "" ]; then
		pkill $LP
		return
	fi
	
	# don't kill processes that aren't $LP
	for rp in `pgrep $LP`; do
		if [ $(( $rp )) = $(( $pid )) ]; then
			kill $pid
		fi
	done
}

startlights() {
	newstate=$1
	/home/dfsmith/github/arduino/tcl/$LP xmas $newstate >/dev/null &
	echo >$RUNCURRENT "$newstate $!"
}

lights() {
	declare -g state
	newstate="$1"
	
	if [ "$state" != "$newstate" -o "$newstate" = "off" ]; then
		stoplights
		startlights $newstate
	fi
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
dummy)
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
