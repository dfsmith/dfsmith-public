#!/bin/bash
# CY545 stepper motor controller multi-pass assembler utility
# Daniel F. Smith, 2014

assemblycode() {
	# Output CY545 assembley code to get assembled.
	# Parameters: list of labels to evaluate.
	# Use lower case for macros (see parse()).
	# Set the origin at the start of memory code (.origin <start_address>).
	# Set labels with .mylabel, etc.
	# Use label with $label_mylabel, etc.
	# Note: does not check for label page boundaries.
	# Note: cannot assemble more than 255 bytes in single E block.
	eval $*
	start=3

	# Glow-in-the-dark ghost controller code.
	# Motor enable output on B0.
	# Home input on B1.
	# Light relay on B2.
	# Trigger input on B3.
	# CTS output on B6.
	# Must fit in 256 bytes of EEPROM.
	# When home sensor, move ghost slowly to recharge light.
	# At start, move ghost out, then back to find home.
	# When triggered:
	#	motor on
	#	light on
	#	move ghost out slowly
	#	light off
	#	fly ghost across quickly
	#	dance ghost
	#	fly ghost back quickly
	#	light on
	#	move ghost in slowly
	#	light off
	#	motor off
	check="Q;? Y;E"
	#waittrigger="W 2"
	waittrigger="D 2000"
	lighton="B 4"
	lightoff="/B 4"
	motoron="/B 0"
	motoroff="B 0"
	slow="R 5"
	fast="R 200"
	goout="-;G"
	goin="+;G"
	setemerge="N 2000;$slow"
	setmid="N 10000;$fast"
	
	tr ';' '\n' <<EOF
# CY545 assembler from here
	0
	echo start
	Y $start
	E
# start of code
.origin $start
	echo wake
	
	# find home
.mainloop
	R 10
	N 5
	$motoron
	$lighton
.dehoming
	$goin
	D 100
	T 11H,$label_homing
	L 100,$label_dehoming
	Y $label_fail
.homing
	$goout
	D 100
	T 01H,$label_homed
	L 100,$label_homing
	Y $label_fail

.homed
	$setemerge
	$goout
	$goin
	$lightoff
	$motoroff
	# trigger on transition from dark->light
	echo dk
	W 02H
	echo lg
	W 12H
	echo trig
	$check
	$motoron
	$setemerge
	$lighton
	$goout
	$lightoff
	$setmid
	$goout
	$goin
	$check
	$setemerge
	$lighton
	$goin
	Y $label_mainloop
.fail
	$motoroff
	$lightoff
	echo Fail
	0
	Q
	echo end
	? Y
	Y $start
	? Y
	? M,3
EOF
}

settty() {
	stty <$1 9600 icanon icrnl -crtscts
	echo -en >$1 "\r\r\r"
	echo -en >$1 "O 0A0H\r"
	stty <$1 crtscts
}

debugout() {
	if [ $# -lt 3 ]; then echo; fi
	byte="$2"
	ct="${3:-0}"
	nct="${4:-0}"
	case "$byte" in
	$'\n')
		echo
		# print next address
		if [ $nct -gt 0 ]; then
			printf "%d:" $(( $addr + $3 ))
		else
			printf "%.*s:" ${#addr} "      "
		fi
		return
		;;
	*)
		echo -n "$byte"
		;;
	esac
}

devout() {
	if [ $# -lt 3 ]; then return; fi
	dev="$1"
	byte="$2"
	debugout "$1" "$2" "$3" "$4"
	case "$byte" in
	$'\n')
		byte=$'\r'
		delay="sleep 0.1"
		;;
	"\"")
		delay="sleep 0.2"
		;;
	*)
		delay="sleep 0.01"
		;;
	esac
	echo -en >>$dev "$byte"
	$delay
}

send() {
	# send code to device or console one character at a time
	dev="$1"
	shift
	eval "$*"
	#echo "$code"|hd
	#echo "$count"|hd
	if [ "$dev" = "none" ]; then
		output=debugout
	else
		output=devout
		# Set serial auto-baud.
		$output $dev "#"
		$output $dev "#"
		$output $dev "#"
	fi

	addr=$origin
	while [ "$code" != "" ]; do
		byte="${code::1}"
		$output $dev "${code::1}" "${count::1}" "${count:1:1}"
		addr=$(( $addr + ${count::1} ))
		code="${code:1}"
		count="${count:1}"
	done
	$output
}

addcode() {
	add="$1"
	while [ "$add" != "" ]; do
		byte="${add::1}"
		add="${add:1}"
		code+="$byte"
		if [ "$byte" = "\\" ]; then continue; fi
		count+="$counting"
		addr=$(( $addr + $counting ))
	done
}

parse() {
	# decode macros/labels for one line of assembler
	cmd="$1"
	if [ "$cmd" = "" ]; then return; fi
	shift
	case $cmd in
	"echo")
		addcode "\"$*\n\""
		;;
	".origin")
		addr="$1"
		origin="$addr"
		;;
	.*)
		if [ "$1" != "" ]; then
			labels+="label_${cmd:1}=\\\"$1\\\" "
		else
			labels+="label_${cmd:1}=\\\"$addr\\\" "
		fi
		;;
	'#')
		;;
	'"')
		addcode '"'
		;;
	'Y')
		if [ $counting -eq 0 ]; then
			addr="$1"
		fi
		addcode "$cmd $*\n"
		;;
	'E')
		addcode 'E\n'
		counting=1
		;;
	'Q')
		counting=0
		addcode 'Q\n'
		;;
	*)
		if [ "$*" != "" ]; then
			addcode "${cmd} $*\n"
		else
			addcode "${cmd}\n"
		fi
		;;
	esac
}

parser() {
	labels=""
	counting=0
	addr=0
	code=''
	count=''
	origin=0
	while read line; do
		parse $line
	done
	echo -e "code='$code' labels=\"$labels\" origin=$origin count=\"$count\""
}

# parse command line
device="none"
if [ "$1" != "" ]; then device="$1"; fi
if [ "$device" = "0" ]; then device="/dev/ttyUSB0"; fi
if [ "$device" != "none" ]; then settty "$device"; fi
set -o noglob

x1=""
oldlabels=""
while true; do
	x2=`assemblycode $oldlabels | parser`
	#echo -e "$x2" | hd
	eval $x2 # sets $code, $count and $labels
	if [ "$x1" = "$x2" ]; then break; fi
	x1="$x2"
	oldlabels="$labels"
done

#eval $labels
#echo "$code"|hd
#echo "$count"|hd
send "$device" "$x2"

