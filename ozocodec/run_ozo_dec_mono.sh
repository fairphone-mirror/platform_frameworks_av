#!/bin/bash

. ozo_common.sh

parseCommandLine "$@"

FILE_NAME='test_ozoaudio_dec_mono'

IN=${FILE_NAME}.mp4
OUT=out_${FILE_NAME}.wav
REF=ref_${FILE_NAME}.wav

decode inputs/${IN} ${OUT} ''

verify ${OUT} ${REF} 14
