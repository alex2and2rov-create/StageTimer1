#pragma once
// Pure timer logic: parsing, state machine and formatting.
// No OpenGL / FFGL here so it can be unit-tested on its own.

#include <string>

namespace stimer
{
enum class Mode
{
	Timer,      // From -> To (counts down if From > To, up otherwise)
	UntilTime,  // counts down to the clock time given in "To" (e.g. 19:30)
	Clock       // shows the current time of day
};

enum class EndAction
{
	Stop,      // stop at "To"
	StopFlash, // stop at "To" and flash
	Loop,      // start over from "From"
	Continue   // keep counting past "To" (overtime)
};

enum class Format
{
	Auto,   // 1:05:00 when there are hours, otherwise 05:00
	HMS,    // 01:05:00
	MS,     // 65:00 (minutes may exceed 59)
	Seconds,// 3900
	HM      // 01:05
};

enum class Phase
{
	Normal,
	Warning,
	Critical,
	Over// finished (Stop & Flash) or past "To" (Continue)
};

// Parses "1:30:00", "90:00", "45", "1:30.5", "1h30m", "90s", "2m 30s".
// Returns false for empty / invalid input. Result in seconds (may be fractional).
bool ParseDuration( const std::string& text, double& seconds );

// Parses a time of day "19:30", "19:30:15", "7:05 pm". Result: seconds after midnight.
bool ParseTimeOfDay( const std::string& text, double& secondsOfDay );

// Formats a signed time value. `roundUp` = show ceil (countdowns, so 0:00 appears
// exactly at the end), otherwise floor. decimals: 0, 1 or 2.
std::string FormatTime( double seconds, Format format, int decimals, bool roundUp );

struct Snapshot
{
	double displayValue = 0.0;// seconds to show (signed)
	double remaining    = 0.0;// seconds until "To" (negative = overtime)
	double progress     = 0.0;// 0..1 of the From->To span (Timer mode only)
	bool countingDown   = true;
	bool finished       = false;// reached "To" with Stop / Stop & Flash
	bool valid          = true; // false when From / To can't be parsed
	Phase phase         = Phase::Normal;
};

class TimerState
{
public:
	// Elapsed run time, advanced explicitly by the caller (keeps this class deterministic).
	void Start() { running = true; }
	void Pause() { running = false; }
	void Toggle() { running = !running; }
	void Reset()
	{
		elapsed  = 0.0;
		finished = false;
	}
	bool IsRunning() const { return running; }
	double Elapsed() const { return elapsed; }

	// Positive = more time left (moves the display back towards "From").
	void AdjustRemaining( double seconds )
	{
		elapsed -= seconds;
		finished = false;
	}

	// Advance by dt seconds of real time and compute what to display.
	// `nowSecondsOfDay` is the local wall clock (for UntilTime / Clock).
	Snapshot Update( double dt, Mode mode, double from, bool fromValid, double to, bool toValid, EndAction end,
	                 double warnAt, double critAt, double nowSecondsOfDay );

	// True once when the timer stopped by itself at the end (so the host's "Running" toggle can be updated).
	bool ConsumeAutoStopped()
	{
		bool v      = autoStopped;
		autoStopped = false;
		return v;
	}

private:
	bool running     = false;
	bool finished    = false;
	bool autoStopped = false;
	double elapsed   = 0.0;
};

}// namespace stimer
