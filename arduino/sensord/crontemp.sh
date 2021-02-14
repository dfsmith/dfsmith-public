#!/bin/bash
# This script is designed to be called from cron every minute.
cd "/home/dfsmith/public_html/sensors"
export PATH="$PATH:/home/dfsmith/bin"

startdaemon() {
	if [ -x /home/dfsmith/bin/sensord -a -r /dev/ttyarduino ]; then
		stty 115200 </dev/ttyarduino
		/home/dfsmith/bin/sensord &
	fi
}

stopdaemon() {
	killall sensord
}

getsensorlines() {
	# returns sensor data in the form
	# seconds: time1 time1...
	# degC: temp1 temp2...
	# %rh: rh1 rh2...
	# hPa: pressure1 pressure2...
	curl -s http://localhost:8888/measurementlines || startdaemon
}

addah() {
	# add a virtual absolute humidity line to the results
	declare -a degc rh
	while read unit values; do
		case $unit in
		degC:)    degc=( $values );;
		%rh:)     rh=( $values );;
		esac
		echo "$unit $values"
	done
	n=${#degc[@]}
	if [ $n -lt 1 ]; then return; fi
	echo -n "ah:"
	for((c=0; c<n; c++)); do
		echo -n " $(rhtoah ${rh[$c]} ${degc[$c]})"
	done
	echo
}

getstarttime() {
	# return the starting time from the logfile
	( while read secs rest; do
		if [ "${secs:0:1}" != "#" ]; then
			echo "${secs%.*}"
			break
		fi
	done ) <"$1"
}

getdate() {
	date "+%Y/%m/%d" -d@$1
}

