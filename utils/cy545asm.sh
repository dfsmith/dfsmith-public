#!/bin/bash
# CY545 stepper motor controller multi-pass assembler utility
# Daniel F. Smith, 2014

assembleycode() {
	# Output CY545 assembley code to get assembled.
	# Parameters: list of labels to evaluate.
	# Use lower case for macros (see parse()).
	# Set the origin at the start of memory code (.origin <start_address>).
	# Set labels with .mylabel, etc.
	# Use label with $label_mylabel, etc.
	# Note: does not check for label page boundaries.
	eval $*
	origin=100

	# Glow-in-the-dark ghost controller code.
	# Motor enable output on B0.
	# Home input on B1.
	# Light relay on B2.
	# Trigger input on B3.
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
	cr="#"
	waittrigger="W 3"
	lighton="B 2"
	lightoff="/B 2"
	motoron="/B 0"
	motoroff="B 0"
	slow="R 50"
	fast="R 200"
	goout="-${cr}G"
	goin="+${cr}G"
	emerge="$lighton${cr}N 200$cr$slow$cr$goout$cr$lightoff"
	retreat="$lighton${cr}N 200$cr$slow$cr$goin$cr$lightoff"
	across="N 5000$cr$fast$cr$goout"
	back="N 5000$cr$fast$cr$goin"
	cat <<EOF
# CY545 assembler from here
	echo start
	Y $origin
	E
# start of code
.origin $origin
	echo ghost awakens
	D 100
	
	# find home
.mainloop
	R 10
	N 5
.dehoming
	$goout
	T 11H,$label_homing
	L 10,$label_dehoming
	echo dehoming failed
	0
.homing
	$goin
	D 100
	T 01H,$label_homed
	L 10,$label_homing
	echo homing failed
	0

.homed
	$emerge
	$retreat
.waitfortrigger
	echo waiting...
	$waittrigger
	echo ghost triggered
	$motoron
	$emerge
	$across
	$back
	$retreat
	$motoroff
	Y $label_mainloop
	Q
	echo end
	Y $origin
	? Y
EOF
}

debugout() {
	if [ "$2" = "#" ]; then
		echo
		echo -n "$count: "
		return
	fi
	echo -n "$2"
}

devout() {
	dev="$1"
	byte="$2"
	if [ "$2" = "#" ]; then
		byte="$'\r'"
		delay="sleep 0.1"
	else
		delay="sleep 0.01"
	fi
	echo -en >>$dev "$byte"
	$delay
}

send() {
	# send code to device or console
	if [ "$1" = "" ]; then
		dev="none"
		output=debugout
	else
		dev="$1"
		output=devout
		# Set serial auto-baud.
		$output $dev "#"
		$output $dev "#"
		$output $dev "#"
	fi

	count=$offset
	while IFS= read -n1 char; do
		count=$(( $count + 1 ))
		$output $dev "$char"
	done
}

parse() {
	# decode macros/labels
	cmd="$1"
	if [ "$cmd" = "" ]; then return; fi
	shift
	case $cmd in
	echo)
		code+="\"$*#\""
		;;
	.origin)
		offset=$(( $1 - ${#code} ))
		labels+="offset=$offset "
		;;
	.*)
		if [ "$1" != "" ]; then
			labels+="label_${cmd:1}=\\\"$1\\\" "
		else
			labels+="label_${cmd:1}=\\\"$(( ${#code} + $offset ))\\\" "
		fi
		;;
	'#')
		;;
	'"')
		code+='"'
		;;
	*)
		if [ "$*" != "" ]; then
			code+="${cmd} $*#"
		else
			code+="${cmd}#"
		fi
		;;
	esac
}

parser() {
	offset=0
	labels=""
	code=''
	while read line; do
		parse $line
	done
	echo -e "code='$code' labels=\"$labels\""
}

# parse command line
device=""
if [ "$1" != "" ]; then device="$1"; fi
if [ "$device" = "0" ]; then device="/dev/ttyUSB0"; fi
set -o noglob

x1=""
oldlabels=""
while true; do
	x2=`assembleycode $oldlabels | parser`
	eval $x2 # sets $code and $labels
	# echo -e "$code" | hd
	if [ "$x1" = "$x2" ]; then break; fi
	x1="$x2"
	oldlabels="$labels"
done

eval $labels
echo -e "$code" | send $device
