#!/bin/bash

doconversion() {
	in="$1"
	prefix="$2"
	base="TFTP:Desktops/800x480x24/"
	filename="${in##*/}"
	main="${filename%.*}.png"
	tn="TN-${filename%.*}.png"
	convert "$in" -resize 800x480^ -gravity center -extent 800x480 "$main"
	convert "$in" -resize 139x109^ -gravity center -extent 139x109 "$tn"
	
	echo -e "${prefix}Image=\"${base}${tn}\""
	echo -e "${prefix}URL=\"${base}${main}\""
}

if [ $# -lt 1 ]; then
	echo "Syntax: $0 <image_files>..."
	echo -e "\\tConvert image files to Cisco 88xx format and print List.xml file."
	echo -e "\\tNew image files will be saved to current directory."
	exit
fi

echo -e "<!-- thumbnails 139x109, main 800x480 -->"
echo -e "<CiscoIPPhoneImageList>"
for f in "${@}"; do
	echo -e "\\t<ImageItem"
	doconversion "$f" "\\t\\t"
	echo -e "\\t/>"
done
echo -e "</CiscoIPPhoneImageList>"
