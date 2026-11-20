#include "avi.hpp"
#include "platform/platform.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mc {
static uint16_t le16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
static uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
static bool tag(const uint8_t* p, const char* value) { return std::memcmp(p,value,4)==0; }
static int stream_number(const uint8_t* p) { return p[0]>='0' && p[0]<='9' && p[1]>='0' && p[1]<='9' ? (p[0]-'0')*10+p[1]-'0' : -1; }
void AviDecoder::close() {
    file_.close(); info_={}; video_.clear();audio_.clear();sample_block_.clear();
    video_stream_=audio_stream_=-1; audio_frames_=audio_cursor_=audio_output_left_=0; sample_offset_=sample_frames_=0;
    samples_initialized_=false;audio_failed_=false; phase_=0;cancel_=nullptr;
}
bool AviDecoder::parse_stream(uint64_t start,uint64_t end,int index,std::string& error) {
    std::vector<uint8_t> stream_header, format;
    for (uint64_t pos=start;pos<end;) {
        uint8_t h[8]; if (end-pos<8 || !file_.read_at(pos,h,8)) { error="Truncated AVI stream header";return false; }
        uint64_t bytes=le32(h+4),data=pos+8; if (bytes>end-data) { error="AVI stream chunk leaves its container";return false; }
        if (tag(h,"strh") || tag(h,"strf")) {
            if (bytes>65536) { error="AVI format header is too large";return false; }
            auto& target=tag(h,"strh")?stream_header:format; target.resize(static_cast<size_t>(bytes));
            if (!file_.read_at(data,target.data(),target.size())) { error="AVI header read failed";return false; }
        }
        uint64_t next=data+bytes+(bytes&1); if (next>end) { error="Invalid AVI stream padding";return false; } pos=next;
    }
    if (stream_header.size()<48) { error="Missing AVI stream description";return false; }
    const auto* sh=stream_header.data();
    if (tag(sh,"vids")) {
        if (video_stream_>=0 || format.size()<40) { error="Multiple video streams or missing BITMAPINFOHEADER";return false; }
        const auto* f=format.data(); int32_t width=static_cast<int32_t>(le32(f+4)), height=static_cast<int32_t>(le32(f+8));
        if (width<=0 || width>1920 || height==0 || height>1080 || height< -1080 || le16(f+12)!=1) { error="AVI exceeds 1920x1080 or has invalid dimensions";return false; }
        if (le32(f+16)==0 && le16(f+14)==24) { mjpeg_=false;info_.codec="AVI RGB24"; }
        else if (tag(f+16,"MJPG") || tag(f+16,"mjpg")) { mjpeg_=true;info_.codec="AVI MJPEG"; }
        else { error="AVI video codec unavailable; use MJPEG or uncompressed 24-bit RGB";return false; }
        uint32_t scale=le32(sh+20),rate=le32(sh+24);
        if (!scale || !rate || double(rate)/scale<1 || double(rate)/scale>60 || le32(sh+28)!=0) { error="Unsupported AVI timing or nonzero stream start";return false; }
        info_.width=width;bottom_up_=height>0;info_.height=height>0?height:-height;info_.frame_seconds=double(scale)/rate;video_stream_=index;
    } else if (tag(sh,"auds")) {
        if (audio_stream_>=0 || format.size()<16) { error="Multiple audio streams or missing audio format";return false; }
        const auto* f=format.data();unsigned channels=le16(f+2),rate=le32(f+4),bits=le16(f+14),align=le16(f+12);
        if (le16(f)!=1 || channels<1 || channels>2 || rate<8000 || rate>192000
            || (bits!=8 && bits!=16 && bits!=24 && bits!=32) || align!=channels*(bits/8) || le32(f+8)!=rate*align) {
            error="AVI audio codec unavailable; use mono/stereo integer PCM";return false;
        }
        audio_stream_=index;audio_bits_=bits;audio_align_=align;info_.has_audio=true;
        info_.audio.sample_rate=rate;info_.audio.channels=channels;info_.audio.codec="AVI PCM";
    } else { error="Unsupported extra AVI stream; subtitles and multiple tracks are unavailable";return false; }
    return true;
}
bool AviDecoder::parse_headers(uint64_t start,uint64_t end,std::string& error) {
    int index=0;
    for (uint64_t pos=start;pos<end;) {
        uint8_t h[12]; if (end-pos<8 || !file_.read_at(pos,h,8)) { error="Truncated AVI header";return false; }
        uint64_t bytes=le32(h+4),data=pos+8;if (bytes>end-data) { error="Invalid AVI header size";return false; }
        if (tag(h,"LIST")) {
            if (bytes<4 || !file_.read_at(data,h+8,4)) { error="Invalid AVI header list";return false; }
            if (tag(h+8,"strl")) { if (index>=8 || !parse_stream(data+4,data+bytes,index++,error)) return false; }
        }
        uint64_t next=data+bytes+(bytes&1);if (next>end) { error="Invalid AVI header padding";return false; }pos=next;
    }
    return video_stream_>=0;
}
bool AviDecoder::index_movi(uint64_t start,uint64_t end,unsigned depth,std::string& error) {
    if (depth>8) { error="AVI record nesting is too deep";return false; }
    for (uint64_t pos=start;pos<end;) {
        if (cancel_ && cancel_->load()) { error="Cancelled";return false; }
        uint8_t h[12];if (end-pos<8 || !file_.read_at(pos,h,8)) { error="Truncated AVI media chunk";return false; }
        uint64_t bytes=le32(h+4),data=pos+8;if (bytes>end-data) { error="AVI media chunk leaves the file";return false; }
        if (tag(h,"LIST")) {
            if (bytes<4 || !file_.read_at(data,h+8,4) || !index_movi(data+4,data+bytes,depth+1,error)) return false;
        } else {
            int stream=stream_number(h);
            if (stream==video_stream_ && h[2]=='d' && (h[3]=='b' || h[3]=='c')) {
                if (!bytes || bytes>8u*1024u*1024u || video_.size()>=216000) { error="AVI video frame or index exceeds its memory limit";return false; }
                if (!mjpeg_) { uint64_t stride=(uint64_t(info_.width)*3+3)&~uint64_t(3);if (bytes!=stride*info_.height) { error="Uncompressed AVI frame length is invalid";return false; } }
                video_.push_back({data,static_cast<uint64_t>(video_.size()),static_cast<uint32_t>(bytes)});
            } else if (stream==audio_stream_ && h[2]=='w' && h[3]=='b') {
                if (bytes%audio_align_ || bytes>16u*1024u*1024u || audio_.size()>=262144) { error="AVI PCM chunk or index exceeds its memory limit";return false; }
                if (bytes) { audio_.push_back({data,audio_frames_,static_cast<uint32_t>(bytes)});audio_frames_+=bytes/audio_align_; }
            }
        }
        uint64_t next=data+bytes+(bytes&1);if (next>end) { error="Invalid AVI media padding";return false; }pos=next;
    }
    return true;
}
bool AviDecoder::open(const Item& item,const std::atomic<bool>& cancel,std::string& error) {
    close();cancel_=&cancel;
    if (extension(item.entry.name)!="avi") { error="The legacy AVI reader requires a RIFF AVI file";return false; }
    if (!file_.open(item.source,item.entry.relative,error,&cancel)) return false;
    uint8_t h[12];if (!file_.read_at(0,h,12) || !tag(h,"RIFF") || !tag(h+8,"AVI ")) { error="Not a valid RIFF AVI file";return false; }
    uint64_t end=uint64_t(le32(h+4))+8;
    if (end<12 || end>file_.size() || file_.size()>end+1) { error="Truncated AVI or unsupported OpenDML/AVIX segments";return false; }
    uint64_t movi_start=0,movi_end=0;bool headers=false;
    for (uint64_t pos=12;pos<end;) {
        if (end-pos<8 || !file_.read_at(pos,h,8)) { error="Truncated AVI container";return false; }
        uint64_t bytes=le32(h+4),data=pos+8;if (bytes>end-data) { error="AVI chunk size is outside the container";return false; }
        if (tag(h,"LIST")) {
            if (bytes<4 || !file_.read_at(data,h+8,4)) { error="Invalid AVI list";return false; }
            if (tag(h+8,"hdrl")) { if (headers || !parse_headers(data+4,data+bytes,error)) { if (error.empty()) error="Missing AVI video stream";return false; }headers=true; }
            if (tag(h+8,"movi")) { if (movi_start) { error="Multiple AVI media segments are unavailable";return false; }movi_start=data+4;movi_end=data+bytes; }
        }
        uint64_t next=data+bytes+(bytes&1);if (next>end) { error="Invalid AVI container padding";return false; }pos=next;
    }
    if (!headers || !movi_start || !index_movi(movi_start,movi_end,0,error) || video_.empty()) { if (error.empty()) error="AVI has no decodable video frames";return false; }
    info_.frames=video_.size();info_.duration=info_.frames*info_.frame_seconds;
    if (info_.duration>7200) { error="AVI duration exceeds the two-hour index budget";return false; }
    if (info_.has_audio) {
        if (!audio_frames_) { error="AVI audio stream is empty";return false; }
        info_.audio.total_frames=audio_frames_;info_.audio.duration=double(audio_frames_)/info_.audio.sample_rate;
        sample_block_.resize(1024*info_.audio.channels);seek_audio(0);
    }
    return !cancel;
}
std::shared_ptr<Bitmap> AviDecoder::frame(uint64_t index,std::string& error) {
    if (index>=video_.size()) { error="Video frame is outside the checked index";return {}; }
    const auto& chunk=video_[static_cast<size_t>(index)];std::vector<uint8_t> bytes(chunk.bytes);
    if (!file_.read_at(chunk.offset,bytes.data(),bytes.size())) { error="Video read failed or storage disconnected";return {}; }
    std::shared_ptr<Bitmap> bitmap;
    if (mjpeg_) {
        bitmap=decode_bitmap(bytes.data(),bytes.size(),1920u*1080u,error);
        if (bitmap && (bitmap->width!=info_.width || bitmap->height!=info_.height)) { error="MJPEG frame dimensions differ from the AVI header";return {}; }
    } else {
        auto* pixels=static_cast<uint8_t*>(decode_allocate(static_cast<size_t>(info_.width)*info_.height*4));
        if (!pixels) { error="Video frame memory budget reached";return {}; }
        bitmap=std::make_shared<Bitmap>();bitmap->width=info_.width;bitmap->height=info_.height;bitmap->pixels={pixels,decode_free};
        size_t stride=(static_cast<size_t>(info_.width)*3+3)&~size_t(3);
        for (int y=0;y<info_.height;++y) {
            const uint8_t* row=bytes.data()+(bottom_up_?info_.height-1-y:y)*stride;
            for (int x=0;x<info_.width;++x) { size_t at=(static_cast<size_t>(y)*info_.width+x)*4;pixels[at]=row[x*3+2];pixels[at+1]=row[x*3+1];pixels[at+2]=row[x*3];pixels[at+3]=255; }
        }
    }
    return bitmap;
}
bool AviDecoder::seek_audio(double seconds) {
    if (!info_.has_audio || !std::isfinite(seconds)) return false;
    seconds=std::max(0.0,std::min(info_.audio.duration,seconds));audio_cursor_=static_cast<uint64_t>(seconds*info_.audio.sample_rate);
    audio_cursor_=std::min(audio_cursor_,audio_frames_);audio_output_left_=static_cast<uint64_t>(std::ceil(double(audio_frames_-audio_cursor_)*output_sample_rate/info_.audio.sample_rate));
    phase_=0;samples_initialized_=false;audio_failed_=false;sample_frames_=sample_offset_=0;return true;
}
size_t AviDecoder::read_pcm(int16_t* output,size_t frames) {
    size_t done=0;
    while (done<frames && audio_cursor_<audio_frames_) {
        auto it=std::upper_bound(audio_.begin(),audio_.end(),audio_cursor_,[](uint64_t frame,const Chunk& chunk) { return frame<chunk.first; });
        if (it==audio_.begin()) return done;--it;
        uint64_t local=audio_cursor_-it->first;size_t count=static_cast<size_t>(std::min<uint64_t>(frames-done,it->bytes/audio_align_-local));
        std::vector<uint8_t> data(count*audio_align_);if (!file_.read_at(it->offset+local*audio_align_,data.data(),data.size())) { audio_failed_=true;return done; }
        unsigned sample_bytes=audio_bits_/8;
        for (size_t i=0;i<count*info_.audio.channels;++i) {
            const auto* p=data.data()+i*sample_bytes;int32_t value=0;
            if (audio_bits_==8) value=(int32_t(p[0])-128)*256;
            else if (audio_bits_==16) value=static_cast<int16_t>(le16(p));
            else if (audio_bits_==24) { uint32_t packed=uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16);value=static_cast<int32_t>(packed<<8)>>16; }
            else value=static_cast<int32_t>(le32(p))>>16;
            output[done*info_.audio.channels+i]=static_cast<int16_t>(value);
        }
        done+=count;audio_cursor_+=count;
    }
    return done;
}
bool AviDecoder::pull_audio(int16_t sample[2]) {
    if (sample_offset_>=sample_frames_) { sample_frames_=read_pcm(sample_block_.data(),1024);sample_offset_=0;if (!sample_frames_) return false; }
    const auto* p=sample_block_.data()+sample_offset_++*info_.audio.channels;sample[0]=p[0];sample[1]=info_.audio.channels==2?p[1]:p[0];return true;
}
size_t AviDecoder::read_audio_stereo(int16_t* output,size_t frames) {
    if (!info_.has_audio) return 0;
    if (!samples_initialized_) { if (!pull_audio(sample_a_)) return 0;if (!pull_audio(sample_b_)) { sample_b_[0]=sample_a_[0];sample_b_[1]=sample_a_[1]; }samples_initialized_=true; }
    size_t done=0;double step=double(info_.audio.sample_rate)/output_sample_rate;
    while (done<frames && audio_output_left_ && !(cancel_ && cancel_->load())) {
        for (int c=0;c<2;++c) output[done*2+c]=static_cast<int16_t>(std::lround(sample_a_[c]*(1-phase_)+sample_b_[c]*phase_));
        ++done;--audio_output_left_;phase_+=step;
        while (phase_>=1) { phase_-=1;sample_a_[0]=sample_b_[0];sample_a_[1]=sample_b_[1];if (!pull_audio(sample_b_)) { sample_b_[0]=sample_a_[0];sample_b_[1]=sample_a_[1]; } }
    }
    return done;
}
}
