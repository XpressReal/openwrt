// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 RealTek Inc.
 */

#include <drm/drm_edid.h>
#include "rtk_hdmi.h"

/* Tag Code */
#define AUDIO_BLOCK	0x01
#define VIDEO_BLOCK     0x02
#define VENDOR_BLOCK    0x03
#define SPEAKER_BLOCK	0x04
#define VESA_DISPLAY_TRANSFER_BLOCK	0x05
#define USE_EXTENDED_TAG	0x07

/* Extended Tag Codes */
#define VIDEO_CAPABILITY_DATA_BLOCK			0x00
#define VENDOR_SPECIFIC_VIDEO_DATA_BLOCK	0x01
#define VESA_DISPLAY_DEVICE_DATA_BLOCK		0x02
#define VESA_VIDEO_TIMING_BLOCK_EXTENSION	0x03
#define COLORIMETRY_DATA_BLOCK				0x05
#define HDR_STATIC_METADATA_DATA_BLOCK		0x06
#define HDR_DYNAMIC_METADATA_DATA_BLOCK		0x07
#define VIDEO_FORMAT_PERFERENCE_DATA_BLOCK	0x0D
#define YCBCR420_VIDEO_DATA_BLOCK			0x0E
#define YCBCR420_CAPABILITY_MAP_DATA_BLOCK	0x0F
#define VENDOR_SPECIFIC_AUDIO_DATA_BLOCK	0x11
#define ROOM_CONFIGURATION_DATA_BLOCK		0x13
#define SPEAKER_LOCATION_DATA_BLOCK			0x14
#define INFOFRAME_DATA_BLOCK				0x20
#define HF_EXTENSION_OVERRIDE_DATA_BLOCK    0x78
#define HF_SINK_CAPABILITY_DATA_BLOCK       0x79

static const struct rtk_edid_quirk {
	char vendor[4];
	int product_id;
	u32 quirks;
} rtk_edid_quirk_list[] = {
	/* BenQ GW2480-T */
	{ "BNQ", 30951, RTK_EDID_QUIRK_HDCP_DELAY_2S },
	/* BenQ GL2450 */
	{ "BNQ", 30887, RTK_EDID_QUIRK_HDCP_DELAY_2S },
};

static struct drm_display_mode rtk_4k_mode[] = {
	/* 96 - 3840x2160@50Hz 16:9 */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 594000, 3840, 4896,
		   4984, 5280, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 97 - 3840x2160@60Hz 16:9 */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 594000, 3840, 4016,
		   4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 97 - 3840x2160@59.94Hz 16:9 */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 593406, 3840, 4016,
		   4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 101 - 4096x2160@50Hz 256:135 */
	{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 594000, 4096, 5064,
		   5152, 5280, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_256_135, },
	/* 102 - 4096x2160@60Hz 256:135 */
	{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 594000, 4096, 4184,
		   4272, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_256_135, },
	/* 102 - 4096x2160@59.94Hz 256:135 */
	{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 593406, 4096, 4184,
		   4272, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_256_135, },
};

static int cea_db_payload_len(const u8 *db)
{
	return db[0] & 0x1f;
}

static int cea_db_tag(const u8 *db)
{
	return db[0] >> 5;
}

static int cea_db_offsets(const u8 *cea, int *start, int *end)
{
	/* Data block offset in CEA extension block */
	*start = 4;
	*end = cea[2];
	if (*end == 0)
		*end = 127;
	if (*end < 4 || *end > 127)
		return -ERANGE;
	return 0;
}

#define for_each_cea_db(cea, i, start, end) \
	for ((i) = (start); (i) < (end) && (i) + cea_db_payload_len(&(cea)[(i)]) < (end); \
				(i) += cea_db_payload_len(&(cea)[(i)]) + 1)

static bool rtk_edid_checksum_valid(u8 *edid_block)
{
	int i;
	u8 csum = 0;

	if (edid_block == NULL)
		return false;

	for (i = 0; i < EDID_LENGTH; i++)
		csum += edid_block[i];

	if (csum)
		return false;

	return true;
}

static void rtk_correct_edid_checksum(u8 *edid_block)
{
	int i;
	u8 csum = 0, crc = 0;

	if (edid_block == NULL)
		return;

	for (i = 0; i < EDID_LENGTH - 1; i++)
		csum += edid_block[i];

	crc = 0x100 - csum;
	edid_block[EDID_LENGTH - 1] = crc;
}

static void rtk_erase_edid_db(u8 *edid_block, u8 offset, u8 size)
{
	int i;

	if (edid_block == NULL)
		return;

	if ((offset + size) >= EDID_LENGTH)
		return;

	for (i = offset; (i + size) < (EDID_LENGTH - 1); i++)
		edid_block[i] = edid_block[i+size];

	for (i = (EDID_LENGTH - size - 1); i < EDID_LENGTH - 1; i++)
		edid_block[i] = 0;

	/* Modify descriptor offset */
	edid_block[2] = edid_block[2] - size;

	rtk_correct_edid_checksum(edid_block);
}

