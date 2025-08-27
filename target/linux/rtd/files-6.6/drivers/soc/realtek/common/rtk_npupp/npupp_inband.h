#ifndef __NPUPP_INBAND_H__
#define __NPUPP_INBAND_H__

/** inband cmd type. I use prefix "VIDEO_DEC_" to label the cmd used in video decoder. */
typedef enum
{
  INBAND_CMD_TYPE_PTS = 0,
  INBAND_CMD_TYPE_PTS_SKIP,
  INBAND_CMD_TYPE_NEW_SEG,
  INBAND_CMD_TYPE_SEQ_END,
  INBAND_CMD_TYPE_EOS,
  INBAND_CMD_TYPE_CONTEXT,
  INBAND_CMD_TYPE_DECODE,

  /* Video Decoder In-band Command */
  VIDEO_DEC_INBAND_CMD_TYPE_VOBU,
  VIDEO_DEC_INBAND_CMD_TYPE_DVDVR_DCI_CCI,
  VIDEO_DEC_INBAND_CMD_TYPE_DVDV_VATR,

  /* MSG Type for parse mode */
  VIDEO_DEC_INBAND_CMD_TYPE_SEG_INFO,
  VIDEO_DEC_INBAND_CMD_TYPE_PIC_INFO,

  /* Sub-picture Decoder In-band Command */
  VIDEO_SUBP_INBAND_CMD_TYPE_SET_PALETTE,
  VIDEO_SUBP_INBAND_CMD_TYPE_SET_HIGHLIGHT,

  /* Video Mixer In-band Command */
  VIDEO_MIXER_INBAND_CMD_TYPE_SET_BG_COLOR,
  VIDEO_MIXER_INBAND_CMD_TYPE_SET_MIXER_RPTS,
  VIDEO_MIXER_INBAND_CMD_TYPE_BLEND,

  /* Video Scaler In-band Command */
  VIDEO_SCALER_INBAND_CMD_TYPE_OUTPUT_FMT,

  /*Dv3 resolution In-band Command*/
  VIDEO_DV3_INBAND_CMD_TYPE_RESOLUTION,

  /*MPEG4 detected In-band command*/
  VIDEO_MPEG4_INBAND_CMD_TYPE_MP4,
  /* Audio In-band Commands Start Here */

  /* DV In-band Commands */
  VIDEO_DV_INBAND_CMD_TYPE_VAUX,
  VIDEO_DV_INBAND_CMD_TYPE_FF,                //fast forward

  /* Transport Demux In-band command */
  VIDEO_TRANSPORT_DEMUX_INBAND_CMD_TYPE_PID,
  VIDEO_TRANSPORT_DEMUX_INBAND_CMD_TYPE_PTS_OFFSET,
  VIDEO_TRANSPORT_DEMUX_INBAND_CMD_TYPE_PACKET_SIZE,

  /* Real Video In-band command */
  VIDEO_RV_INBAND_CMD_TYPE_FRAME_INFO,
  VIDEO_RV_INBAND_CMD_TYPE_FORMAT_INFO,
  VIDEO_RV_INBAND_CMD_TYPE_SEGMENT_INFO,

  /*VC1 video In-band command*/
  VIDEO_VC1_INBAND_CMD_TYPE_SEQ_INFO,

  /* general video properties */
  VIDEO_INBAND_CMD_TYPE_VIDEO_USABILITY_INFO,
  VIDEO_INBAND_CMD_TYPE_VIDEO_MPEG4_USABILITY_INFO,

  /*MJPEG resolution In-band Command*/
  VIDEO_MJPEG_INBAND_CMD_TYPE_RESOLUTION,

  /* picture object for graphic */
  VIDEO_GRAPHIC_INBAND_CMD_TYPE_PICTURE_OBJECT,
  VIDEO_GRAPHIC_INBAND_CMD_TYPE_DISPLAY_INFO,

  /* subtitle offset sequence id for 3D video */
  VIDEO_DEC_INBAND_CMD_TYPE_SUBP_OFFSET_SEQUENCE_ID,

  VIDEO_H264_INBAND_CMD_TYPE_DPBBYPASS,

  /* Clear back frame to black color and send it to VO */
  VIDEO_FJPEG_INBAND_CMD_TYPE_CLEAR_SCREEN,

  /* each picture info of MJPEG */
  VIDEO_FJPEG_INBAND_CMD_TYPE_PIC_INFO,

  /*FJPEG resolution In-band Command*/
  VIDEO_FJPEG_INBAND_CMD_TYPE_RESOLUTION,

  /*VO receive VP_OBJ_PICTURE_TYPE In-band Command*/
  VIDEO_VO_INBAND_CMD_TYPE_OBJ_PIC,
  VIDEO_VO_INBAND_CMD_TYPE_OBJ_DVD_SP,
  VIDEO_VO_INBAND_CMD_TYPE_OBJ_DVB_SP,
  VIDEO_VO_INBAND_CMD_TYPE_OBJ_BD_SP,
  VIDEO_VO_INBAND_CMD_TYPE_OBJ_SP_FLUSH,
  VIDEO_VO_INBAND_CMD_TYPE_OBJ_SP_RESOLUTION,

  /* VO receive writeback buffers In-band Command */
  VIDEO_VO_INBAND_CMD_TYPE_WRITEBACK_BUFFER,

  /* for VO debug, VO can dump picture */
  VIDEO_VO_INBAND_CMD_TYPE_DUMP_PIC,
  VIDEO_CURSOR_INBAND_CMD_TYPE_PICTURE_OBJECT,
  VIDEO_CURSOR_INBAND_CMD_TYPE_COORDINATE_OBJECT,
  VIDEO_TRANSCODE_INBAND_CMD_TYPE_PICTURE_OBJECT,
  VIDEO_WRITEBACK_INBAND_CMD_TYPE_PICTURE_OBJECT,

  VIDEO_VO_INBAND_CMD_TYPE_OBJ_BD_SCALE_RGB_SP,

// TV code
  VIDEO_INBAND_CMD_TYPE_DV_CERTIFY,

  /*M_DOMAIN resolution In-band Command*/
  VIDEO_INBAND_CMD_TYPE_M_DOMAIN_RESOLUTION,

  /* DTV source In-band Command */
  VIDEO_INBAND_CMD_TYPE_SOURCE_DTV,

  /* Din source copy mode In-band Command */
  VIDEO_DIN_INBAND_CMD_TYPE_COPY_MODE,

  /* Video Decoder AU In-band command */
  VIDEO_DEC_INBAND_CMD_TYPE_AU,

  /* Video Decoder parse frame In-band command */
  VIDEO_DEC_INBAND_CMD_TYPE_PARSE_FRAME_IN,
  VIDEO_DEC_INBAND_CMD_TYPE_PARSE_FRAME_OUT,

  /* Set video decode mode In-band command */
  VIDEO_DEC_INBAND_CMD_TYPE_NEW_DECODE_MODE,

  /* Secure buffer protection */
  VIDEO_INBAND_CMD_TYPE_SECURE_PROTECTION,

  /* Dolby HDR inband command */
  VIDEO_DEC_INBAND_CMD_TYPE_DV_PROFILE,

  /* VP9 HDR10 In-band command */
  VIDEO_VP9_INBAND_CMD_TYPE_HDR10_METADATA,

  /* AV1 HDR10 In-band command */
  VIDEO_AV1_INBAND_CMD_TYPE_HDR10_METADATA,

  /* DvdPlayer tell RVSD video BS ring buffer is full */
  VIDEO_DEC_INBAND_CMD_TYPE_BS_RINGBUF_FULL,

  /* Frame Boundary In-band command */
  VIDEO_INBAND_CMD_TYPE_FRAME_BOUNDARY = 100,

    /* VO receive npp writeback buffers In-band Command */
  VIDEO_NPP_INBAND_CMD_TYPE_WRITEBACK_BUFFER,
  VIDEO_NPP_OUT_INBAND_CMD_TYPE_OBJ_PIC,
} INBAND_CMD_TYPE ;

