#include "platform/ps4/stat_compat.h"
#include "platform/ps4/kernel_result.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
unsigned assertions=0;
void check(bool value,const char* expression) {
    ++assertions;if (!value) throw std::runtime_error(expression);
}
#define REQUIRE(value) check(bool(value),#value)
using namespace std::chrono;

void ordinary_timeouts_return_without_terminating() {
    // macOS and the PS4 target both use FreeBSD's errno value 60.
    REQUIRE(ETIMEDOUT==60);
    std::mutex mutex;std::condition_variable wake;std::unique_lock<std::mutex> lock(mutex);
    for (int i=0;i<3;++i) {
        auto begin=steady_clock::now();
        REQUIRE(wake.wait_until(lock,system_clock::now()+milliseconds(10))==std::cv_status::timeout);
        REQUIRE(steady_clock::now()-begin>=milliseconds(8));REQUIRE(lock.owns_lock());
    }
    REQUIRE(!wake.wait_for(lock,milliseconds(10),[]{return false;}));
    REQUIRE(wake.wait_for(lock,milliseconds(0),[]{return true;}));
}

void notify_one_interrupts_a_timed_worker_wait() {
    std::mutex mutex;std::condition_variable wake,ready;
    bool waiting=false,requested=false,observed=false;
    std::thread worker([&] {
        std::unique_lock<std::mutex> lock(mutex);waiting=true;ready.notify_one();
        observed=wake.wait_for(lock,seconds(2),[&]{return requested;});
    });
    bool started;
    {
        std::unique_lock<std::mutex> lock(mutex);
        started=ready.wait_for(lock,seconds(2),[&]{return waiting;});requested=true;wake.notify_one();
    }
    worker.join();REQUIRE(started);REQUIRE(observed);
}

void notify_all_cancels_and_joins_sleeping_workers() {
    std::mutex mutex;std::condition_variable wake,ready;
    unsigned waiting=0,finished=0;bool cancel=false;
    std::vector<std::thread> workers;
    for (unsigned i=0;i<3;++i) workers.emplace_back([&] {
        std::unique_lock<std::mutex> lock(mutex);++waiting;ready.notify_all();
        wake.wait(lock,[&]{return cancel;});++finished;
    });
    bool started;
    {
        std::unique_lock<std::mutex> lock(mutex);
        started=ready.wait_for(lock,seconds(2),[&]{return waiting==3;});cancel=true;wake.notify_all();
    }
    for (auto& worker:workers) worker.join();REQUIRE(started);REQUIRE(finished==3);
}

template<size_t N> void put(std::array<unsigned char,N>& raw,size_t offset,uint64_t value,unsigned bytes) {
    for (unsigned i=0;i<bytes;++i) raw.at(offset+i)=static_cast<unsigned char>(value>>(i*8));
}
void kernel_stat_offsets_preserve_large_sizes_and_timestamps() {
    // Write by native byte offset, independently of McKernelStat's field names.
    std::array<unsigned char,120> raw{};
    put(raw,0,0x10203040,4);put(raw,4,0x55667788,4);put(raw,8,S_IFREG|0640,2);put(raw,10,3,2);
    put(raw,12,1001,4);put(raw,16,1002,4);put(raw,20,9,4);
    put(raw,24,1710000000,8);put(raw,32,123456789,8);
    put(raw,40,1710000001,8);put(raw,48,987654321,8);
    put(raw,56,1710000002,8);put(raw,64,24680,8);
    constexpr uint64_t large_size=6ull*1024*1024*1024+37;
    put(raw,72,large_size,8);put(raw,80,17000000,8);put(raw,88,4096,4);put(raw,92,2,4);put(raw,96,3,4);
    put(raw,104,1700000000,8);put(raw,112,13579,8);
    struct stat output{};REQUIRE(mc_ps4_decode_stat(raw.data(),raw.size(),&output)==0);
    REQUIRE(output.st_dev==0x10203040);REQUIRE(output.st_ino==0x55667788);REQUIRE(output.st_mode==(S_IFREG|0640));
    REQUIRE(output.st_nlink==3);REQUIRE(output.st_uid==1001);REQUIRE(output.st_gid==1002);REQUIRE(output.st_rdev==9);
    REQUIRE(uint64_t(output.st_size)==large_size);REQUIRE(output.st_blocks==17000000);REQUIRE(output.st_blksize==4096);
    REQUIRE(output.st_atime==1710000000);REQUIRE(output.st_mtime==1710000001);REQUIRE(output.st_ctime==1710000002);
    REQUIRE(output.st_mtimespec.tv_nsec==987654321);REQUIRE(output.st_birthtimespec.tv_sec==1700000000);
    REQUIRE(output.st_birthtimespec.tv_nsec==13579);REQUIRE(output.st_flags==2);REQUIRE(output.st_gen==3);
    auto original=output;errno=0;
    REQUIRE(mc_ps4_decode_stat(raw.data(),119,&output)==-1);REQUIRE(errno==EINVAL);
    REQUIRE(std::memcmp(&output,&original,sizeof(output))==0);
    REQUIRE(mc_ps4_decode_stat(nullptr,120,&output)==-1);REQUIRE(mc_ps4_decode_stat(raw.data(),120,nullptr)==-1);
}

