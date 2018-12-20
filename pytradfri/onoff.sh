#!/bin/bash
# Control personal Tradfri devices.
# tradfri@dfsmith.net

# -- configuration start -- #

# Tradfri gateway name or IP address
IP="gw-tradfri"
#PASSWD=`cat $HOME/gw-tradfri` # not needed since PSK

# Map of locations to Tradfri id numbers: run without arguments to list devices
declare -A location
location[livingroom]="65540 65545"
location[garage]="65550"
location[bedroom]="65537"
location[kitchen]="65548"

# Pathname of python script to do the work.
ON="./on.py"

# -- configuration end -- #

loc_to_id() {
	loc="$*"
	ids="${location[$loc]}"
	if [ -n "$ids" ]; then echo "$ids"; return; fi
	echo "$loc"
}

id_to_loc() {
	id="$*"
	found=0
	for key in "${!location[@]}"; do
		if [[ " ${location[$key]} " =~ " $id " ]]; then
			echo "$key"
			found=$((found+1))
		fi
	done
	if [ $found -lt 1 ]; then
		echo "$id"
	fi
}

action() {
	# 
	# PASSWD not used: config file with pre-shared key required
	$ON $IP $*
}

prog="$0"
cmd="$1"
shift
locs=( "$@" )

if [ "$cmd" = "" ]; then cmd="list"; locs=( "dummy" ); fi

if [ ${#locs} -lt 1 ]; then
	echo "Syntax: $prog [command id/location...]"
	exit
fi

for locname in "${locs[@]}"; do
	ids=( `loc_to_id $locname` )
	for id in "${ids[@]}"; do
		case $cmd in
		on)	action $id 254;;
		off)	action $id 0;;
		list)	action | while read id level rest; do
				printf "%-12s %3s %s\n" "`id_to_loc $id`" "$level" "$rest"
			done
			;;
		*)	echo "$prog: Unknown command $cmd";;
		esac
	done
done