typedef struct
{
  INBAND_CMD_TYPE type ;
  unsigned int size ;
} INBAND_CMD_PKT_HEADER ;

typedef struct {
  INBAND_CMD_PKT_HEADER header ;
  unsigned int bufferNum;
  unsigned int bufferId;
  unsigned int bufferSize;
  unsigned int addrR;
  unsigned int addrG;
  unsigned int addrB; 
  unsigned int pitch;
  unsigned int version;//version='rtk0'
  unsigned int pLock;
  unsigned int pReceived;
  unsigned int targetFormat;
  //see enum wb_targetFormat...
  //bit 0=>yuv, 0:rgb, 1:yuv;
  //bit 1=>tr, 0:rounding down, 1:rounding half up
  //bit 2=>fmt, 0:3-plane RGB, 1: singleplaneRGB;
  unsigned int width;
  unsigned int height;

  //for crop, if needn't to crop please fill 0 
  unsigned int partialSrcWin_x; 
  unsigned int partialSrcWin_y;
  unsigned int partialSrcWin_w;
  unsigned int partialSrcWin_h;

  unsigned int reserved[2];
} VIDEO_VO_NPP_WB_PICTURE_OBJECT;

typedef struct
{
  INBAND_CMD_PKT_HEADER header ;
  unsigned int version;
  unsigned int npp_yuv_mode;
  unsigned int npp_tr_mode;
  unsigned int npp_fmt;
  unsigned int R_addr;    
  unsigned int G_addr;    
  unsigned int B_addr;    
  unsigned int pLock;  
  unsigned int width;
  unsigned int height;
  unsigned int pitch;   
  unsigned int RPTSH;
  unsigned int RPTSL;  
  unsigned int PTSH;
  unsigned int PTSL;
} NPP_PICTURE_OBJECT_TRANSCODE ;

