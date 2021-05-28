#!/bin/bash

. ozo_common.sh

parseCommandLine "$@"

FILE_NAME='test_ozoaudio_enc'

IN_FILE=${FILE_NAME}.wav
OUT_FILE=out_${FILE_NAME}_ambisonics.mp4
OUT_DECODED=out_${FILE_NAME}_ambisonics.wav
REF_FILE=ref_${FILE_NAME}_ambisonics.wav

CHANNELS=2
BITRATE=512000

FOCUS='off'
ENCODING_MODE='ambisonics'

encode ${IN_FILE} ${OUT_FILE}

decode out/${OUT_FILE} ${OUT_DECODED} '-o'

verify ${OUT_DECODED} ${REF_FILE} 9