makelogfiledir() {
	# print time, from s since epoch, to YYYY/MM/DD
	# create directory YYYY/MM
	if [ $(( 10#0$1 )) -lt 1 ]; then
		exit
	fi
	DATE=`getdate $1`
	date="${DATE%/*}" # YYYY/MM/DD -> YYYY/MM
	mkdir -p "$date"
	echo "$DATE"
}

gettz() {
	IFS=:
	declare -a hhmm
	hhmm=( `date +%:z` )
	echo "$(( ( ${hhmm[0]:0:1}1 * 10#${hhmm[0]:1}*60 + 10#${hhmm[1]} ) * 60 ))"
}

boxxy() {
	# gnuplot boxxyerrorbars helper
	echo "(lt(\$1)):($1):(lt(\$1)):(lt(\$2)):($1):($2)"
}

plotlongterm() {
	# plot from 1Jan2014 to current
	tz=`gettz`
	col1='lt rgbcolor "cyan"'
	col2='lt rgbcolor "magenta"'
	col3='lt rgbcolor "yellow"'
	col4='lt rgbcolor "red"'
	col5='lt rgbcolor "green"'
	
	nice gnuplot <<EOF 1>&2
set xdata time
set format x "%F"
set timefmt "%Y-%m-%d"
set xrange ["2014-01-01":*]
set timefmt "%s"
set grid x
set grid y
set xtics rotate by -30
ctof(t)=32+1.8*t
lt(x)=x+($tz)

set style fill transparent solid 0.3 noborder
set style data boxxyerrorbars
set terminal png truecolor enhanced font "palatino,12" size 800,600

set output "daily-temp.png"
set title "Temperature range over day"
set ylabel "temperature /{/Symbol \260}F"
plot  "minmax-degC.log" using $(boxxy 'ctof($3)'  'ctof($4)' ) $col1 title "Garage box",\
      ""                using $(boxxy 'ctof($5)'  'ctof($6)' ) $col2 title "Garage outside",\
      ""                using $(boxxy 'ctof($7)'  'ctof($8)' ) $col3 title "Garage interior",\
      ""                using $(boxxy 'ctof($9)'  'ctof($10)') $col4 title "Kitchen",\
      ""                using $(boxxy 'ctof($11)' 'ctof($12)') $col5 title "Master"
set output

set output "daily-rh.png"
set title "Relative humidity range over day"
set ylabel "relative humidity /%"
plot  "minmax-%rh.log"   using $(boxxy '$3'  '$4' ) $col1 title "Garage box",\
      ""                 using $(boxxy '$5'  '$6' ) $col2 title "Garage outside",\
      ""                 using $(boxxy '$9'  '$10') $col3 title "Kitchen",\
      ""                 using $(boxxy '$11' '$12') $col4 title "Master"
set output

set output "daily-ah.png"
set title "Absolute humidity range over day"
set ylabel "absolute humidity /g/m^3"
plot  "minmax-ah.log"   using $(boxxy '$3' '$4') $col1 title "Garage box",\
      ""                using $(boxxy '$5' '$6') $col2 title "Garage outside"
set output

set output "daily-pressure.png"
set title "Atmospheric pressure range over day"
set ylabel "atmospheric pressure /hPa"
plot  "minmax-hPa.log" using $(boxxy '$7' '$8') $col1 title "Garage interior"
set output
EOF
}

probeopt() {
	name=( "garage box" "garage outside" "garage inside" "garage door" "kitchen" "master" "black box")
	  lt=(  1            2                3               4             5         6        7          )
	 col=(  \$2          \$3              \$4             \$5           \$6       \$7      \$8        )
	probe="$1"
	trans="$2"
	width="${3:-2}"
	if [ "$trans" != "" ]; then
		col="$trans(${col[$probe]})"
	else
		col="${col[$probe]}"
	fi
	
	echo "using (lt(\$1)):($col) lt ${lt[$probe]} lw ${width} title \"${name[$probe]}\""
}

plotdate() {
	date="$1"
	linkto="$2"
	lazyfile="${date}-$3.log"
	ext="png"
	terminal="png truecolor enhanced font \"palatino,12\" size 800,600 #"
	degree="\260"
	#ext="js"
	#terminal="canvas rounded size 800,600 enhanced fsize 10 lw 1.6 fontscale 1 jsdir \".\" name"
	#degree="&deg;"

	tempfile="${date}-degf.${ext}"
	pressurefile="${date}-hpa.${ext}"
	rhfile="${date}-rh.${ext}"
	ahfile="${date}-ah.${ext}"
	statefile="${date}-state.${ext}"

	if [ ! -f "${date}-degC.log" ]; then
		return
	fi

	if [ -f "$lazyfile" -a -f "$tempfile" ]; then
		lazyage=`stat -c %Z "${lazyfile}"`
		tempage=`stat -c %Z "${tempfile}"`
		if [ $(( $lazyage - $tempage )) -lt 300 ]; then
			# less than 5 minutes old: skip plotting
			return
		fi
	fi

	tz=`gettz`
	mintime=$(( `getstarttime "${date}-degC.log"` + $tz ))
	maxtime=$(( $mintime + 24*60*60 ))
	
	nice gnuplot <<EOF 2>&1 | grep -v "Skipping data file with no valid points"
set timefmt "%s"
set xdata time
set format x "%F\n%T"
ctof(t)=32+1.8*t
lt(x)=x+($tz)

pw(degC,rh)=pws(degC+tt)*rh*0.01
pws(t)=pc*exp((tc/t) * (c1*eta(t) + c2*eta(t)**1.5 + c3*eta(t)**3 + c4*eta(t)**3.5 + c5*eta(t)**4 + c6*eta(t)**7.5) )
eta(t)=1.0-(t/tc)
a(degC,rh)=c*pw(degC,rh)/(degC+tt)

c = 2.16679
tc = 647.096
tt = 273.16
pc = 22064000
c1 = -7.85951783
c2 = 1.84408259
c3 = -11.7866497
c4 = 22.6807411
c5 = -15.9618719
c6 = 1.80122502

set style data line
set grid x
set grid y
set xrange ["$mintime":"$maxtime"]

set terminal ${terminal} "chart_${date//\//_}_temp"
set output "$tempfile.tmp"
set ylabel "temperature /${degree}F"
set yrange [20:110]
plot "${date}-degC.log" \
	   $(probeopt 0 ctof 6),\
	"" $(probeopt 1 ctof 6),\
	"" $(probeopt 2 ctof 6),\
	"" $(probeopt 4 ctof 6),\
	"" $(probeopt 5 ctof 6)
set output

set terminal ${terminal} "chart_${date//\//_}_hpa"
set output "$pressurefile.tmp"
set ylabel "pressure /hPa"
set yrange [950:1025]
plot 1000 with line lt 1 lw 1 dt 3 notitle,"${date}-hPa.log" \
	   $(probeopt 2)
set output

set terminal ${terminal} "chart_${date//\//_}_rh"
set output "$rhfile.tmp"
set ylabel "relative humidity /%"
set yrange [0:100]
plot "${date}-%rh.log" \
	   $(probeopt 0),\
	"" $(probeopt 1),\
	"" $(probeopt 2),\
	"" $(probeopt 4),\
	"" $(probeopt 5)
set output

set terminal ${terminal} "chart_${date//\//_}_ah"
set output "$ahfile.tmp"
set ylabel "absolute humidity /g/m^3"
set yrange [0:20]
plot "${date}-ah.log" \
	   $(probeopt 0),\
	"" $(probeopt 1),\
	"" $(probeopt 2),\
	"" $(probeopt 4),\
	"" $(probeopt 5)
set output

set terminal ${terminal} "chart_${date//\//_}_state"
set output "$statefile.tmp"
set ylabel "state"
set yrange [0:1]
plot "${date}-state.log" \
	   $(probeopt 3)
set output

EOF
	for f in $tempfile $pressurefile $rhfile $ahfile $statefile; do
		mv "${f}.tmp" "$f"
		if [ "$linkto" != "" ]; then
			ln -sf "$f" "${linkto}${f#${date}}"
		fi
	done
}

makeahfile() {
	# assume degC and %rh files are line synchronized
	date="$1"
	while read t1 h1 h2 rest <&3 && read t2 c1 c2 rest <&4; do
		if [ "$t1" = "#" -o "$t2" = "#" ]; then continue; fi
		#if [ "$t1" != "$t2" ]; then continue; fi # small discrepancy tolerable (!)
		if [ "$h1" != "" -a "$c1" != "" ]; then a1=`rhtoah $h1 $c1`; else a1="?"; fi
		if [ "$h2" != "" -a "$c2" != "" ]; then a2=`rhtoah $h2 $c2`; else a2="?"; fi
		echo "$t1 $a1 $a2"
	done 3<${date}-%rh.log 4<${date}-degC.log
}

# start of main script

# replot charts?
if [ "$1" != "" ]; then
	case "$1" in
	-?|--help|-h)
		echo "$0 [ longterm | YYYY/MM/DD ]"
		;;
	longterm)
		plotlongterm
		;;
	????/??/??)
		plotdate "$1"
		;;
	*)
		echo "$0: unknown argument \"$1\""
		;;
	esac
	exit
