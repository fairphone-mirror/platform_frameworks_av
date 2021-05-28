#!/bin/bash

. ozo_common.sh

parseCommandLine "$@"

FILE_NAME='test_ozoaudio_dec_ambisonics_orientation'

IN=${FILE_NAME}.mp4
OUT=out_${FILE_NAME}.wav
REF=ref_${FILE_NAME}.wav

decode inputs/${IN} ${OUT} '-o' "head-tracking" '"0 180 0 0"'

verify ${OUT} ${REF} 14
