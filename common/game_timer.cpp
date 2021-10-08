#include "game_timer.h"
#include <windows.h>

GameTimer::GameTimer () :
    stop_time_(0), delta_time_(-1.0), base_time_(0),
    paused_time_(0), prev_time_(0), curr_time_(0), stopped_(false)
{
    __int64 count_per_sec;
    QueryPerformanceFrequency((LARGE_INTEGER *)&count_per_sec);
    sec_per_count_ = 1.0 / (double)count_per_sec;
}
//
// -- total time since last Reset() call not including paused time
float GameTimer::TotalTime () const {
    // NOTE(omid): Remember to subtract previous paused time(s) 
    if (stopped_)
        return (float)(((stop_time_ - paused_time_) - base_time_) * sec_per_count_);
    else
        return (float)(((curr_time_ - paused_time_) - base_time_) * sec_per_count_);
}

float GameTimer::DeltaTime () const { return (float)delta_time_; }

void GameTimer::Reset () {
    __int64 curr_time;  // -- in counts
    QueryPerformanceCounter((LARGE_INTEGER *)&curr_time);

    base_time_ = curr_time;
    prev_time_ = curr_time;
    stop_time_ = 0;
    stopped_ = false;
}
void GameTimer::Start () {
    __int64 start_time;  // -- in counts
    QueryPerformanceCounter((LARGE_INTEGER *)&start_time);

    // -- accumulate paused time
    if (stopped_) {
        paused_time_ += (start_time - stop_time_);
        prev_time_ = start_time;
        stop_time_ = 0;
        stopped_ = false;
    }
}
void GameTimer::Stop () {
    if (!stopped_) {
        __int64 curr_time;  // -- in counts
        QueryPerformanceCounter((LARGE_INTEGER *)&curr_time);

        stop_time_ = curr_time;
        stopped_ = true;
    }
}
void GameTimer::Tick () {
    if (stopped_) {
        delta_time_ = 0.0;
        return;
    }
    __int64 curr_time;  // -- in counts
    QueryPerformanceCounter((LARGE_INTEGER *)&curr_time);
    curr_time_ = curr_time;

    delta_time_ = (curr_time_ - prev_time_) * sec_per_count_;
    prev_time_ = curr_time_;

    // -- avoid negative cases (processors shuffling, power save mode issues, etc.)
    if (delta_time_ < 0.0)
        delta_time_ = 0.0;
}

