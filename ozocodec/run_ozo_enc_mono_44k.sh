#!/bin/bash

. ozo_common.sh

parseCommandLine "$@"

FILE_NAME='test_ozoaudio_enc'

IN_FILE=${FILE_NAME}.wav
OUT_FILE=out_${FILE_NAME}_mono_44100.mp4
OUT_DECODED=out_${FILE_NAME}_mono_44100.wav
REF_FILE=ref_${FILE_NAME}_mono_44100.wav

CHANNELS=1
BITRATE=128000

FOCUS='off'
ENCODING_MODE='ls-mono-44.1'

encode ${IN_FILE} ${OUT_FILE}

decode out/${OUT_FILE} ${OUT_DECODED} ''

verify ${OUT_DECODED} ${REF_FILE} 14
