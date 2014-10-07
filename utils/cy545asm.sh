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
	cat <<EOF
# CY545 assembler from here
	echo start
	Y $origin
	E
# start of code
.origin $origin
	D 100
	echo begin
	D 100
	echo again
	D 100
	T 11H,$label_low
	T 01H,$label_high
	echo neither
	0
.low
	D 100
	echo low
	0
.high
	D 100
	echo high
	0
	Q
	Y $origin
	? M,40
EOF
return


W 11H
.go
"go
"
/B 0
R 100
N 200
+
G
R 200
N 2000
+
G
D 1000
-
G
R 100
N 200
-
G
"stop
"
B 0
Y 100
0
Q
"end
"
Y 100
? Y
EOF
}

send() {
	# send code to device or console
	if [ "$1" = "" ]; then
		sep=$'\n'
		dev="/dev/tty"
		chardelay=""
		linedelay=""
	else
		sep=$'\r'
		dev="$1"
		chardelay="sleep 0.01"
		linedelay="sleep 0.1"
	fi
	
	while IFS= read -n1 char; do
		if [ "$char" = "#" ]; then
			char="$sep"
			delay="$linedelay"
		else
			delay="$chardelay"
		fi
		echo -en >>$dev "$char"
		$delay
	done
}

parse() {
	# decode macros/labels
	cmd="$1"
	shift
	case $cmd in
	echo)
		code+="\"$*#\""
		;;
	.origin)
		offset=$(( $1 - ${#code} ))
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

echo -e "###$code" | send $device
