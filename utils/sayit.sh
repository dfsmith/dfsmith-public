#!/bin/bash
# also curl -d '{"text": "what a load of cobblers!"}'

TTSURL='https://watson-api-explorer.mybluemix.net/text-to-speech/api/v1/synthesize?accept=audio%2Fwav&voice=en-US_MichaelVoice'
VLC="$PROGRAMFILES/VideoLAN/VLC/vlc.exe"

texttojson() {
	echo -en '{\n\t"text":"'
	sed s/\"/\\\\\"/g
	echo -en '"\n}\n'
}

jsontowav() {
	curl -X POST -s \
		--header 'Accept: audio/wav' \
		--header "Content-Type: application/json" \
		-d @- \
		$TTSURL
}

wavtosound() {
	"$VLC" -I dummy - vlc://quit
}

texttojson | jsontowav | wavtosound
