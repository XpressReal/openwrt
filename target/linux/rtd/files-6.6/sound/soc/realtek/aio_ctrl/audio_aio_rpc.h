#ifndef __AUDIO_AIO_RPC_H
#define __AUDIO_AIO_RPC_H

#include "snd_rtk_audio_enum.h"
#include "snd-realtek_common.h"

struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS {
	int instanceID;
	enum AUDIO_ENUM_AIO_PRIVAETINFO type;
	int argateInfo[16];
};

/**
 * ENUM_PRIVATEINFO_AIO_AI_INTERFACE_SWITCH_CONTROL - enable ai function & config
 *
 * paramters:
 *    [0]:  set bit ENUM_DT_AI_AIN and one of a higer bit to enable the
 *          the relative ai function.
 *    [1]:  only available if ai type i2s.
 *          set bit 0 to indicate this i2s-in config is valid,
 *          set bit 1 to indicate i2s-in is pin shared, and
 *          set bit 2 to indicate i2s-in is master.
 */
enum {
	ENUM_DT_AI_EN         = 0, /* this is used by acpu */
	ENUM_DT_AI_AIN        = 1, /* alway s set this bit if using AI */
	ENUM_DT_AI_ADC        = 2,
	ENUM_DT_AI_ANALOG_IN  = 3,
	ENUM_DT_AI_ADC_AMIC   = 4,
	ENUM_DT_AI_EARC_COMBO = 6,
};

#define RTK_AUDIO_IN_I2S_STATUS       0x1
#define RTK_AUDIO_IN_I2S_PIN_SHARED   0x2
#define RTK_AUDIO_IN_I2S_MASTER       0x4

/**
 * ENUM_PRIVATEINFO_AIO_AO_INTERFACE_SWITCH_CONTROL - disable ao function & config
 *
 * paramters:
 *    [0]:  set the relative bit to disable an ai function.
 *    [1]:  only available if i2s-out is not disabled.
            set num of i2s-out channel. possiable value is 2 or 8.
 *    [2]:  only available if i2s-out is not disabled.
 *          set 1 if i2s-out mode is slave mode.
 */

enum {
	ENUM_DT_AO_DAC    = 0,
	ENUM_DT_AO_I2S    = 1,
	ENUM_DT_AO_SPDIF  = 2,
	ENUM_DT_AO_HDMI   = 3,
	ENUM_DT_AO_GLOBAL = 4,
};

#define RTK_AUDIO_OUT_I2S_2_CHANNEL    0
#define RTK_AUDIO_OUT_I2S_8_CHANNEL    1

#define RTK_AUDIO_OUT_I2S_MODE_MASTER  0
#define RTK_AUDIO_OUT_I2S_MODE_SLAVE   1

#endif
