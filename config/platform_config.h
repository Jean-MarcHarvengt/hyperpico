#ifndef _PLATFORM_CONFIG_H_
#define _PLATFORM_CONFIG_H_


#define KEYLAYOUT      KLAYOUT_UK

//#define HAS_SND        1
//#define HAS_USBHOST    1		// enable USB keyboard (standalone mode only)  
//#define HAS_NETWORK    1		// enable network wifi (standalone mode only)  


#ifdef HAS_SND

#define SOUNDRATE 22050                           // sound rate [Hz]

//#define AUDIO_8BIT     1
#define AUDIO_1DMA      1

#ifdef AUDIO_1DMA
#undef AUDIO_8BIT
#endif

#ifdef AUDIO_8BIT
typedef char  audio_sample;
#else
typedef short  audio_sample;
#endif

#endif

#endif