typedef enum
{
    INTERLEAVED_TOP_FIELD = 0,  /* top    field data stored in even lines of a frame buffer */
    INTERLEAVED_BOT_FIELD,      /* bottom field data stored in odd  lines of a frame buffer */
    CONSECUTIVE_TOP_FIELD,      /* top    field data stored consecutlively in all lines of a field buffer */
    CONSECUTIVE_BOT_FIELD,      /* bottom field data stored consecutlively in all lines of a field buffer */
    CONSECUTIVE_FRAME,           /* progressive frame data stored consecutlively in all lines of a frame buffer */
    INTERLEAVED_TOP_FIELD_422,  /* top    field data stored in even lines of a frame buffer */
    INTERLEAVED_BOT_FIELD_422,      /* bottom field data stored in odd  lines of a frame buffer */
    CONSECUTIVE_TOP_FIELD_422,      /* top    field data stored consecutlively in all lines of a field buffer */
    CONSECUTIVE_BOT_FIELD_422,      /* bottom field data stored consecutlively in all lines of a field buffer */
    CONSECUTIVE_FRAME_422,        /* progressive frame with 4:2:2 chroma */
    TOP_BOTTOM_FRAME,            /* top field in the 0~height/2-1, bottom field in the height/2~height-1 in the frame */
    INTERLEAVED_TOP_BOT_FIELD,   /* one frame buffer contains one top and one bot field, top field first */
    INTERLEAVED_BOT_TOP_FIELD,   /* one frame buffer contains one bot and one top field, bot field first */

    MPEG2_PIC_MODE_NOT_PROG      /*yllin: for MPEG2 check pic mode usage */

} VP_PICTURE_MODE_t ;

struct tch_metadata_variables {
	int tmInputSignalBlackLevelOffset;
	int tmInputSignalWhiteLevelOffset;
	int shadowGain;
	int highlightGain;
	int midToneWidthAdjFactor;
	int tmOutputFineTuningNumVal;
	int tmOutputFineTuningX[15];
	int tmOutputFineTuningY[15];
	int saturationGainNumVal;
	int saturationGainX[15];
	int saturationGainY[15];
};

struct tch_metadata_tables {
	int luminanceMappingNumVal;
	int luminanceMappingX[33];
	int luminanceMappingY[33];
	int colourCorrectionNumVal;
	int colourCorrectionX[33];
	int colourCorrectionY[33];
	int chromaToLumaInjectionMuA;
	int chromaToLumaInjectionMuB;
};

struct tch_metadata {
	int specVersion;
	int payloadMode;
	int hdrPicColourSpace;
	int hdrMasterDisplayColourSpace;
	int hdrMasterDisplayMaxLuminance;
	int hdrMasterDisplayMinLuminance;
	int sdrPicColourSpace;
	int sdrMasterDisplayColourSpace;
	union {
		struct tch_metadata_variables variables;
		struct tch_metadata_tables tables;
	} u;
};

