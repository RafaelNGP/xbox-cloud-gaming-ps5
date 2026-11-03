/* SPDX-License-Identifier: GPL-3.0-only */
/* From XCloud4 (https://github.com/asp11edgr/XCloud4, src/video/stream_sps.c at 9aab6b1),
 * reused with its author's agreement: https://github.com/asp11edgr/XCloud4/issues/8 */
#include "stream_sps.h"
#include <string.h>
#include <limits.h>

/* Read the EBSP incrementally. Track EBSP bytes, RBSP bits and the first reader
 * error independently; a missing bit must never turn into a policy rejection. */
enum { READ_OK, READ_TRUNCATED, READ_EBSP, READ_OVERFLOW };
typedef struct {
    const uint8_t *p; size_t n, at, rbsp_bits;
    unsigned byte, left, zeros, error;
} Bits;
static unsigned bit(Bits *b)
{
    if(b->error)return 0;
    if(!b->left){
        if(b->at>=b->n){b->error=READ_TRUNCATED;return 0;}
        unsigned x=b->p[b->at++];
        if(b->zeros>=2){
            if(x==3){
                if(b->at>=b->n){b->error=READ_TRUNCATED;return 0;}
                if(b->p[b->at]>3){b->error=READ_EBSP;return 0;}
                x=b->p[b->at++];b->zeros=0;
            }else if(x<=2){b->error=READ_EBSP;return 0;}
        }
        b->zeros=x?0:b->zeros+1;b->byte=x;b->left=8;
    }
    ++b->rbsp_bits;return (b->byte>>--b->left)&1u;
}
static unsigned bits(Bits *b,unsigned n)
{
    if(n>32){b->error=READ_OVERFLOW;return 0;}
    unsigned x=0;for(unsigned i=0;i<n&&!b->error;++i)x=(x<<1)|bit(b);return x;
}
static unsigned ue(Bits *b)
{
    unsigned n=0;
    while(!b->error&&!bit(b))if(++n>31){b->error=READ_OVERFLOW;return 0;}
    return b->error?0:(n?((1u<<n)-1u)+bits(b,n):0);
}
static int se(Bits *b)
{
    unsigned x=ue(b);
    if(x>INT_MAX){b->error=READ_OVERFLOW;return 0;}
    return x&1u?(int)(x/2u+1u):-(int)(x/2u);
}
static void scaling(Bits *b,unsigned n)
{
    int last=8,next=8;
    for(unsigned i=0;i<n&&!b->error;++i){
        if(next){int delta=se(b);next=(int)(((int64_t)last+delta+256)%256);if(next<0)next+=256;}
        if(next)last=next;
    }
}
static bool hrd(Bits *b)
{
    unsigned count=ue(b);if(b->error||count>31)return false;
    bits(b,8);
    for(unsigned i=0;i<=count&&!b->error;++i){ue(b);ue(b);bit(b);}
    bits(b,20);return !b->error;
}
static bool high_syntax(unsigned p)
{
    switch(p){case 100:case 110:case 122:case 244:case 44:case 83:case 86:
    case 118:case 128:case 138:case 139:case 134:case 135:return true;default:return false;}
}
static unsigned level_dpb(unsigned level,unsigned constraints)
{
    if(level==11&&(constraints&0x10))return 396; /* Level 1b. */
    switch(level){case 10:return 396;case 11:return 900;case 12:case 13:case 20:return 2376;
    case 21:return 4752;case 22:case 30:return 8100;case 31:return 18000;case 32:return 20480;
    case 40:case 41:return 32768;case 42:return 34816;case 50:return 110400;
    case 51:case 52:return 184320;case 60:case 61:case 62:return 696320;default:return 0;}
}
static unsigned level_fs(unsigned level,unsigned constraints)
{
    if(level==11&&(constraints&0x10))return 99;
    switch(level){case 10:return 99;case 11:case 12:case 13:case 20:return 396;
    case 21:return 792;case 22:case 30:return 1620;case 31:return 3600;case 32:return 5120;
    case 40:case 41:return 8192;case 42:return 8704;case 50:return 22080;
    case 51:case 52:return 36864;case 60:case 61:case 62:return 139264;default:return 0;}
}
#if X4_STREAM_PROFILE
unsigned x4_stream_sps_level_dpb_capacity(unsigned level,unsigned constraints,
    unsigned width,unsigned height,bool *known)
{
    unsigned bound=level_dpb(level,constraints);
    if(known)*known=bound!=0;
    if(!width||!height)return 0;
    uint64_t macroblocks=((uint64_t)width+15)/16*(((uint64_t)height+15)/16);
    if(!macroblocks)return 0;
    if(!bound)return 16;
    unsigned pictures=(unsigned)(bound/macroblocks);
    return pictures>16?16:pictures;
}
#endif
static size_t prefix_at(const uint8_t *p,size_t n,size_t at)
{
    if(at+3<=n&&!p[at]&&!p[at+1]&&p[at+2]==1)return 3;
    if(at+4<=n&&!p[at]&&!p[at+1]&&!p[at+2]&&p[at+3]==1)return 4;
    return 0;
}
/* An Annex-B SPS ends before the next start code. Leading/trailing zero bytes
 * belong to the byte stream, not the RBSP. A raw NAL may not contain them. */
