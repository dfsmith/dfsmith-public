#!/usr/bin/env python3
#
# Fix DTEND being before DTSTART in ics (icalendar) file
#

import sys
import icalendar
from datetime import datetime,timedelta

def readfile(filename):
	with open(filename,"rb") as fp:
		return fp.read()

def writefile(filename,data):
	with open(filename,"wb") as fp:
		fp.write(data)

def ics_fix_dtend(filename):
	ics=readfile(filename)
	cal=icalendar.Calendar.from_ical(ics)
	if not cal:
		return None
	
	events=cal.walk("VEVENT")
	for ev in events:	
		dtstart=ev['DTSTART'].dt
		dtend=ev['DTEND'].dt
		
		if not isinstance(dtstart,datetime) or not isinstance(dtend,datetime):
			print(f"Event has no time")
			return None
		
		if dtstart.timestamp() <= dtend.timestamp():
			print(f"{filename} looks okay")
			return None

		print(f"{filename} {ev}")
		print(f"   start {type(dtstart)}: {dtstart}")
		print(f"     end {type(dtend)}: {dtend}")
		
		dtend=dtstart + timedelta(hours=1)
		ev['DTEND'].dt=dtend

	ics=cal.to_ical()
	return ics

if __name__ == "__main__":
	f=sys.argv[1]
	newcal=ics_fix_dtend(f)
	if newcal:
		rename(f,f"{f}.old")
		writefile(f,newcal)