typedef struct {
    INBAND_CMD_PKT_HEADER header ;
    unsigned int version;

    unsigned int mode;
    unsigned int Y_addr;
    unsigned int U_addr;
    unsigned int pLock;
    unsigned int width;
    unsigned int height;
    unsigned int Y_pitch;
    unsigned int C_pitch;

    unsigned int RPTSH;
    unsigned int RPTSL;
    unsigned int PTSH;
    unsigned int PTSL;

    /* for send two interlaced fields in the same packet,
    valid only when mode is INTERLEAVED_TOP_BOT_FIELD or INTERLEAVED_BOT_TOP_FIELD*/
    unsigned int RPTSH2;
    unsigned int RPTSL2;
    unsigned int PTSH2;
    unsigned int PTSL2;

    unsigned int context;
    unsigned int pRefClock;  /* not used now */

    unsigned int pixelAR_hor; /* pixel aspect ratio hor, not used now */
    unsigned int pixelAR_ver; /* pixel aspect ratio ver, not used now */

    unsigned int Y_addr_Right; /* for mvc */
    unsigned int U_addr_Right; /* for mvc */
    unsigned int pLock_Right; /* for mvc */
    unsigned int mvc;         /* 1: mvc */
    unsigned int subPicOffset;/* 3D Blu-ray dependent-view sub-picture plane offset metadata as defined in BD spec sec. 9.3.3.6.
                                       Valid only when Y_BufId_Right and C_BufId_Right are both valid */
    unsigned int pReceived;         // fix bug 44329 by version 0x72746B30 'rtk0'
    unsigned int pReceived_Right;   // fix bug 44329 by version 0x72746B30 'rtk0'

    unsigned int fps;   // 'rtk1'
    unsigned int IsForceDIBobMode; // force vo use bob mode to do deinterlace, 'rtk2'.
    unsigned int lumaOffTblAddr;    // 'rtk3'
    unsigned int chromaOffTblAddr;  // 'rtk3'
    unsigned int lumaOffTblAddrR; /* for mvc, 'rtk3' */
    unsigned int chromaOffTblAddrR; /* for mvc, 'rtk3' */

    unsigned int bufBitDepth;   // 'rtk3'
    unsigned int bufFormat;     // 'rtk3', according to VO spec: 10bits Pixel Packing mode selection, "0": use 2 bytes to store 1 components. MSB justified. "1": use 4 bytes to store 3 components. LSB justified. 

    // VUI (Video Usability Information)
    unsigned int transferCharacteristics;   // 0:SDR, 1:HDR, 2:ST2084, 'rtk3'

    // Mastering display colour volume SEI, 'rtk3'
    unsigned int display_primaries_x0;
    unsigned int display_primaries_y0;
    unsigned int display_primaries_x1;
    unsigned int display_primaries_y1;
    unsigned int display_primaries_x2;
    unsigned int display_primaries_y2;
    unsigned int white_point_x;
    unsigned int white_point_y;
    unsigned int max_display_mastering_luminance;
    unsigned int min_display_mastering_luminance;

    /*for transcode interlaced feild use.*/ //'rtk4'
    unsigned int Y_addr_prev;   //'rtk4'
    unsigned int U_addr_prev;   //'rtk4'
    unsigned int Y_addr_next;   //'rtk4'
    unsigned int U_addr_next;   //'rtk4'
    unsigned int video_full_range_flag; //'rtk4'  default= 1
    unsigned int matrix_coefficients;   //'rtk4   default= 1

    /*for transcode interlaced feild use.*/  //'rtk5'
    unsigned int pLock_prev;
    unsigned int pReceived_prev;
    unsigned int pLock_next;
    unsigned int pReceived_next;

    unsigned int is_tch_video; // rtk-6
    struct tch_metadata tch_hdr_metadata;  //'rtk6'

    unsigned int pFrameBufferDbg;  //rtk7
    unsigned int pFrameBufferDbg_Right;
    unsigned int Y_addr_EL; //'rtk8' for dolby vision
    unsigned int U_addr_EL;
    unsigned int width_EL;
    unsigned int height_EL;
    unsigned int Y_pitch_EL;
    unsigned int C_pitch_EL;
    unsigned int lumaOffTblAddr_EL;
    unsigned int chromaOffTblAddr_EL;

    unsigned int dm_reg1_addr;
    unsigned int dm_reg1_size;
    unsigned int dm_reg2_addr;
    unsigned int dm_reg2_size;
    unsigned int dm_reg3_addr;
    unsigned int dm_reg3_size;
    unsigned int dv_lut1_addr;
    unsigned int dv_lut1_size;
    unsigned int dv_lut2_addr;
    unsigned int dv_lut2_size;

    unsigned int slice_height;     //'rtk8'

    unsigned int hdr_metadata_addr;//'rtk9'
    unsigned int hdr_metadata_size;//'rtk9'
    unsigned int tch_metadata_addr;//'rtk9'
    unsigned int tch_metadata_size;//'rtk9'
    unsigned int is_dolby_video;//'rtk10'

    unsigned int lumaOffTblSize;    // 'rtk11'
    unsigned int chromaOffTblSize;  // 'rtk11'
    // 'rtk12'
    unsigned int Combine_Y_Addr;
    unsigned int Combine_U_Addr;
    unsigned int Combine_Width;
    unsigned int Combine_Height;
    unsigned int Combine_Y_Pitch;
    unsigned int secure_flag;

    // 'rtk13'
    unsigned int tvve_picture_width;//rtk 13 TVVE   for vo calculate header pitch
    unsigned int tvve_lossy_en;
    unsigned int tvve_bypass_en;
    unsigned int tvve_qlevel_sel_y;
    unsigned int tvve_qlevel_sel_c;
    unsigned int is_ve_tile_mode;  //rtk tile mode
    unsigned int film_grain_metadat_addr;
    unsigned int film_grain_metadat_size;

    // 'rtk14'
    unsigned int partialSrcWin_x; //rtk14 0x72746B3E
    unsigned int partialSrcWin_y;
    unsigned int partialSrcWin_w;
    unsigned int partialSrcWin_h;

    // 'rtk15'
//    unsigned int dolby_out_hdr_metadata_addr; //rtk 15 0x72746B3F
//    unsigned int dolby_out_hdr_metadata_size;
} VIDEO_VO_PICTURE_OBJECT_NEW ;

#endif
