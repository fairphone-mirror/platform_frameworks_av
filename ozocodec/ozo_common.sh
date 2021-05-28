#!/bin/bash

adb='adb'
serial=''
reference=0

build='android-10.0.0_r14'
device='flame'

# Test device ID (paula3cust)
DEVICE_ID='4238B2D5-0234-41C0-8E49-BB5D3FD474DC'
# Use none device_id for testing disabled ozo encoder
#DEVICE_ID='none'

usage() {
	echo "Available options:"
	echo "-b <build>  build target, eg. android-10.0.0_r14"
	echo "-d <device> device type in use, eg. marlin, blueline, flame"
	echo "-s <serial> use device with given serial (overrides $ANDROID_SERIAL)"
	echo "-r          create reference"
}

parseCommandLine() {
	while getopts "b::d:s:r" opt; do
		case $opt in
			b)
			build=$OPTARG
			;;
			d)
			device=$OPTARG
			;;
			s)
			serial=$OPTARG
			;;
			r)
			reference=1
			;;
			\?)
			echo "Invalid option: -$OPTARG" >&2
			usage
			exit 1
			;;
			:)
			echo "Option -$OPTARG requires an argument." >&2
			exit 1
			;;
		esac
	done

	if [ ! -z "${serial}" ]; then
		adb="${adb} -s ${serial}"
	fi
}

runCommand() {
	echo "$1"
	exitOnError=1
	if [ "$#" -gt 1 ]; then
		exitOnError=0
	fi

	eval "$1"
	ret=$?
	if [ ! ${ret} -eq 0 ]; then
		echo "Command failed with error: ${ret}"
		if [ ${exitOnError} -eq 1 ]; then
			exit ${ret}
		fi
	fi
	return ${ret}
}

runCommandWithADBLog() {
	COMMAND=$1
	FILE=$2
	LOG=$3

	echo "Run command: ${COMMAND}"
	runCommand "${adb} shell logcat -c" 0
	runCommand "${adb} shell rm -f /data/local/tmp/${FILE}"
	runCommand "${adb} shell rm -f /data/local/tmp/${LOG}"
	runCommand "${adb} shell ${COMMAND}" 0
	commandRet=$?

	runCommand "mkdir -p out"
	runCommand "rm -f out/${OUTPUT}"
	runCommand "${adb} shell logcat -d > out/${LOG}"

	if [ ${commandRet} -ne 0 ]; then
		echo "Command failed with error: ${commandRet}"
		runCommand "cat out/${LOG}"
		exit ${commandRet}
	fi

	echo "Transfering file from device: ${FILEs}"
	runCommand "${adb} pull /data/local/tmp/${FILE} out"
}

decode() {
	if [ "$#" -lt 3 ]; then
    	echo "Illegal number of parameters for decode"
    	exit -1
	fi

	INPUT=$1
	OUTPUT=$2
	LOG="${2%.wav}.log"
	OPTIONS=$3

	if [ "$#" -gt 3 ]; then
		OPTIONS="$OPTIONS -g $4"
	fi

	if [ "$#" -gt 4 ]; then
		OPTIONS="$OPTIONS -l '$5'"
	fi

	echo 'Upload test input to device'
	runCommand "${adb} push ${INPUT} /data/local/tmp"

	INPUT=`basename ${INPUT}`

	COMMAND="codec ${OPTIONS} -O /data/local/tmp/${OUTPUT} /data/local/tmp/${INPUT}"

	runCommandWithADBLog "${COMMAND}" ${OUTPUT} ${LOG}
}

encode() {
	if [ "$#" -ne 2 ]; then
    	echo "Illegal number of parameters for encode"
    	exit -1
	fi

	INPUT=$1
	OUTPUT=$2
	LOG="${2%.mp4}.log"

	echo 'Upload test input to device'
	runCommand "${adb} push inputs/${INPUT} /data/local/tmp"

	COMMAND="ozoencapp -device ${DEVICE_ID} -focus ${FOCUS} -channels ${CHANNELS} -bitrate ${BITRATE} -encoding-mode ${ENCODING_MODE} /data/local/tmp/${INPUT} /data/local/tmp/${OUTPUT}"

	runCommandWithADBLog "${COMMAND}" ${OUTPUT} ${LOG}
}

verify() {
	if [ "$#" -ne 3 ]; then
    	echo "Illegal number of parameters for verify"
    	exit -1
	fi
	if [ ${reference} -eq 1 ]; then
		echo 'Creating new reference'
		runCommand "cp out/$1 refs/${build}/${device}/$2"
	else
		echo 'Verify decoded output file'
		runCommand "./ssnrcd_linux -t 7 -k $3 out/$1 refs/${build}/${device}/$2"
	fi
}
