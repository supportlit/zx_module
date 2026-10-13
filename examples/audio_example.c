
#include <stdio.h>
#include <string.h>
#include "typedef.h"
#include "osi_api.h"
#include "audio_api.h"
#include "fs_api.h"
#include "mfpr_api.h"
#include "gpio_api.h"

//#define MODULE_TYPE_ZX600RG //the compile macro for ZX600RG
#ifdef MODULE_TYPE_ZX600RG
#define PA_GPIO_ENABLE	GPIO_49
#endif

void appimg_exit(void);

static void WAV_TEST()
{
	auPlayWavFile("1k 0db.wav",NULL);
}

static void MP3_TEST()
{
    Mp3PlayConfigInfo config = { 0 };
    config.block_size = 2 * 1024;
    mp3PlayStart("1k 0db.mp3", &config, 0);
}

static void STREAM_TEST()
{
    char buf[16000*10] = {0};
    int fd = fs_open("1k 0db.wav", "rb");
	fs_read(buf, 1, 16000*10, fd);
	auPlayPcmStream(16000, 1, AUSAMPLE_FORMAT_S16, (char *)buf, 16000*10);
}

#ifdef MODULE_TYPE_ZX600RG
static void prvPaCfg(void)
{
	/*Multip Function Pin confige*/
    unsigned int mfp_pa_gpio[] = {
        MFP_REG(PA_GPIO_ENABLE) | MFP_AF0 | MFP_DRIVE_VERY_SLOW | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_EOC
    };
    mfp_config(mfp_pa_gpio);

	/*GPIO function set*/
	GpioSetDirection(PA_GPIO_ENABLE, GPIO_OUT_PIN); //OUTPUT
}
#endif/*MODULE_TYPE_ZX600RG*/

static void prvAUThread(void *param)
{
	int audio_type = 0;
	/*
	audio_test_type:
	0: wav
	1: mp3
	2: stream
	*/
#ifdef MODULE_TYPE_ZX600RG	
	prvPaCfg();
#endif
    AudioHAL_AifUseInternalCodec();
    AudioHAL_AifSetVolume(11);

#ifdef MODULE_TYPE_ZX600RG
	GpioSetLevel(49, 1);
#endif/*MODULE_TYPE_ZX600RG*/
	switch (audio_type)
	{
	case 0:
		WAV_TEST();
		break;
	case 1:
	MP3_TEST();
		break;
	case 2:
		STREAM_TEST();
		break;
	
	default:
		break;
	}
#ifdef MODULE_TYPE_ZX600RG
	GpioSetLevel(49, 0);
#endif/*MODULE_TYPE_ZX600RG*/
	
	appimg_exit();
}

int appimg_enter(void *param)
{
	osiThreadCreate("audio example", prvAUThread, NULL, 231, 16000*11);
    return 0;
}

void appimg_exit(void)
{
    RTI_LOG("application image exit");
}