static bool nal_span(const uint8_t *p,size_t n,size_t *begin,size_t *end)
{
    size_t at=0;
    while(at<n&&!p[at])++at;
    if(at>=2&&at<n&&p[at]==1){
        *begin=at+1;*end=n;
        for(size_t i=*begin;i+3<=n;++i)if(prefix_at(p,n,i)){*end=i;break;}
        while(*end>*begin&&p[*end-1]==0)--*end;
        return true;
    }
    *begin=0;*end=n;
    while(*end&&p[*end-1]==0)--*end;
    return *end!=0;
}
static int finish(X4StreamSps *out,const X4StreamSps *s,X4StreamSpsDiagnostics *d,
                  const Bits *b,int code,unsigned reason)
{
    if(b){
        d->rbsp_bit_offset=b->rbsp_bits;d->ebsp_byte_offset=b->at+1;d->reader_error=b->error;
        if(b->error==READ_TRUNCATED)code=X4_SPS_TRUNCATED;
        else if(b->error==READ_EBSP)code=X4_SPS_EBSP;
        else if(b->error==READ_OVERFLOW)code=X4_SPS_SYNTAX;
    }
    d->code=code;d->reason=reason;*out=*s;return code;
}
/* All call sites test the reader before validating the value just read. */
#define READ_CHECK() do{if(b.error)return finish(out,&s,d,&b,X4_SPS_SYNTAX,1);}while(0)
#define REJECT(code,why) return finish(out,&s,d,&b,(code),(why))
#define STAGE(v) (d->stage=(v))
static int parse_vui(Bits *r,X4StreamSps *s,X4StreamSpsDiagnostics *d)
{
    Bits b=*r;
    if(bit(&b)){unsigned id=bits(&b,8);if(id==255){bits(&b,16);bits(&b,16);}}
    if(bit(&b))bit(&b);
    s->video_signal_type_present=bit(&b);
    if(s->video_signal_type_present){
        bits(&b,3);s->full_range=bit(&b);
        if(!b.error)d->valid_mask|=X4_SPS_HAVE_FULL_RANGE;
        s->colour_description_present=bit(&b);
        if(s->colour_description_present){
            s->colour_primaries=bits(&b,8);s->transfer_characteristics=bits(&b,8);s->matrix_coefficients=bits(&b,8);
            if(!b.error)d->valid_mask|=X4_SPS_HAVE_MATRIX;
        }
    }
    if(!b.error)d->valid_mask|=X4_SPS_HAVE_COLOR;
    if(bit(&b)){unsigned top=ue(&b),bottom=ue(&b);if(!b.error&&(top>5||bottom>5)){*r=b;return X4_SPS_COLOR;}}
    if(bit(&b)){bits(&b,32);bits(&b,32);bit(&b);}
    bool nal=bit(&b);if(nal&&!hrd(&b)){*r=b;return X4_SPS_SYNTAX;}
    bool vcl=bit(&b);if(vcl&&!hrd(&b)){*r=b;return X4_SPS_SYNTAX;}
    if(nal||vcl)bit(&b);
    bit(&b);s->bitstream_restriction_present=bit(&b);
    if(s->bitstream_restriction_present){
        bit(&b);ue(&b);ue(&b);ue(&b);ue(&b);
        s->num_reorder_frames=ue(&b);
        if(!b.error)d->valid_mask|=X4_SPS_HAVE_REORDER_COUNT;
        s->max_dec_frame_buffering=ue(&b);
        if(!b.error)d->valid_mask|=X4_SPS_HAVE_DPB_COUNT;
        if(!b.error)d->valid_mask|=X4_SPS_HAVE_DPB;
        s->dpb_source=1;
    }
    *r=b;return 0;
}
int x4_stream_sps_inspect_diagnostic(const uint8_t *p,size_t n,X4StreamSps *out,X4StreamSpsDiagnostics *d)
{
    if(!out||!d)return X4_SPS_INVALID;
    memset(d,0,sizeof(*d));
    X4StreamSps s={.chroma_format_idc=1,.bit_depth_luma=8,.bit_depth_chroma=8,
        .matrix_coefficients=2,.colour_primaries=2,.transfer_characteristics=2};
    if(!p||!n||n>4096)return finish(out,&s,d,NULL,X4_SPS_INVALID,1);
    size_t begin=0,end=0;if(!nal_span(p,n,&begin,&end)||begin>=end)return finish(out,&s,d,NULL,X4_SPS_INVALID,2);
    p+=begin;n=end-begin;
    if((p[0]&0x80)||(p[0]&31)!=7)return finish(out,&s,d,NULL,X4_SPS_INVALID,3);
    d->valid_mask|=X4_SPS_HAVE_NAL;
    Bits b={.p=p+1,.n=n-1};STAGE(X4_SPS_STAGE_PROFILE);
    s.profile_idc=bits(&b,8);READ_CHECK();d->valid_mask|=X4_SPS_HAVE_PROFILE;
    s.constraints=bits(&b,8);READ_CHECK();d->valid_mask|=X4_SPS_HAVE_CONSTRAINTS;
    s.level_idc=bits(&b,8);READ_CHECK();d->valid_mask|=X4_SPS_HAVE_LEVEL;
    if(s.constraints&3)REJECT(X4_SPS_PROFILE,2);
    s.seq_parameter_set_id=ue(&b);READ_CHECK();
    if(s.seq_parameter_set_id>31)REJECT(X4_SPS_PROFILE,3);
    d->valid_mask|=X4_SPS_HAVE_ID;
    if(s.profile_idc!=66&&s.profile_idc!=77&&s.profile_idc!=88&&!high_syntax(s.profile_idc))d->warning_mask|=X4_SPS_WARN_UNKNOWN_PROFILE;
    STAGE(X4_SPS_STAGE_FORMAT);
    if(high_syntax(s.profile_idc)){
        s.chroma_format_idc=ue(&b);READ_CHECK();d->valid_mask|=X4_SPS_HAVE_CHROMA;
        if(s.chroma_format_idc>3)REJECT(X4_SPS_CHROMA,2);
        if(s.chroma_format_idc==3){s.separate_colour_plane_flag=bit(&b);READ_CHECK();}
        unsigned luma=ue(&b);READ_CHECK();s.bit_depth_luma=luma<=UINT_MAX-8?8+luma:UINT_MAX;
        d->valid_mask|=X4_SPS_HAVE_DEPTH_LUMA;
        if(luma>6)REJECT(X4_SPS_BIT_DEPTH,2);
        unsigned chroma=ue(&b);READ_CHECK();s.bit_depth_chroma=chroma<=UINT_MAX-8?8+chroma:UINT_MAX;
        d->valid_mask|=X4_SPS_HAVE_DEPTH_CHROMA;
        if(chroma>6)REJECT(X4_SPS_BIT_DEPTH,3);
        bit(&b);unsigned lists=bit(&b);READ_CHECK();
        if(lists)for(unsigned i=0;i<(s.chroma_format_idc==3?12u:8u)&&!b.error;++i)if(bit(&b))scaling(&b,i<6?16:64);
        READ_CHECK();
    }else d->valid_mask|=X4_SPS_HAVE_CHROMA|X4_SPS_HAVE_DEPTH_LUMA|X4_SPS_HAVE_DEPTH_CHROMA;
    d->valid_mask|=X4_SPS_HAVE_FORMAT;
    STAGE(X4_SPS_STAGE_FRAME_NUM);s.log2_max_frame_num_minus4=ue(&b);READ_CHECK();
    if(s.log2_max_frame_num_minus4>12)REJECT(X4_SPS_SYNTAX,2);
    d->valid_mask|=X4_SPS_HAVE_FRAME_NUM;
    STAGE(X4_SPS_STAGE_POC);s.pic_order_cnt_type=ue(&b);READ_CHECK();d->valid_mask|=X4_SPS_HAVE_POC_TYPE;
    if(s.pic_order_cnt_type==0){s.log2_max_pic_order_cnt_lsb_minus4=ue(&b);READ_CHECK();if(s.log2_max_pic_order_cnt_lsb_minus4>12)REJECT(X4_SPS_SYNTAX,3);}
    else if(s.pic_order_cnt_type==1){
        bit(&b);se(&b);se(&b);unsigned count=ue(&b);READ_CHECK();if(count>255)REJECT(X4_SPS_SYNTAX,4);
        for(unsigned i=0;i<count&&!b.error;++i)se(&b);
        READ_CHECK();
    }else if(s.pic_order_cnt_type!=2)REJECT(X4_SPS_SYNTAX,5);
    d->valid_mask|=X4_SPS_HAVE_POC;
    STAGE(X4_SPS_STAGE_REFS);s.max_num_ref_frames=ue(&b);READ_CHECK();
    if(s.max_num_ref_frames>INT_MAX)REJECT(X4_SPS_LEVEL_DPB,1);
    d->valid_mask|=X4_SPS_HAVE_REFS;bit(&b);READ_CHECK();
    STAGE(X4_SPS_STAGE_SIZE);unsigned wm=ue(&b);READ_CHECK();
    s.pic_width_in_mbs_minus1=wm;d->valid_mask|=X4_SPS_HAVE_WIDTH;
    if(wm>UINT_MAX/16-1)REJECT(X4_SPS_SIZE,1);
    s.coded_width=(wm+1)*16;
    unsigned hm=ue(&b);READ_CHECK();s.pic_height_in_map_units_minus1=hm;d->valid_mask|=X4_SPS_HAVE_MAP_HEIGHT;
    if(hm>UINT_MAX/32-1)REJECT(X4_SPS_SIZE,1);
    STAGE(X4_SPS_STAGE_INTERLACE);s.frame_mbs_only_flag=bit(&b);READ_CHECK();d->valid_mask|=X4_SPS_HAVE_FRAME_ONLY;
    s.coded_height=(hm+1)*16*(2-s.frame_mbs_only_flag);d->valid_mask|=X4_SPS_HAVE_SIZE;
    if(!s.frame_mbs_only_flag){s.mb_adaptive_frame_field_flag=bit(&b);READ_CHECK();}
    s.direct_8x8_inference_flag=bit(&b);READ_CHECK();d->valid_mask|=X4_SPS_HAVE_FRAME_FLAGS;
    STAGE(X4_SPS_STAGE_CROP);
    if(bit(&b)){
        unsigned cat=s.separate_colour_plane_flag?0:s.chroma_format_idc;
        unsigned ux=cat==1||cat==2?2:1,uy=(cat==1?2:1)*(2-s.frame_mbs_only_flag);
        unsigned left=ue(&b);READ_CHECK();if(left>UINT_MAX/ux)REJECT(X4_SPS_CROP,1);
        s.crop_left=left*ux;d->valid_mask|=X4_SPS_HAVE_CROP_LEFT;
        unsigned right=ue(&b);READ_CHECK();if(right>UINT_MAX/ux)REJECT(X4_SPS_CROP,1);
        s.crop_right=right*ux;d->valid_mask|=X4_SPS_HAVE_CROP_RIGHT;
        unsigned top=ue(&b);READ_CHECK();if(top>UINT_MAX/uy)REJECT(X4_SPS_CROP,1);
        s.crop_top=top*uy;d->valid_mask|=X4_SPS_HAVE_CROP_TOP;
        unsigned bottom=ue(&b);READ_CHECK();if(bottom>UINT_MAX/uy)REJECT(X4_SPS_CROP,1);
        s.crop_bottom=bottom*uy;d->valid_mask|=X4_SPS_HAVE_CROP_BOTTOM;
    }else if(!b.error)d->valid_mask|=X4_SPS_HAVE_CROP_LEFT|X4_SPS_HAVE_CROP_RIGHT|X4_SPS_HAVE_CROP_TOP|X4_SPS_HAVE_CROP_BOTTOM;
    READ_CHECK();d->valid_mask|=X4_SPS_HAVE_CROP;
    if((uint64_t)s.crop_left+s.crop_right>=s.coded_width||(uint64_t)s.crop_top+s.crop_bottom>=s.coded_height)REJECT(X4_SPS_CROP,2);
    s.visible_width=s.coded_width-s.crop_left-s.crop_right;s.visible_height=s.coded_height-s.crop_top-s.crop_bottom;
    d->valid_mask|=X4_SPS_HAVE_GEOMETRY;
    STAGE(X4_SPS_STAGE_VUI);s.vui_present=bit(&b);READ_CHECK();
    if(s.vui_present){int rc=parse_vui(&b,&s,d);READ_CHECK();if(rc)REJECT(rc,2);}
    else d->valid_mask|=X4_SPS_HAVE_COLOR|X4_SPS_HAVE_FULL_RANGE|X4_SPS_HAVE_MATRIX;
    d->valid_mask|=X4_SPS_HAVE_VUI;
    STAGE(X4_SPS_STAGE_TRAILING);unsigned stop=bit(&b);READ_CHECK();if(!stop)REJECT(X4_SPS_SYNTAX,6);
    while(b.left){unsigned padding=bit(&b);READ_CHECK();if(padding)REJECT(X4_SPS_SYNTAX,7);}
    /* The raw NAL may include legal trailing_zero_8bits, but never another NAL. */
    while(b.at<b.n){unsigned padding=bits(&b,8);READ_CHECK();if(padding)REJECT(X4_SPS_SYNTAX,8);}
    d->valid_mask|=X4_SPS_HAVE_TRAILING;d->syntax_complete=true;
    uint64_t fs=(uint64_t)(wm+1)*(hm+1)*(2-s.frame_mbs_only_flag);
    unsigned dpb=level_dpb(s.level_idc,s.constraints);
    unsigned max=dpb&&fs?(unsigned)(dpb/fs):0;if(max>16)max=16;
    if(!dpb)d->warning_mask|=X4_SPS_WARN_UNKNOWN_LEVEL;
    else if(!max||fs>level_fs(s.level_idc,s.constraints)||s.max_num_ref_frames>max)d->warning_mask|=X4_SPS_WARN_LEVEL_LIMIT;
    if(!s.bitstream_restriction_present){
        s.dpb_source=dpb?2:3;s.max_dec_frame_buffering=max;
        if(s.max_dec_frame_buffering<s.max_num_ref_frames)s.max_dec_frame_buffering=s.max_num_ref_frames;
        if(!s.max_dec_frame_buffering)s.max_dec_frame_buffering=1;
        s.num_reorder_frames=(s.profile_idc==66||s.profile_idc==83)&&(s.constraints&0x40)?0:s.max_dec_frame_buffering;
        d->warning_mask|=X4_SPS_WARN_INFERRED_DPB;d->valid_mask|=X4_SPS_HAVE_DPB|X4_SPS_HAVE_REORDER_COUNT|X4_SPS_HAVE_DPB_COUNT;
    }
    if(s.max_dec_frame_buffering>INT_MAX||s.num_reorder_frames>INT_MAX)REJECT(X4_SPS_LEVEL_DPB,2);
    if(!s.max_dec_frame_buffering||s.max_num_ref_frames>s.max_dec_frame_buffering||s.num_reorder_frames>s.max_dec_frame_buffering||(max&&s.max_dec_frame_buffering>max))d->warning_mask|=X4_SPS_WARN_DPB;
    if(s.matrix_coefficients!=1&&s.matrix_coefficients!=2&&s.matrix_coefficients!=5&&s.matrix_coefficients!=6)d->warning_mask|=X4_SPS_WARN_COLOR;
    if(s.full_range)d->warning_mask|=X4_SPS_WARN_FULL_RANGE;
    if(!((s.visible_width==1280&&s.visible_height==720)||(s.visible_width==1920&&s.visible_height==1080)))d->warning_mask|=X4_SPS_WARN_GEOMETRY;
    STAGE(X4_SPS_STAGE_POLICY);
    if(s.chroma_format_idc!=1||s.separate_colour_plane_flag)REJECT(X4_SPS_CHROMA,1);
    if(s.bit_depth_luma!=8||s.bit_depth_chroma!=8)REJECT(X4_SPS_BIT_DEPTH,1);
    if(s.coded_width>1920||s.coded_height>1088)REJECT(X4_SPS_SIZE,2);
    if(!s.frame_mbs_only_flag)REJECT(X4_SPS_INTERLACE,1);
    if(!s.visible_width||!s.visible_height)REJECT(X4_SPS_GEOMETRY,1);
    STAGE(X4_SPS_STAGE_DONE);return finish(out,&s,d,&b,0,0);
}
#undef READ_CHECK
#undef REJECT
#undef STAGE
int x4_stream_sps_inspect(const uint8_t *p,size_t n,X4StreamSps *out,bool *syntax_complete)
{
    if(!syntax_complete)return X4_SPS_INVALID;
    *syntax_complete=false;X4StreamSpsDiagnostics d={0};int rc=x4_stream_sps_inspect_diagnostic(p,n,out,&d);
    *syntax_complete=d.syntax_complete;return rc;
}
int x4_stream_sps_parse(const uint8_t *p,size_t n,X4StreamSps *out)
{
    if(!out)return X4_SPS_INVALID;
    X4StreamSps observed={0};X4StreamSpsDiagnostics d={0};int rc=x4_stream_sps_inspect_diagnostic(p,n,&observed,&d);
    if(!rc)*out=observed;
    return rc;
}
bool x4_stream_sps_compatible(const X4StreamSps *a,const X4StreamSps *b)
{
    /* Diagnostics and inferred-vs-explicit provenance do not change the native
     * contract. POC/SPS identifiers do: no decoder reconfiguration in flight. */
    if(!a||!b)return false;
#define SAME(f) if(a->f!=b->f)return false
    SAME(profile_idc);SAME(constraints);SAME(level_idc);SAME(coded_width);SAME(coded_height);
    SAME(visible_width);SAME(visible_height);SAME(crop_left);SAME(crop_top);SAME(crop_right);SAME(crop_bottom);
    SAME(chroma_format_idc);SAME(bit_depth_luma);SAME(bit_depth_chroma);SAME(max_num_ref_frames);
    SAME(max_dec_frame_buffering);SAME(num_reorder_frames);SAME(matrix_coefficients);SAME(full_range);
    SAME(seq_parameter_set_id);SAME(log2_max_frame_num_minus4);SAME(pic_order_cnt_type);
    SAME(log2_max_pic_order_cnt_lsb_minus4);SAME(separate_colour_plane_flag);SAME(frame_mbs_only_flag);
    SAME(mb_adaptive_frame_field_flag);SAME(direct_8x8_inference_flag);
#undef SAME
    return true;
}
int x4_stream_sps_check_au(const uint8_t *p,size_t n,const X4StreamSps *initial)
{
    if(!p||!n||!initial)return X4_SPS_INVALID;
    size_t at=0;
    while(at+3<=n){
        size_t prefix=prefix_at(p,n,at);if(!prefix){++at;continue;}
        size_t begin=at+prefix;if(begin>=n)return X4_SPS_TRUNCATED;
        size_t end=n;for(size_t i=begin+1;i+3<=n;++i)if(prefix_at(p,n,i)){end=i;break;}
        if((p[begin]&31)==7){
            X4StreamSps s={0};int rc=x4_stream_sps_parse(p+begin,end-begin,&s);if(rc)return rc;
            if(!x4_stream_sps_compatible(&s,initial))return X4_SPS_CHANGED;
        }
        at=end;
    }
    return 0;
}
const char *x4_stream_sps_stage_name(unsigned stage)
{
    static const char *const names[]={"input","profile","format","frame_num","poc","refs","size","interlace","crop","vui","trailing","policy","done"};
    return stage<sizeof(names)/sizeof(*names)?names[stage]:"unknown";
}
const char *x4_stream_sps_error_name(int code)
{
    switch(code){case 0:return "accepted";case X4_SPS_INVALID:return "invalid_nal_or_argument";case X4_SPS_CHANGED:return "changed_sps_contract";
    case X4_SPS_PROFILE:return "profile_syntax";case X4_SPS_CHROMA:return "unsupported_chroma";case X4_SPS_BIT_DEPTH:return "unsupported_bit_depth";
    case X4_SPS_SIZE:return "coded_size_limit";case X4_SPS_INTERLACE:return "interlaced";case X4_SPS_CROP:return "invalid_crop";
    case X4_SPS_LEVEL_DPB:return "unrepresentable_dpb";case X4_SPS_COLOR:return "colour_syntax";case X4_SPS_GEOMETRY:return "invalid_geometry";
    case X4_SPS_TRUNCATED:return "truncated_rbsp";case X4_SPS_EBSP:return "invalid_emulation_prevention";case X4_SPS_SYNTAX:return "invalid_rbsp_syntax";
    default:return "unknown";}
}
