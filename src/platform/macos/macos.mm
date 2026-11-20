#include "platform/platform.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>
#import <QuartzCore/QuartzCore.h>
#include <AudioUnit/AudioUnit.h>

@interface MCView : NSView
@end
@implementation MCView
- (BOOL)isFlipped { return YES; }
@end
@interface MCWindowDelegate : NSObject<NSWindowDelegate>
@property(nonatomic) BOOL closed;
@end
@implementation MCWindowDelegate
- (void)windowWillClose:(NSNotification*)notification { (void)notification;self.closed=YES; }
@end

namespace mc {
class MacAudio final:public AudioSink {
    AudioUnit unit_=nullptr;
    std::array<int16_t,8192*2> ring_{};
    size_t read_=0,write_=0,frames_=0;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> paused_{false},alive_{false};
    std::atomic<uint64_t> played_{0};
    static OSStatus callback(void* context,AudioUnitRenderActionFlags*,const AudioTimeStamp*,UInt32,UInt32 count,AudioBufferList* buffers) {
        auto* sink=static_cast<MacAudio*>(context);
        for (UInt32 b=0;b<buffers->mNumberBuffers;++b) if (buffers->mBuffers[b].mData) std::memset(buffers->mBuffers[b].mData,0,buffers->mBuffers[b].mDataByteSize);
        if (!sink->alive_ || sink->paused_ || buffers->mNumberBuffers!=1 || !buffers->mBuffers[0].mData) return noErr;
        auto* output=static_cast<int16_t*>(buffers->mBuffers[0].mData);count=std::min<UInt32>(count,buffers->mBuffers[0].mDataByteSize/4);
        size_t taken=0;
        {
            std::lock_guard<std::mutex> lock(sink->mutex_);taken=std::min<size_t>(count,sink->frames_);
            for (size_t i=0;i<taken;++i) { output[i*2]=sink->ring_[sink->read_*2];output[i*2+1]=sink->ring_[sink->read_*2+1];sink->read_=(sink->read_+1)%8192; }
            sink->frames_-=taken;sink->played_.fetch_add(taken);
        }
        sink->wake_.notify_one();return noErr;
    }
public:
    ~MacAudio() override { close(); }
    bool open(std::string& error) override {
        AudioComponentDescription description{};description.componentType=kAudioUnitType_Output;description.componentSubType=kAudioUnitSubType_DefaultOutput;description.componentManufacturer=kAudioUnitManufacturer_Apple;
        AudioComponent component=AudioComponentFindNext(nullptr,&description);
        OSStatus status=component?AudioComponentInstanceNew(component,&unit_):-1;
        if (status) { error="AudioComponentInstanceNew: "+std::to_string(status);return false; }
        AudioStreamBasicDescription format{};format.mSampleRate=output_sample_rate;format.mFormatID=kAudioFormatLinearPCM;format.mFormatFlags=kAudioFormatFlagIsSignedInteger|kAudioFormatFlagIsPacked|kAudioFormatFlagsNativeEndian;
        format.mBytesPerPacket=4;format.mFramesPerPacket=1;format.mBytesPerFrame=4;format.mChannelsPerFrame=2;format.mBitsPerChannel=16;
        status=AudioUnitSetProperty(unit_,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&format,sizeof(format));
        AURenderCallbackStruct render{callback,this};if (!status) status=AudioUnitSetProperty(unit_,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&render,sizeof(render));
        if (!status) status=AudioUnitInitialize(unit_);
        if (status) { error="CoreAudio setup: "+std::to_string(status);close();return false; }
        alive_=true;status=AudioOutputUnitStart(unit_);
        if (status) { error="AudioOutputUnitStart: "+std::to_string(status);close();return false; }return true;
    }
    bool push(const int16_t* stereo,size_t count,const std::atomic<bool>& cancel) override {
        if (count>8192) return false;
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        std::unique_lock<std::mutex> lock(mutex_);
        while (8192-frames_<count && alive_ && !cancel) { if (wake_.wait_until(lock,deadline)==std::cv_status::timeout) return false; }
        if (!alive_ || cancel) return false;
        for (size_t i=0;i<count;++i) { ring_[write_*2]=stereo[i*2];ring_[write_*2+1]=stereo[i*2+1];write_=(write_+1)%8192; }
        frames_+=count;return true;
    }
    uint64_t played_frames() const override { return played_; }
    void pause(bool paused) override { paused_=paused; }
    void flush() override {
        if (unit_) AudioOutputUnitStop(unit_);
        { std::lock_guard<std::mutex> lock(mutex_);read_=write_=frames_=0;played_=0; }
        if (unit_ && AudioOutputUnitStart(unit_)!=noErr) alive_=false;
    }
    void close() override {
        alive_=false;wake_.notify_all();if (unit_) { AudioOutputUnitStop(unit_);AudioUnitUninitialize(unit_);AudioComponentInstanceDispose(unit_);unit_=nullptr; }
    }
};
class MacPlatform final:public Platform {
    std::string assets_,data_;
    NSWindow* window_=nil;
    MCView* view_=nil;
    MCWindowDelegate* delegate_=nil;
    uint32_t keyboard_=0;
    uint64_t revision_=UINT64_MAX;
    CGColorSpaceRef color_space_=nullptr;
    static uint32_t key(NSEvent* event) {
        switch (event.keyCode) { case 126:return Up;case 125:return Down;case 123:return Left;case 124:return Right;case 36:case 49:return Cross;case 53:return Circle;case 48:return Options;default:break; }
        NSString* chars=event.charactersIgnoringModifiers;if (!chars.length) return 0;
        switch ([chars characterAtIndex:0]) { case 'w':return Up;case 's':return Down;case 'a':return Left;case 'd':return Right;case 't':return Triangle;case 'q':return Square;case '[':return L1;case ']':return R1;case '1':return L2;case '2':return R2;default:return 0; }
    }
public:
    MacPlatform(std::string assets,std::string data):assets_(std::move(assets)),data_(std::move(data)) {}
    ~MacPlatform() override {
        @autoreleasepool { if (window_) { view_.layer.contents=nil;window_.delegate=nil;[window_ orderOut:nil];[window_ close]; } }
        if (color_space_) CGColorSpaceRelease(color_space_);
    }
    bool initialize(std::string&) override {
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        window_=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1280,720) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
        window_.title=@"Media Center";window_.releasedWhenClosed=NO;
        view_=[[MCView alloc] initWithFrame:NSMakeRect(0,0,1280,720)];view_.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
        view_.wantsLayer=YES;view_.layer.backgroundColor=NSColor.blackColor.CGColor;
        view_.layer.contentsGravity=kCAGravityResizeAspect;view_.layer.masksToBounds=YES;
        color_space_=CGColorSpaceCreateDeviceRGB();
        delegate_=[MCWindowDelegate new];delegate_.closed=NO;window_.delegate=delegate_;window_.contentView=view_;
        [window_ center];[window_ makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];[NSApp finishLaunching];return true;
    }
    bool poll(RawInput& input) override {
        @autoreleasepool {
            int pointer_x=-1,pointer_y=-1,pointer_clicks=0;bool pointer_pressed=false;
            for (unsigned i=0;i<64;++i) {
                NSDate* deadline=i==0?[NSDate dateWithTimeIntervalSinceNow:1.0/60]:[NSDate distantPast];
                NSEvent* event=[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:deadline inMode:NSDefaultRunLoopMode dequeue:YES];if (!event) break;
                if (event.type==NSEventTypeKeyDown || event.type==NSEventTypeKeyUp) { uint32_t button=key(event);if (event.type==NSEventTypeKeyDown) keyboard_|=button;else keyboard_&=~button; }
                else {
                    if (event.type==NSEventTypeLeftMouseDown && event.window==window_) {
                        NSPoint point=[view_ convertPoint:event.locationInWindow fromView:nil];NSRect bounds=view_.bounds;
                        double scale=std::min(bounds.size.width/screen_width,bounds.size.height/screen_height);
                        double left=(bounds.size.width-screen_width*scale)/2,top=(bounds.size.height-screen_height*scale)/2;
                        if (scale>0 && point.x>=left && point.y>=top && point.x<left+screen_width*scale && point.y<top+screen_height*scale) {
                            pointer_x=static_cast<int>((point.x-left)/scale);pointer_y=static_cast<int>((point.y-top)/scale);
                            pointer_clicks=static_cast<int>(event.clickCount);pointer_pressed=true;
                        }
                    }
                    [NSApp sendEvent:event];
                }
            }
            if (!window_.isKeyWindow) keyboard_=0;
            input={};input.buttons=keyboard_;
            input.pointer_x=pointer_x;input.pointer_y=pointer_y;input.pointer_clicks=pointer_clicks;input.pointer_pressed=pointer_pressed;
            GCController* controller=GCController.controllers.firstObject;GCExtendedGamepad* p=controller.extendedGamepad;
            if (p) {
                if (p.dpad.up.isPressed) input.buttons|=Up;if (p.dpad.down.isPressed) input.buttons|=Down;if (p.dpad.left.isPressed) input.buttons|=Left;if (p.dpad.right.isPressed) input.buttons|=Right;
                if (p.buttonA.isPressed) input.buttons|=Cross;if (p.buttonB.isPressed) input.buttons|=Circle;if (p.buttonY.isPressed) input.buttons|=Triangle;if (p.buttonX.isPressed) input.buttons|=Square;
                if (p.leftShoulder.isPressed) input.buttons|=L1;if (p.rightShoulder.isPressed) input.buttons|=R1;
                if (p.buttonMenu.isPressed || p.buttonOptions.isPressed) input.buttons|=Options;
                input.lx=p.leftThumbstick.xAxis.value;input.ly=-p.leftThumbstick.yAxis.value;input.rx=p.rightThumbstick.xAxis.value;input.ry=-p.rightThumbstick.yAxis.value;input.lt=p.leftTrigger.value;input.rt=p.rightTrigger.value;
            }
            return !delegate_.closed;
        }
    }
    bool present(const uint32_t* pixels,uint64_t revision,std::string& error) override {
        if (revision_==revision) return true;
        // One replaceable layer image avoids accumulating drawing contexts in
        // the manually driven Cocoa loop. Copy pixels into immutable storage:
        // Core Animation may consume them after the application draws again.
        @autoreleasepool {
            CFDataRef data=CFDataCreate(kCFAllocatorDefault,reinterpret_cast<const UInt8*>(pixels),size_t(screen_width)*screen_height*4);
            CGDataProviderRef provider=data?CGDataProviderCreateWithCFData(data):nullptr;
            CGImageRef image=provider?CGImageCreate(screen_width,screen_height,8,32,screen_width*4,color_space_,kCGBitmapByteOrder32Big|kCGImageAlphaLast,provider,nullptr,false,kCGRenderingIntentDefault):nullptr;
            if (image) {
                [CATransaction begin];[CATransaction setDisableActions:YES];view_.layer.contents=(__bridge id)image;
                [CATransaction commit];[CATransaction flush];revision_=revision;
                CGImageRelease(image);
            }
            if (provider) CGDataProviderRelease(provider);if (data) CFRelease(data);
            if (!image) { error="Cannot allocate the native presentation image";return false; }
        }
        return true;
    }
    std::unique_ptr<AudioSink> make_audio_sink() override { return std::unique_ptr<AudioSink>(new MacAudio); }
    std::string assets_root() const override { return assets_; }
    std::string data_root() const override { return data_; }
};
std::unique_ptr<Platform> make_native_platform(const std::string& assets,const std::string& data) { return std::unique_ptr<Platform>(new MacPlatform(assets,data)); }
}
