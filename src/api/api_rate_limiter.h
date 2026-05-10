#pragma once
#include <mutex>
#include <chrono>
#include <thread>
#include <algorithm>
#include <atomic>

/*
 * Token-bucket rate limiter for GW2 API calls.
 *
 * GW2 API limit: ~600 req/min per IP.  We target ≤5 req/sec (300/min) as
 * recommended by the Best Practices wiki, using a 30-token burst bucket.
 * All HTTP calls go through WaitAndAcquire() — this enforces the limit
 * globally regardless of which subsystem fires the request.
 *
 * UI layer reads g_InFlight / IsThrottled() to grey out interactive controls.
 */
namespace APIRateLimit {

inline std::atomic<int>  g_InFlight{0};    /* requests currently in progress */

namespace detail {
    inline std::mutex     g_mutex;
    inline double         g_tokens     = 30.0;
    inline std::chrono::steady_clock::time_point g_last_refill =
        std::chrono::steady_clock::now();

    constexpr double FILL_RATE  = 5.0;   /* tokens per second */
    constexpr double CAPACITY   = 30.0;  /* burst ceiling */
    constexpr double LOW_MARK   = 10.0;  /* "throttled" threshold for the UI */
}

/* Refill the bucket from elapsed time.  Must hold detail::g_mutex. */
inline void _Refill()
{
    using namespace std::chrono;
    auto now = steady_clock::now();
    double elapsed = duration_cast<duration<double>>(
        now - detail::g_last_refill).count();
    detail::g_tokens = std::min(detail::CAPACITY,
        detail::g_tokens + elapsed * detail::FILL_RATE);
    detail::g_last_refill = now;
}

/* Block the calling thread until a token is available (or 10-second timeout).
 * Call this at the top of every background HTTP request. */
inline void WaitAndAcquire()
{
    using namespace std::chrono;
    auto deadline = steady_clock::now() + seconds(10);

    while (steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lk(detail::g_mutex);
            _Refill();
            if (detail::g_tokens >= 1.0) {
                detail::g_tokens -= 1.0;
                return;
            }
        }
        std::this_thread::sleep_for(milliseconds(100));
    }
    /* Timed out — proceed anyway; network timeouts will self-regulate */
}

/* True when the bucket is getting low.  Checked by the UI to grey out
 * controls and show the "limited" status message. */
inline bool IsThrottled()
{
    std::lock_guard<std::mutex> lk(detail::g_mutex);
    _Refill();
    return detail::g_tokens < detail::LOW_MARK;
}

/* Convenience: UI should grey out / show message when either condition is true. */
inline bool ShouldShowLimitedMessage()
{
    return g_InFlight.load() > 0 || IsThrottled();
}

} /* namespace APIRateLimit */
