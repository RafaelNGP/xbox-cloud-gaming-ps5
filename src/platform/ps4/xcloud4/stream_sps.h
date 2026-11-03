/* SPDX-License-Identifier: GPL-3.0-only */
/* From XCloud4 (https://github.com/asp11edgr/XCloud4, src/video/stream_sps.h at 9aab6b1),
 * reused with its author's agreement: https://github.com/asp11edgr/XCloud4/issues/8 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    unsigned profile_idc, constraints, level_idc;
    unsigned coded_width, coded_height, visible_width, visible_height;
    unsigned crop_left, crop_top, crop_right, crop_bottom;
    unsigned chroma_format_idc, bit_depth_luma, bit_depth_chroma;
    unsigned max_num_ref_frames, max_dec_frame_buffering, num_reorder_frames;
    unsigned matrix_coefficients, full_range;
    unsigned seq_parameter_set_id, log2_max_frame_num_minus4;
    unsigned pic_order_cnt_type, log2_max_pic_order_cnt_lsb_minus4;
    unsigned pic_width_in_mbs_minus1, pic_height_in_map_units_minus1;
    unsigned separate_colour_plane_flag, frame_mbs_only_flag;
    unsigned mb_adaptive_frame_field_flag, direct_8x8_inference_flag;
    unsigned vui_present, video_signal_type_present, colour_description_present;
    unsigned colour_primaries, transfer_characteristics, bitstream_restriction_present;
    /* 1 = explicit VUI, 2 = level inference, 3 = reference-derived request
     * because the signalled level is unknown. */
    unsigned dpb_source;
} X4StreamSps;
enum {
    X4_SPS_INVALID = -7401, X4_SPS_CHANGED = -7403,
    X4_SPS_PROFILE = -7410, X4_SPS_CHROMA = -7411,
    X4_SPS_BIT_DEPTH = -7412, X4_SPS_SIZE = -7413,
    X4_SPS_INTERLACE = -7414, X4_SPS_CROP = -7415,
    X4_SPS_LEVEL_DPB = -7416, X4_SPS_COLOR = -7417,
    X4_SPS_GEOMETRY = -7418, X4_SPS_TRUNCATED = -7419,
    X4_SPS_EBSP = -7420, X4_SPS_SYNTAX = -7421
};
enum {
    X4_SPS_STAGE_INPUT, X4_SPS_STAGE_PROFILE, X4_SPS_STAGE_FORMAT,
    X4_SPS_STAGE_FRAME_NUM, X4_SPS_STAGE_POC, X4_SPS_STAGE_REFS,
    X4_SPS_STAGE_SIZE, X4_SPS_STAGE_INTERLACE, X4_SPS_STAGE_CROP,
    X4_SPS_STAGE_VUI, X4_SPS_STAGE_TRAILING, X4_SPS_STAGE_POLICY,
    X4_SPS_STAGE_DONE
};
enum {
    X4_SPS_HAVE_NAL = UINT64_C(1) << 0,
    X4_SPS_HAVE_PROFILE = UINT64_C(1) << 1,
    X4_SPS_HAVE_CONSTRAINTS = UINT64_C(1) << 2,
    X4_SPS_HAVE_LEVEL = UINT64_C(1) << 3,
    X4_SPS_HAVE_ID = UINT64_C(1) << 4,
    X4_SPS_HAVE_FORMAT = UINT64_C(1) << 5,
    X4_SPS_HAVE_FRAME_NUM = UINT64_C(1) << 6,
    X4_SPS_HAVE_POC = UINT64_C(1) << 7,
    X4_SPS_HAVE_REFS = UINT64_C(1) << 8,
    X4_SPS_HAVE_SIZE = UINT64_C(1) << 9,
    X4_SPS_HAVE_FRAME_FLAGS = UINT64_C(1) << 10,
    X4_SPS_HAVE_CROP = UINT64_C(1) << 11,
    X4_SPS_HAVE_GEOMETRY = UINT64_C(1) << 12,
    X4_SPS_HAVE_COLOR = UINT64_C(1) << 13,
    X4_SPS_HAVE_VUI = UINT64_C(1) << 14,
    X4_SPS_HAVE_DPB = UINT64_C(1) << 15,
    X4_SPS_HAVE_TRAILING = UINT64_C(1) << 16,
    X4_SPS_HAVE_CHROMA = UINT64_C(1) << 17,
    X4_SPS_HAVE_DEPTH_LUMA = UINT64_C(1) << 18,
    X4_SPS_HAVE_DEPTH_CHROMA = UINT64_C(1) << 19,
    X4_SPS_HAVE_WIDTH = UINT64_C(1) << 20,
    X4_SPS_HAVE_MAP_HEIGHT = UINT64_C(1) << 21,
    X4_SPS_HAVE_FRAME_ONLY = UINT64_C(1) << 22,
    X4_SPS_HAVE_FULL_RANGE = UINT64_C(1) << 23,
    X4_SPS_HAVE_MATRIX = UINT64_C(1) << 24,
    X4_SPS_HAVE_CROP_LEFT = UINT64_C(1) << 25,
    X4_SPS_HAVE_CROP_RIGHT = UINT64_C(1) << 26,
    X4_SPS_HAVE_CROP_TOP = UINT64_C(1) << 27,
    X4_SPS_HAVE_CROP_BOTTOM = UINT64_C(1) << 28,
    X4_SPS_HAVE_POC_TYPE = UINT64_C(1) << 29,
    X4_SPS_HAVE_REORDER_COUNT = UINT64_C(1) << 30,
    X4_SPS_HAVE_DPB_COUNT = UINT64_C(1) << 31
};
enum {
    X4_SPS_WARN_UNKNOWN_PROFILE = 1u << 0,
    X4_SPS_WARN_UNKNOWN_LEVEL = 1u << 1,
    X4_SPS_WARN_LEVEL_LIMIT = 1u << 2,
    X4_SPS_WARN_DPB = 1u << 3,
    X4_SPS_WARN_COLOR = 1u << 4,
    X4_SPS_WARN_GEOMETRY = 1u << 5,
    X4_SPS_WARN_FULL_RANGE = 1u << 6,
    X4_SPS_WARN_INFERRED_DPB = 1u << 7
};
typedef struct {
    int code;
    unsigned stage, reason, reader_error;
    uint64_t valid_mask;
    unsigned warning_mask;
    size_t rbsp_bit_offset, ebsp_byte_offset;
    bool syntax_complete;
} X4StreamSpsDiagnostics;
/* Bounded incremental parser, with no allocation or automatic SPS copy.
 * Annex-B input can contain following NALs; the first SPS alone is parsed.
 * Profile, level, DPB and colour are left to native Query/Create admission. */
int x4_stream_sps_parse(const uint8_t *, size_t, X4StreamSps *);
/* Observational forms publish fields read before failure. Consult valid_mask
 * before interpreting defaults. parse leaves *out unchanged on failure. */
int x4_stream_sps_inspect(const uint8_t *, size_t, X4StreamSps *, bool *syntax_complete);
int x4_stream_sps_inspect_diagnostic(const uint8_t *, size_t, X4StreamSps *, X4StreamSpsDiagnostics *);
const char *x4_stream_sps_stage_name(unsigned);
const char *x4_stream_sps_error_name(int);
bool x4_stream_sps_compatible(const X4StreamSps *, const X4StreamSps *);
#if X4_STREAM_PROFILE
/* H.264 Annex A DPB limit for the coded macroblock geometry. An unknown
 * level is explicitly reported and bounded by the standard's 16 pictures. */
unsigned x4_stream_sps_level_dpb_capacity(unsigned level, unsigned constraints,
    unsigned coded_width, unsigned coded_height, bool *known);
#endif
/* Validate every SPS present in an AU; an AU without SPS keeps the initial
 * contract. A changed contract requires a safe session shutdown. */
int x4_stream_sps_check_au(const uint8_t *, size_t, const X4StreamSps *);