/**
 * parse_hdmi_colorimetry_db - parse colorimetry data block
 * @hdmi: struct rtk_hdmi
 * @db: colorimetry data block
 *
 * bit         7                   6                    5                4                   3                2             1             0
 *     BT2020_RGB/BT2020_YCC/BT2020_cYCC/AdobeRGB/AdobeYCC601/sYCC601/xvYCC709/xvYCC601
 */
static void parse_hdmi_colorimetry_db(struct rtk_hdmi *hdmi, const u8 *db)
{
	hdmi->edid_info.colorimetry = db[2];
}

/**
 * parse_hdmi_VideoCapability_db - parse video capability data block
 * @hdmi: struct rtk_hdmi
 * @db: video capability data block
 *
 * bit   7   6      5         4        3         2         1          0
 *      QY/QS/S_PT1/S_PT0/S_IT1/S_IT0/S_CE1/S_CE0
 */
static void
parse_hdmi_video_capability_db(struct rtk_hdmi *hdmi, const u8 *db)
{
	hdmi->edid_info.vcdb = db[2];
}

/**
 * parse_hf_eeodb - parse HDMI Forum EDID Extension Override Data Block
 *                            (HF-EEODB)
 * @hdmi: struct rtk_hdmit
 * @db: Data block, should be HF-EEODB
 */
static void
parse_hf_eeodb(struct rtk_hdmi *hdmi, const u8 *db)
{
	unsigned char len;

	len = cea_db_payload_len(db);

	if (len < 2)
		return;

	hdmi->edid_info.eeodb.override_count = db[2];
}

/**
 * parse_hdmi_scds - parse Sink Capability  Data Structure(SCDS)
 *   The minimun length of the SCDS is 4, and the maximum length is 28
 *   SCDS might be contained in HF-VSDB or HF-SCDB
 * @hdmi: struct rtk_hdmit
 * @db: Data block, should be HF-VSDB or HF-SCDB
 */
static void parse_hdmi_scds(struct rtk_hdmi *hdmi, const u8 *db)
{
	unsigned char len;

	len = cea_db_payload_len(db);

	if (len < 7)
		return;

	hdmi->edid_info.max_tmds_char_rate = db[5]*5;
	hdmi->edid_info.scdc_capable = db[6];
	hdmi->edid_info.dc_420 = db[7]&0x7;
	hdmi->edid_info.max_frl_rate = (db[7]>>4)&0xF;

	if (len >= 8)
		hdmi->edid_info.scds_pb5 = db[8];

	if (len >= 9)
		hdmi->edid_info.vrr_min = db[9]&0x3F;

	if (len >= 10)
		hdmi->edid_info.vrr_max = ((db[9]&0xC0)<<2) | db[10];

	if (len >= 11)
		hdmi->edid_info.scds_pb8 = db[11];

}

