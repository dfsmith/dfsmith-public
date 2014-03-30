#!/bin/bash
# This script is designed to be called from cron every minute.
cd "/home/dfsmith/public_html/sensors"
export PATH="$PATH:/home/dfsmith/bin"

gettempline() {
	# returns sensor data in the form
	# time temp1 rh1 temp2 rh2...
	curl -s http://localhost:8888/probeline
}

getsensorlines() {
	# returns sensor data in the form
	# seconds: time
	# degC: temp1 temp2...
	# %rh: rh1 rh2...
	# hPa: pressure1 pressure2...
	curl -s http://localhost:8888/measurementlines
}

getstarttime() {
	# return the starting time from the logfile
	( while read secs rest; do
		if [ "${secs:0:1}" != "#" ]; then
			echo $secs
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

plotlongterm() {
	# plot from 1Jan2014 to current
	tz=`gettz`
	nice gnuplot <<EOF 1>&2
set xdata time
set format x "%F"
set timefmt "%Y-%m-%d"
set xrange ["2014-01-01":*]
set timefmt "%s"
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
plot  "daily-temp.log" using (lt(\$1)):(ctof(\$3)):(lt(\$1)):(lt(\$2)):(ctof(\$3)):(ctof(\$4)) lt rgbcolor "cyan"    title "Garage box",\
      ""               using (lt(\$1)):(ctof(\$5)):(lt(\$1)):(lt(\$2)):(ctof(\$5)):(ctof(\$6)) lt rgbcolor "magenta" title "Garage outside"
set output

set output "daily-rh.png"
set title "Relative humidity range over day"
set ylabel "relative humidity /%"
plot  "daily-rh.log"   using (lt(\$1)):3:(lt(\$1)):(lt(\$2)):3:4 lt rgbcolor "cyan"    title "Garage box",\
      ""               using (lt(\$1)):5:(lt(\$1)):(lt(\$2)):5:6 lt rgbcolor "magenta" title "Garage outside"
set output

set output "daily-ah.png"
set title "Absolute humidity range over day"
set ylabel "absolute humidity /g/m^3"
plot  "daily-ah.log"   using (lt(\$1)):3:(lt(\$1)):(lt(\$2)):3:4 lt rgbcolor "cyan"    title "Garage box",\
      ""               using (lt(\$1)):5:(lt(\$1)):(lt(\$2)):5:6 lt rgbcolor "magenta" title "Garage outside"
set output

set output "daily-pressure.png"
set title "Atmospheric pressure range over day"
set ylabel "atmospheric pressure /hPa"
plot  "daily-pressure.log" using (lt(\$1)):3:(lt(\$1)):(lt(\$2)):3:4 lt rgbcolor "cyan"    title "Garage interior"
set output
EOF
}

plotfile() {
	logfile="$1"
	linkto="$2"
	lazy="$3"
	plotfile="${logfile%%.log}.png"

	if [ "$lazy" != "" -a -f "$plotfile" ]; then
		logage=`stat -c %Z "$logfile"`
		plotage=`stat -c %Z "$plotfile"`
		if [ $(( $logage - $plotage )) -lt 120 ]; then
			# less than 5 minutes old: skip plotting
			return
		fi
	fi

	tz=`gettz`
	mintime=$(( `getstarttime $logfile` + $tz ))
	maxtime=$(( $mintime + 24*60*60 ))
	
	nice gnuplot <<EOF 1>&2
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
set yrange [20:110]
set xrange ["$mintime":"$maxtime"]

set terminal png small size 800,600
set output "$plotfile.tmp"
plot "$logfile" using (lt(\$1)):(ctof(\$2))     lt 1 lw 6      title     "temperature /degF (probe0)",\
     ""         using (lt(\$1)):(\$3)           lt 1 lw 2      title      "rel. humidity /% (probe0)",\
     ""         using (lt(\$1)):(10*a(\$2,\$3)) lt 1 with line title "abs. humidity /g/10m3 (probe0)",\
     ""         using (lt(\$1)):(ctof(\$4))     lt 2 lw 6      title     "temperature /degF (probe1)",\
     ""         using (lt(\$1)):(\$5)           lt 2 lw 2      title      "rel. humidity /% (probe1)",\
     ""         using (lt(\$1)):(10*a(\$4,\$5)) lt 2 with line title "abs. humidity /g/10m3 (probe1)",\
     ""         using (lt(\$1)):(ctof(\$6))     lt 3 lw 6      title     "temperature /degF (probe2)",\
     ""         using (lt(\$1)):(\$7-950)       lt 3 lw 2      title  "pressure-950hPa /hPa (probe2)"
set output
EOF
	mv "$plotfile.tmp" "$plotfile"
	if [ "$linkto" != "" ]; then
		ln -sf "$plotfile" "$linkto"
	fi
}

dailyprocess() {
	declare -a ah
	logfile="$1"
	ah=( `cut -d' ' -f2,3 <"$logfile" | rhtoah -r | minmax` )
	an1="${ah[1]}"
	ax1="${ah[3]}"
	ah=( `cut -d' ' -f4,5 <"$logfile" | rhtoah -r | minmax` )
	an2="${ah[1]}"
	ax2="${ah[3]}"
	minmax <"$logfile" | {
		read min tmn tn1 hn1 tn2 hn2 tn3 pn3 restn
		read max tmx tx1 hx1 tx2 hx2 tx3 px3 restx
		echo >>"daily-temp.log"     "$tmn $tmx $tn1 $tx1 $tn2 $tx2"
		echo >>"daily-rh.log"       "$tmn $tmx $hn1 $hx1 $hn2 $hx2"
		echo >>"daily-ah.log"       "$tmn $tmx $an1 $ax1 $an2 $ax2"
		echo >>"daily-pressure.log" "$tmn $tmx $pn3 $px3"
	}
}

# start of main script

if [ "$1" != "" -a "$2" != "" ]; then
	# just plot existing named logfile
	plotfile "$1" "$2"
	exit
fi

# old scheme

TEMPLINE=`gettempline`
set -- $TEMPLINE
LOGFILEPRE=`makelogfiledir $1`
if [ "$LOGFILEPRE" = "" ]; then exit; fi
LOGFILE="${LOGFILEPRE}-tth.log"

if [ -f "$LOGFILE" ]; then
	# file exists: append and plot
	echo >>"$LOGFILE" "$TEMPLINE"
else
	# start new file and rotate old files
	echo >>"$LOGFILE" "# tm_sec temp0 rh0 temp1 rh1..."
	echo >>"$LOGFILE" "# $DATE"
	echo >>"$LOGFILE" "$TEMPLINE"

	OLDLOGFILE=`readlink today.log`
	ln -sf "$LOGFILE" today.log

	if [ "$OLDLOGFILE" != "" ]; then
		ln -sf "$OLDLOGFILE" yesterday.log
		plotfile "$OLDLOGFILE" yesterday.png
		dailyprocess "$OLDLOGFILE"
		plotlongterm
	fi
fi

plotfile "$LOGFILE" today.png lazy

# new scheme

getsensorlines | ( 
	read sunit seconds
	if [ "$sunit" != "seconds:" ]; then exit; fi
	LOGFILEPRE=`makelogfiledir ${seconds%.*}`
	if [ "$LOGFILEPRE" = "" ]; then exit; fi
	replot=""
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
				replot="$replot ${OLDLOGFILE%-*}"
			fi
		fi
		echo >>"$LOGFILE" "$seconds $values"
	done
	
	lastd=""
	for date in $replot; do
		if [ "$date" = "$lastd" ]; then continue; fi
		plotdate $date
		lastd="$date"
	done
	if [ "$replot" != "" ]; then
		plotlongterm
	fi
)