void kernel_statfs_preserves_capacity_and_mount_strings() {
    std::array<unsigned char,472> raw{};
    put(raw,0,0x20030518,4);put(raw,4,0x35,4);put(raw,8,1,8);put(raw,16,4096,8);put(raw,24,65536,8);
    constexpr uint64_t blocks=(1ull<<26)+17;
    put(raw,32,blocks,8);put(raw,40,1234567,8);put(raw,48,1234000,8);put(raw,56,8000,8);put(raw,64,3000,8);
    put(raw,188,1001,4);put(raw,192,0x11223344,4);put(raw,196,0x55667788,4);
    std::memset(raw.data()+200,'X',80);
    std::memcpy(raw.data()+280,"exfat",6);std::memcpy(raw.data()+296,"/dev/da0s1",11);
    std::memcpy(raw.data()+384,"/mnt/usb0",10);
    struct statfs output{};REQUIRE(mc_ps4_decode_statfs(raw.data(),raw.size(),&output)==0);
    REQUIRE(output.f_bsize==4096);REQUIRE(output.f_iosize==65536);REQUIRE(output.f_blocks==blocks);
    REQUIRE(output.f_bfree==1234567);REQUIRE(output.f_bavail==1234000);REQUIRE(output.f_files==8000);REQUIRE(output.f_ffree==3000);
    REQUIRE(std::string(output.f_fstypename)=="exfat");REQUIRE(std::string(output.f_mntfromname)=="/dev/da0s1");
    REQUIRE(std::string(output.f_mntonname)=="/mnt/usb0");REQUIRE(output.f_owner==1001);
    int32_t identifier[2]{};std::memcpy(identifier,&output.f_fsid,sizeof(identifier));
    REQUIRE(identifier[0]==0x11223344);REQUIRE(identifier[1]==0x55667788);
    put(raw,48,static_cast<uint64_t>(-3ll),8);
    std::memset(raw.data()+280,'F',16);std::memset(raw.data()+384,'M',88);
    REQUIRE(mc_ps4_decode_statfs(raw.data(),raw.size(),&output)==0);REQUIRE(output.f_bavail==-3);
    REQUIRE(std::strlen(output.f_fstypename)==15);REQUIRE(std::strlen(output.f_mntonname)==87);
    auto original=output;errno=0;
    REQUIRE(mc_ps4_decode_statfs(raw.data(),471,&output)==-1);REQUIRE(errno==EINVAL);
    REQUIRE(std::memcmp(&output,&original,sizeof(output))==0);
}