fi

is_number() {
	if [ "$1" -eq "$1" ] 2>/dev/null; then
		return 0
	fi
	return 1
}

# cron script: get new sensor data
currentseconds=`date +%s`
getsensorlines | addah | (
	iseconds=0
	read unitcolon slist
	if [ "$unitcolon" != "seconds:" ]; then exit; fi
	for s in $slist; do
		is="${s%.*}" # effectively (int)floor(seconds)
		if ! is_number $is; then continue; fi
		if [ $is -gt $iseconds ]; then iseconds=$is; seconds=$s; fi
	done
	if [ $iseconds -le 0 ]; then exit; fi
	
	# check status
	dsec=$(( $currentseconds - $iseconds ))
	if [ $dsec -gt 180 ]; then stopdaemon; exit; fi
	if [ $dsec -gt 120 ]; then exit; fi
	if [ $dsec -lt 0 ]; then exit; fi
	
	LOGFILEPRE=`makelogfiledir ${iseconds}`
	if [ "$LOGFILEPRE" = "" ]; then exit; fi
	declare -A replot
	declare -a degc
	declare -a rh
	while read unitcolon values; do
		unit="${unitcolon%:}"
		LOGFILE="${LOGFILEPRE}-$unit.log"
		if [ ! -f "$LOGFILE" ]; then
			# start new logfile
			echo >>"$LOGFILE" "# tm_sec $unit"
			echo >>"$LOGFILE" "# $LOGFILEPRE"
			
			# rotate old log file
			OLDLOGFILE=`readlink today-$unit.log`
			ln -sf "$LOGFILE" "today-$unit.log"
			
			if [ "$OLDLOGFILE" != "" ]; then
				ln -sf "$OLDLOGFILE" "yesterday-$unit.log"
				replot[${OLDLOGFILE%-*}]="1"
				minmax -s <"$OLDLOGFILE" >>"minmax-$unit.log"
			fi
		fi
		echo >>"$LOGFILE" "$seconds $values"
	done

	# build complete charts for finalized day(s)
	for date in ${!replot[@]}; do
		plotdate "$date" yesterday
	done

	# keep current charts up to the minute
	plotdate "$LOGFILEPRE" today degC

	# build daily minmax charts
	if [ ${#replot[@]} -gt 0 ]; then
		plotlongterm
	fi
)