static void rtk_parse_hdmi_extdb(struct rtk_hdmi *hdmi, const u8 *db)
{
	int dbl;

	if (cea_db_tag(db) != USE_EXTENDED_TAG)
		return;

	dbl = cea_db_payload_len(db);

	switch (*(db+1)) {
	case VIDEO_CAPABILITY_DATA_BLOCK:
		dev_dbg(hdmi->dev, "VIDEO_CAPABILITY_DATA_BLOCK (%u bytes)", dbl);
		parse_hdmi_video_capability_db(hdmi, db);
		break;
	case VENDOR_SPECIFIC_VIDEO_DATA_BLOCK:
		dev_dbg(hdmi->dev,
			"VENDOR_SPECIFIC_VIDEO_DATA_BLOCK (%u bytes)", dbl);
		break;
	case VESA_DISPLAY_DEVICE_DATA_BLOCK:
		dev_dbg(hdmi->dev, "VESA_DISPLAY_DEVICE_DATA_BLOCK (%u bytes)", dbl);
		break;
	case VESA_VIDEO_TIMING_BLOCK_EXTENSION:
		dev_dbg(hdmi->dev,
			"VESA_VIDEO_TIMING_BLOCK_EXTENSION (%u bytes)", dbl);
		break;
	case COLORIMETRY_DATA_BLOCK:
		dev_dbg(hdmi->dev, "COLORIMETRY_DATA_BLOCK (%u bytes)", dbl);
		parse_hdmi_colorimetry_db(hdmi, db);
		break;
	case HDR_STATIC_METADATA_DATA_BLOCK:
		dev_dbg(hdmi->dev, "HDR_STATIC_METADATA_DATA_BLOCK (%u bytes)", dbl);
		break;
	case YCBCR420_VIDEO_DATA_BLOCK:
		dev_dbg(hdmi->dev, "YCBCR420_VIDEO_DATA_BLOCK (%u bytes)", dbl);
		break;
	case YCBCR420_CAPABILITY_MAP_DATA_BLOCK:
		dev_dbg(hdmi->dev,
			"YCBCR420_CAPABILITY_MAP_DATA_BLOCK (%u bytes)", dbl);
		break;
	case VENDOR_SPECIFIC_AUDIO_DATA_BLOCK:
		dev_dbg(hdmi->dev, "VENDOR_SPECIFIC_AUDIO_DATA_BLOCK (%u bytes)", dbl);
		break;
	case INFOFRAME_DATA_BLOCK:
		dev_dbg(hdmi->dev, "INFOFRAME_DATA_BLOCK (%u bytes)", dbl);
		break;
	case HF_EXTENSION_OVERRIDE_DATA_BLOCK:
		dev_dbg(hdmi->dev, "HF_EXTENSION_OVERRIDE_DATA_BLOCK (%u bytes)", dbl);
		parse_hf_eeodb(hdmi, db);
		break;
	case HF_SINK_CAPABILITY_DATA_BLOCK:
		dev_dbg(hdmi->dev, "HF_SINK_CAPABILITY_DATA_BLOCK (%u bytes)", dbl);
		parse_hdmi_scds(hdmi, db);
		break;
	default:
		dev_dbg(hdmi->dev, "Unknow Extend Tag(%u) (%u bytes)", *(db+1), dbl);
		break;
	} /* end of switch (*(db+1)) */

}

static void rtk_process_eeodb_edid(struct rtk_hdmi *hdmi)
{
	u8 *new_edid;
	u8 *buf;
	u8 original_ext;
	u8 override_ext;
	u8 index;
	int ret;
	bool valid;

	if ((hdmi->edid_cache == NULL) ||
		(!hdmi->edid_info.eeodb.override_count) ||
		(hdmi->edid_info.eeodb.offset_in_blk < 4) ||
		(hdmi->edid_info.eeodb.offset_in_blk > 124))
		return;

	dev_info(hdmi->dev, "eeodb override_count=%u blk=%u offset=%u size=%u",
		hdmi->edid_info.eeodb.override_count,
		hdmi->edid_info.eeodb.blk_index,
		hdmi->edid_info.eeodb.offset_in_blk,
		hdmi->edid_info.eeodb.size);

	original_ext = hdmi->edid_cache->extensions;
	override_ext = hdmi->edid_info.eeodb.override_count;

	if (override_ext <= original_ext) {
		dev_info(hdmi->dev, "eeodb override_ext=%u original_ext=%u, skip",
				override_ext, original_ext);
		return;
	}

	new_edid = krealloc((u8 *)hdmi->edid_cache, (override_ext + 1) * EDID_LENGTH, GFP_KERNEL);
	if (IS_ERR_OR_NULL(new_edid)) {
		dev_err(hdmi->dev, "%s fail, no memory", __func__);
		return;
	}
	hdmi->edid_cache = (struct edid *)new_edid;

	for (index = original_ext + 1; index <= override_ext; index++) {
		buf = (u8 *)new_edid + index * EDID_LENGTH;
		ret = hdmi->hdmi_ops->read_edid_block(hdmi, buf, index);
		if (ret) {
			dev_err(hdmi->dev, "%s fail, ret=%d", __func__, ret);
			return;
		}

		if (buf[0] != CEA_EXT) {
			dev_err(hdmi->dev, "block%u header(0x%02x) is invalid", index, buf[0]);
			return;
		}

		valid = rtk_edid_checksum_valid(buf);
		if (!valid) {
			dev_err(hdmi->dev, "block%u checksum is invalid", index);
			return;
		}
	}

	/* Erase EEODB */
	index = hdmi->edid_info.eeodb.blk_index;
	buf = (u8 *)new_edid + index * EDID_LENGTH;
	rtk_erase_edid_db(buf,
		hdmi->edid_info.eeodb.offset_in_blk,
		hdmi->edid_info.eeodb.size);

	/* Overwrite extensions count of base block */
	new_edid[EDID_LENGTH - 2] = override_ext;
	rtk_correct_edid_checksum(new_edid);

}