void native_errors_become_posix_errors() {
    REQUIRE(mc_ps4_posix_result(0)==0);
    REQUIRE(mc_ps4_posix_result(static_cast<int32_t>(0x80020002u))==-1);REQUIRE(errno==ENOENT);
    REQUIRE(mc_ps4_posix_result(static_cast<int32_t>(0x8002003cu))==-1);REQUIRE(errno==ETIMEDOUT);
    errno=EACCES;REQUIRE(mc_ps4_posix_result(-1)==-1);REQUIRE(errno==EACCES);
    REQUIRE(mc_ps4_posix_result(static_cast<int32_t>(0x80030002u))==-1);REQUIRE(errno==EIO);
}

void native_open_preserves_descriptors_and_reports_real_errors() {
    errno=EACCES;
    REQUIRE(mc_ps4_fd_result(0)==0);REQUIRE(errno==EACCES);
    REQUIRE(mc_ps4_fd_result(37)==37);REQUIRE(errno==EACCES);
    REQUIRE(mc_ps4_fd_result(INT32_MAX)==INT32_MAX);
    errno=EBADF;
    REQUIRE(mc_ps4_fd_result(static_cast<int32_t>(0x80020002u))==-1);REQUIRE(errno==ENOENT);
    REQUIRE(mc_ps4_fd_result(static_cast<int32_t>(0x8002000du))==-1);REQUIRE(errno==EACCES);
    errno=ENOTDIR;REQUIRE(mc_ps4_fd_result(-1)==-1);REQUIRE(errno==ENOTDIR);
    REQUIRE(mc_ps4_fd_result(-2)==-1);REQUIRE(errno==EIO);
}
}

int main(int argc,char** argv) {
    const std::pair<const char*,std::function<void()>> tests[]={
        {"ordinary_timeouts_return_without_terminating",ordinary_timeouts_return_without_terminating},
        {"notify_one_interrupts_a_timed_worker_wait",notify_one_interrupts_a_timed_worker_wait},
        {"notify_all_cancels_and_joins_sleeping_workers",notify_all_cancels_and_joins_sleeping_workers},
        {"kernel_stat_offsets_preserve_large_sizes_and_timestamps",kernel_stat_offsets_preserve_large_sizes_and_timestamps},
        {"kernel_statfs_preserves_capacity_and_mount_strings",kernel_statfs_preserves_capacity_and_mount_strings},
        {"native_errors_become_posix_errors",native_errors_become_posix_errors},
        {"native_open_preserves_descriptors_and_reports_real_errors",native_open_preserves_descriptors_and_reports_real_errors}
    };
    std::string root=argc>1?argv[1]:".";
    std::ofstream report(root+"/reports/ps4-runtime-tests.json");
    report<<"{\n  \"target\": \"computer / PS4 compatibility code\",\n  \"hardware_tested\": false,\n  \"timeout_errno\": "<<ETIMEDOUT<<",\n  \"tests\": [\n";
    unsigned failed=0;
    for (size_t i=0;i<std::size(tests);++i) {
        unsigned before=assertions;std::string error;
        try { tests[i].second(); } catch (const std::exception& exception) { error=exception.what();++failed; }
        std::printf("%s %s%s%s\n",error.empty()?"PASS":"FAIL",tests[i].first,error.empty()?"":" — ",error.c_str());
        report<<"    {\"name\": \""<<tests[i].first<<"\", \"passed\": "<<(error.empty()?"true":"false")
            <<", \"assertions\": "<<assertions-before<<", \"error\": \""<<error<<"\"}"<<(i+1==std::size(tests)?"\n":",\n");
    }
    report<<"  ],\n  \"passed\": "<<(failed?"false":"true")<<",\n  \"assertions\": "<<assertions<<",\n  \"failed\": "<<failed<<"\n}\n";
    std::printf("%zu tests, %u assertions, %u failures\n",std::size(tests),assertions,failed);
    return failed?1:0;
}
