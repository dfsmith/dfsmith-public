#!/bin/bash
# also curl -d '{"text": "what a load of cobblers!"}'

findvlc() {
	paths=(
		"$PROGRAMFILES/VideoLAN/VLC/vlc.exe"
		"/usr/bin/vlc"
	)
	for v in "${paths[@]}"; do
		if [ -x "$v" ]; then echo "$v"; return; fi
	done
	echo >&2 "cannot find vlc"
	exit 1
}

VLC=`findvlc`
if [ -x "/usr/bin/pv" ]; then PIPE=pv; else PIPE=cat; fi

texttojson() {
	echo -en '{\n\t"text":"'
	sed s/\"/\\\\\"/g
	echo -en '"\n}\n'
}

jsontowav() {
	URL='https://stream.watsonplatform.net/text-to-speech/api/v1/synthesize?voice=en-US_MichaelVoice'
	source "$HOME/.ibm_apikeys"
	curl -X POST -s \
		-u "apikey:$KEY_TTS" \
		--header 'Accept: audio/wav' \
		--header "Content-Type: application/json" \
		-d @- \
		$URL
}

wavtosound() {
	"$VLC" -I dummy - vlc://quit
}

texttojson | jsontowav | $PIPE | wavtosound
