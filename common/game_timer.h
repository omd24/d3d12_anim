#pragma once

class GameTimer {
private:
    double sec_per_count_;
    double delta_time_;     // -- in seconds

    __int64 base_time_;     // -- time since last Reset() call, in counts
    __int64 paused_time_;   // -- accumulated paused time, in counts
    __int64 stop_time_;     // -- in counts
    __int64 prev_time_;     // -- last start or tick time (used in delta_time calculations), in counts
    __int64 curr_time_;     // -- in counts

    bool stopped_;
public:
    GameTimer ();

    // -- total time since last Reset() call not including paused time
    float TotalTime () const;   // -- in seconds
    float DeltaTime () const;   // -- in seconds

    void Reset ();  // -- call before main msg loop
    void Start ();  // -- call when unpaused
    void Stop ();   // -- call to pause
    void Tick ();   // -- call every frame
};