static int
rtk_do_cea_modes(struct drm_connector *connector, const u8 *db, u8 len)
{
	u8 vic;
	int i;
	int modes = 0;
	struct drm_display_mode *display_mode;

	for (i = 0; i < len; i++) {
		vic = db[i] & 0x7f;

		switch (vic) {
		case 96:
			display_mode = drm_mode_duplicate(connector->dev, &rtk_4k_mode[0]);
			drm_mode_probed_add(connector, display_mode);
			modes++;
			break;
		case 97:
			display_mode = drm_mode_duplicate(connector->dev, &rtk_4k_mode[1]);
			drm_mode_probed_add(connector, display_mode);
			modes++;
			display_mode = drm_mode_duplicate(connector->dev, &rtk_4k_mode[2]);
			drm_mode_probed_add(connector, display_mode);
			modes++;
			break;
		case 101:
			display_mode = drm_mode_duplicate(connector->dev, &rtk_4k_mode[3]);
			drm_mode_probed_add(connector, display_mode);
			modes++;
			break;
		case 102:
			display_mode = drm_mode_duplicate(connector->dev, &rtk_4k_mode[4]);
			drm_mode_probed_add(connector, display_mode);
			modes++;
			display_mode = drm_mode_duplicate(connector->dev, &rtk_4k_mode[5]);
			drm_mode_probed_add(connector, display_mode);
			modes++;
			break;
		default:
			break;
		}

	}

	return modes;
}

u32 rtk_edid_get_quirks(struct rtk_hdmi *hdmi, const struct edid *edid)
{
	const struct rtk_edid_quirk *quirk;
	char edid_vendor[3];
	int i;

	edid_vendor[0] = ((edid->mfg_id[0] & 0x7c) >> 2) + '@';
	edid_vendor[1] = (((edid->mfg_id[0] & 0x3) << 3) |
			  ((edid->mfg_id[1] & 0xe0) >> 5)) + '@';
	edid_vendor[2] = (edid->mfg_id[1] & 0x1f) + '@';

	for (i = 0; i < ARRAY_SIZE(rtk_edid_quirk_list); i++) {
		quirk = &rtk_edid_quirk_list[i];

		if (!strncmp(edid_vendor, quirk->vendor, 3) &&
		    (EDID_PRODUCT_ID(edid) == quirk->product_id)) {
		    dev_info(hdmi->dev, "edid quirks=0x%08x", quirk->quirks);
			return quirk->quirks;
		}
	}

	return 0;
}

void rtk_parse_cea_ext(struct rtk_hdmi *hdmi, struct edid *edid)
{
	bool valid;
	int start;
	int end;
	int i;
	int j;
	u8 *block;
	u8 *db;
	u8 dbl;
	unsigned int oui;

	valid = drm_edid_is_valid(edid);
	if (!valid) {
		dev_err(hdmi->dev, "edid is invalid");
		return;
	}

	if (edid->extensions == 0)
		return;

	for (i = 1; i <= edid->extensions; i++) {
		block = (u8 *)edid + i*EDID_LENGTH;

		if (cea_db_offsets(block, &start, &end))
			return;

		for_each_cea_db(block, j, start, end) {
			db = &block[j];
			dbl = cea_db_payload_len(db);

			switch (cea_db_tag(db)) {
			case AUDIO_BLOCK:
				break;
			case VIDEO_BLOCK:
				break;
			case SPEAKER_BLOCK:
				break;
			case VENDOR_BLOCK:
				if (dbl < 7)
					break;

				oui = db[3] << 16 | db[2] << 8 | db[1];
				if (oui == HDMI_FORUM_IEEE_OUI)
					parse_hdmi_scds(hdmi, db);
				break;
			case VESA_DISPLAY_TRANSFER_BLOCK:
				break;
			case USE_EXTENDED_TAG:
				rtk_parse_hdmi_extdb(hdmi, db);

				if (hdmi->edid_info.eeodb.override_count &&
					hdmi->edid_info.eeodb.offset_in_blk == 0) {
					hdmi->edid_info.eeodb.blk_index = i;
					hdmi->edid_info.eeodb.offset_in_blk = j;
					hdmi->edid_info.eeodb.size = cea_db_payload_len(db) + 1;
				}
				break;
			default:
				break;
			}
		}
	}

	rtk_process_eeodb_edid(hdmi);
}

int rtk_add_more_ext_modes(struct drm_connector *connector, struct edid *edid)
{
	int modes = 0;
	int start;
	int end;
	int i;
	int j;
	u8 *block;
	u8 *db;
	u8 dbl;

	if (edid->extensions < 2)
		return modes;

	for (i = 2; i <= edid->extensions; i++) {
		block = (u8 *)edid + i*EDID_LENGTH;

		if (cea_db_offsets(block, &start, &end))
			return modes;

		for_each_cea_db(block, j, start, end) {
			db = &block[j];
			dbl = cea_db_payload_len(db);

			if (cea_db_tag(db) == VIDEO_BLOCK)
				modes += rtk_do_cea_modes(connector, db+1, dbl);

		}
	}

	return modes;
}
