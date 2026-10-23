// Unit tests for the timer logic (no GPU needed).
#include "TimerLogic.h"
#include <cmath>
#include <cstdio>
#include <string>

using namespace stimer;

static int failures = 0;
#define CHECK( cond )                                                    \
	do                                                                   \
	{                                                                    \
		if( !( cond ) )                                                  \
		{                                                                \
			std::printf( "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond ); \
			++failures;                                                  \
		}                                                                \
	} while( 0 )
#define CHECK_STR( a, b )                                                                               \
	do                                                                                                  \
	{                                                                                                   \
		std::string _a = ( a ), _b = ( b );                                                            \
		if( _a != _b )                                                                                  \
		{                                                                                               \
			std::printf( "FAIL %s:%d  '%s' != '%s'\n", __FILE__, __LINE__, _a.c_str(), _b.c_str() ); \
			++failures;                                                                                 \
		}                                                                                               \
	} while( 0 )

static double D( const char* s )
{
	double v = -1;
	return ParseDuration( s, v ) ? v : -1;
}
static double T( const char* s )
{
	double v = -1;
	return ParseTimeOfDay( s, v ) ? v : -1;
}

struct Sim
{
	TimerState t;
	Mode mode = Mode::Timer;
	double from, to;
	EndAction end;
	double warn = 60, crit = 10;
	Snapshot step( double dt, double tod = 0 ) { return t.Update( dt, mode, from, true, to, true, end, warn, crit, tod ); }
	std::string show( double dt, Format f = Format::Auto, int dec = 0 )
	{
		Snapshot s = step( dt );
		return FormatTime( s.displayValue, f, dec, s.countingDown );
	}
};

int main()
{
	// --- parsing
	CHECK( D( "00:05:00" ) == 300 );
	CHECK( D( "5:00" ) == 300 );
	CHECK( D( "90" ) == 90 );
	CHECK( D( "1:30:00" ) == 5400 );
	CHECK( D( "1:30.5" ) == 90.5 );
	CHECK( D( "1:30,5" ) == 90.5 );
	CHECK( D( "1h30m" ) == 5400 );
	CHECK( D( "2m 30s" ) == 150 );
	CHECK( D( "90s" ) == 90 );
	CHECK( D( "1.5h" ) == 5400 );
	CHECK( D( "5 мин" ) == 300 );
	CHECK( D( "1ч 30м" ) == 5400 );
	CHECK( D( "" ) == -1 );
	CHECK( D( "abc" ) == -1 );
	CHECK( D( "1:2:3:4" ) == -1 );
	CHECK( D( "1.5:00" ) == -1 );
	CHECK( T( "19:30" ) == 19 * 3600 + 30 * 60 );
	CHECK( T( "7:05 pm" ) == 19 * 3600 + 5 * 60 );
	CHECK( T( "12:00 am" ) == 0 );
	CHECK( T( "1930" ) == 19 * 3600 + 30 * 60 );
	CHECK( T( "19" ) == 19 * 3600 );
	CHECK( T( "25:00" ) == -1 );
	CHECK( T( "19:75" ) == -1 );

	// --- formatting
	CHECK_STR( FormatTime( 300, Format::Auto, 0, true ), "05:00" );
	CHECK_STR( FormatTime( 3900, Format::Auto, 0, true ), "1:05:00" );
	CHECK_STR( FormatTime( 3900, Format::HMS, 0, true ), "01:05:00" );
	CHECK_STR( FormatTime( 3900, Format::MS, 0, true ), "65:00" );
	CHECK_STR( FormatTime( 3900, Format::Seconds, 0, true ), "3900" );
	CHECK_STR( FormatTime( 3900, Format::HM, 0, true ), "01:05" );
	CHECK_STR( FormatTime( 299.2, Format::Auto, 0, true ), "05:00" );  // countdown rounds up
	CHECK_STR( FormatTime( 299.2, Format::Auto, 0, false ), "04:59" ); // count-up rounds down
	CHECK_STR( FormatTime( 299.25, Format::Auto, 1, true ), "04:59.3" );
	CHECK_STR( FormatTime( 299.25, Format::Auto, 2, false ), "04:59.25" );
	CHECK_STR( FormatTime( -12.0, Format::Auto, 0, true ), "-00:12" );
	CHECK_STR( FormatTime( 0.0, Format::Auto, 0, true ), "00:00" );

	// --- countdown 5:00 -> 0:00, Stop
	{
		Sim s{ {}, Mode::Timer, 300, 0, EndAction::Stop };
		CHECK_STR( s.show( 0 ), "05:00" );
		s.t.Start();
		CHECK_STR( s.show( 0.5 ), "05:00" );
		CHECK_STR( s.show( 0.5 ), "04:59" );
		CHECK_STR( s.show( 238.0 ), "01:01" );
		CHECK( s.step( 0 ).phase == Phase::Normal );
		CHECK( s.step( 1.0 ).phase == Phase::Warning );// 1:00 left
		CHECK( s.step( 50.0 ).phase == Phase::Critical );// 0:10 left
		CHECK( s.t.IsRunning() );
		Snapshot e = s.step( 100.0 );
		CHECK( e.finished );
		CHECK( !s.t.IsRunning() );
		CHECK( s.t.ConsumeAutoStopped() );
		CHECK_STR( FormatTime( e.displayValue, Format::Auto, 0, true ), "00:00" );
		s.t.Reset();
		CHECK_STR( s.show( 0 ), "05:00" );
	}

	// --- count-up 0 -> 5:00 (the same plugin with From/To swapped)
	{
		Sim s{ {}, Mode::Timer, 0, 300, EndAction::Stop };
		s.t.Start();
		CHECK( !s.step( 0 ).countingDown );
		CHECK_STR( s.show( 0.9 ), "00:00" );
		CHECK_STR( s.show( 0.1 ), "00:01" );
		CHECK( s.step( 240 ).phase == Phase::Warning );// 59 s left to the target
		Snapshot e = s.step( 1000 );
		CHECK( e.finished );
		CHECK_STR( FormatTime( e.displayValue, Format::Auto, 0, false ), "05:00" );
	}

	// --- arbitrary range: 10:00 -> 7:30 counts down 2:30
	{
		Sim s{ {}, Mode::Timer, 600, 450, EndAction::Stop };
		s.t.Start();
		CHECK_STR( s.show( 60 ), "09:00" );
		CHECK( std::fabs( s.step( 0 ).progress - 0.4 ) < 1e-9 );
		CHECK_STR( s.show( 1000 ), "07:30" );
	}

	// --- Continue: overtime goes negative
	{
		Sim s{ {}, Mode::Timer, 60, 0, EndAction::Continue };
		s.t.Start();
		Snapshot o = s.step( 72 );
		CHECK( !o.finished );
		CHECK( o.phase == Phase::Over );
		CHECK( s.t.IsRunning() );
		CHECK_STR( FormatTime( o.displayValue, Format::Auto, 0, true ), "-00:12" );
	}

	// --- Loop
	{
		Sim s{ {}, Mode::Timer, 10, 0, EndAction::Loop };
		s.t.Start();
		CHECK_STR( s.show( 13 ), "00:07" );
		CHECK( s.t.IsRunning() );
	}

	// --- Stop & Flash
	{
		Sim s{ {}, Mode::Timer, 10, 0, EndAction::StopFlash };
		s.t.Start();
		Snapshot e = s.step( 11 );
		CHECK( e.finished && e.phase == Phase::Over );
	}

	// --- pause keeps time, +1 min / -1 min
	{
		Sim s{ {}, Mode::Timer, 300, 0, EndAction::Stop };
		s.t.Start();
		s.step( 30 );
		s.t.Pause();
		CHECK_STR( s.show( 100 ), "04:30" );
		s.t.AdjustRemaining( 60 );
		CHECK_STR( s.show( 0 ), "05:30" );
		s.t.AdjustRemaining( -120 );
		CHECK_STR( s.show( 0 ), "03:30" );
	}

	// --- until time of day
	{
		Sim s{ {}, Mode::UntilTime, 0, 19 * 3600 + 30 * 60, EndAction::Stop };
		Snapshot a = s.step( 0, 19 * 3600 + 25 * 60 );
		CHECK_STR( FormatTime( a.displayValue, Format::Auto, 0, true ), "05:00" );
		Snapshot b = s.step( 0, 8 * 3600 );// 11:30 to go
		CHECK_STR( FormatTime( b.displayValue, Format::Auto, 0, true ), "11:30:00" );
		Snapshot c = s.step( 0, 19 * 3600 + 35 * 60 );// passed 5 min ago -> stopped at 0
		CHECK( c.finished );
		CHECK_STR( FormatTime( c.displayValue, Format::Auto, 0, true ), "00:00" );
		s.end      = EndAction::Continue;
		Snapshot d = s.step( 0, 19 * 3600 + 35 * 60 );
		CHECK_STR( FormatTime( d.displayValue, Format::Auto, 0, true ), "-05:00" );
		s.to       = 30 * 60;// 00:30, seen at 23:50 -> 40 min (tomorrow)
		Snapshot e = s.step( 0, 23 * 3600 + 50 * 60 );
		CHECK_STR( FormatTime( e.displayValue, Format::Auto, 0, true ), "40:00" );
	}

	// --- clock
	{
		Sim s{ {}, Mode::Clock, 0, 0, EndAction::Stop };
		Snapshot c = s.step( 0, 9 * 3600 + 5 * 60 + 7.6 );
		CHECK_STR( FormatTime( c.displayValue, Format::HMS, 0, false ), "09:05:07" );
	}

	std::printf( failures ? "%d FAILED\n" : "all tests passed\n", failures );
	return failures ? 1 : 0;
}
