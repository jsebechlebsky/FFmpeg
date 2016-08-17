fate-tee-muxer-h264: CMD = ffmpeg -i $(TARGET_SAMPLES)/mkv/1242-small.mkv -vframes 11\
                           -c:v copy -c:a copy -map v:0 -map a:0 -flags +bitexact\
                           -fflags +bitexact -fflags +bitexact -f tee\
			   "[f=framecrc]$(TARGET_PATH)/tests/data/fate/tee-muxer-h264-copy|[f=framecrc:select=1]$(TARGET_PATH)/tests/data/fate/tee-muxer-h264-audio"
fate-tee-muxer-h264: CMP = mdiff
FATE-SAMPLES-TEE-MUXER-$(call ALLYES, TEE_MUXER, MATROSKA_DEMUXER, H264_DECODER) += fate-tee-muxer-h264

fate-tee-muxer-ignorefail: CMD = ./ffmpeg  -f lavfi -i "testsrc=s=640x480" -f lavfi -i "sine"\
			         -t 1 -map 0:v -map 1:a -c:v copy -c:a copy -flags +bitexact -fflags +bitexact -f tee\
				 "[f=framecrc]$(TARGET_PATH)/tests/data/fate/tee-muxer-ignorefail|[f=framecrc:onfail=ignore]$(TARGET_PATH)/dev/full"
FATE-TEE-MUXER-$(CONFIG_TEE_MUXER) += fate-tee-muxer-ignorefail

fate-tee-muxer-tstsrc: CMD = ./ffmpeg  -f lavfi -i "testsrc=s=640x480" -f lavfi -i "sine"\
                             -t 1 -map 0:v -map 1:a -c:v copy -c:a copy -flags +bitexact -fflags +bitexact -f tee\
			     "[f=framecrc]$(TARGET_PATH)/tests/data/fate/tee-muxer-tstsrc-copy|[f=framecrc:select=1]$(TARGET_PATH)/tests/data/fate/tee-muxer-tstsrc-audio"
fate-tee-muxer-tstsrc: CMP = mdiff
FATE-TEE-MUXER-$(CONFIG_TEE_MUXER) += fate-tee-muxer-tstsrc

FATE_SAMPLES_FFMPEG += $(FATE-SAMPLES-TEE-MUXER-yes)
FATE_FFMPEG += $(FATE-TEE-MUXER-yes)

fate-tee-muxer: $(FATE-TEE-MUXER-yes) $(FATE-SAMPLES-TEE-MUXER-yes)
