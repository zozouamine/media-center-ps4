// The installed libc++ archive compares pthread timeouts with Linux errno 110.
// Rebuild its wait methods using the target headers (FreeBSD ETIMEDOUT = 60).
// Supply every wait/notification method used by this application. The link audit
// verifies that the incompatible archive member is excluded.
#if defined(MC_PS4) || defined(MC_VALIDATE_PS4_RUNTIME)
#include <cerrno>
#include <condition_variable>
#include <limits>
#include <system_error>

_LIBCPP_BEGIN_NAMESPACE_STD
void condition_variable::notify_one() _NOEXCEPT { __libcpp_condvar_signal(&__cv_); }
void condition_variable::notify_all() _NOEXCEPT { __libcpp_condvar_broadcast(&__cv_); }
void condition_variable::wait(unique_lock<mutex>& lock) _NOEXCEPT {
    if (!lock.owns_lock()) __throw_system_error(EPERM,"Condition wait requires a locked mutex");
    int error=__libcpp_condvar_wait(&__cv_,lock.mutex()->native_handle());
    if (error) __throw_system_error(error,"Condition wait failed");
}
void condition_variable::__do_timed_wait(unique_lock<mutex>& lock,
    chrono::time_point<chrono::system_clock,chrono::nanoseconds> deadline) _NOEXCEPT {
    if (!lock.owns_lock()) __throw_system_error(EPERM,"Timed wait requires a locked mutex");
    auto duration=deadline.time_since_epoch();
    auto seconds=chrono::duration_cast<chrono::seconds>(duration);
    auto fraction=duration-seconds;
    if (fraction.count()<0) { seconds-=chrono::seconds(1);fraction+=chrono::seconds(1); }
    timespec time{};
    const auto limit=numeric_limits<decltype(time.tv_sec)>::max();
    if (seconds.count()>=limit) { time.tv_sec=limit;time.tv_nsec=999999999; }
    else {
        time.tv_sec=static_cast<decltype(time.tv_sec)>(seconds.count());
        time.tv_nsec=static_cast<decltype(time.tv_nsec)>(fraction.count());
    }
    int error=__libcpp_condvar_timedwait(&__cv_,lock.mutex()->native_handle(),&time);
    if (error && error!=ETIMEDOUT) __throw_system_error(error,"Timed condition wait failed");
}
_LIBCPP_END_NAMESPACE_STD
#endif
