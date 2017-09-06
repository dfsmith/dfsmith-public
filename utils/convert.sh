#!/bin/bash
# Convert WAV files into MP# and add ID3 data.

albyear="1964"
albartist="Smothers Brothers"
albtitle="Think Ethnic"
albcomment="vinyl transfer"
albgenre="comedy"

# Script below should not need editing.

dirname="${albartist}-${albtitle}"
destprefix="${dirname}/"

tomp3() {
	name="$1"
	src="$2"
	tnum="$3"
	lame 2>&1 --replaygain-fast -q 2 -V 2 \
		--ta "${albartist}" \
		--tl "${albtitle}" \
		--tn "$tnum" \
		--ty "${year}" \
		--tc "${albcomment}" \
		--tg "${albgenre}" \
		--tt "$name" "$src" "${destprefix}$name.mp3"
}

convert() {
	tn=1
	for f in *.wav; do
		tmpname="${f%%-range.wav}"
		name="${tmpname}"
		tomp3 "$name" "$f" $tn
		tn=$(( $tn + 1 ))
	done
	echo $info
}

export LC_ALL=C
rm -Rf "${destprefix}" "${dirname}.zip"
mkdir -p "${destprefix}"
convert | tee convert.log
cp folder.jpg "${destprefix}"
#zip -r -9 "${dirname}.zip" "${destprefix}"
grep ReplayGain convert.log
