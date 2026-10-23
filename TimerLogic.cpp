#include "TimerLogic.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace stimer
{
namespace
{
std::string Trim( const std::string& s )
{
	size_t a = 0, b = s.size();
	while( a < b && std::isspace( (unsigned char)s[ a ] ) )
		++a;
	while( b > a && std::isspace( (unsigned char)s[ b - 1 ] ) )
		--b;
	return s.substr( a, b - a );
}

std::string Lower( std::string s )
{
	for( auto& c : s )
		c = (char)std::tolower( (unsigned char)c );
	return s;
}

bool ParseNumber( const std::string& s, double& v )
{
	if( s.empty() )
		return false;
	std::string t = s;
	for( auto& c : t )
		if( c == ',' )
			c = '.';// "1,5" (Russian decimal comma)
	char* end = nullptr;
	v         = std::strtod( t.c_str(), &end );
	return end && *end == '\0' && std::isfinite( v ) && v >= 0.0;
}

// "1:30:00" style. Seconds part may have a fraction.
bool ParseColon( const std::string& s, double& out )
{
	std::vector< std::string > parts;
	size_t start = 0;
	for( size_t i = 0; i <= s.size(); ++i )
	{
		if( i == s.size() || s[ i ] == ':' )
		{
			parts.push_back( Trim( s.substr( start, i - start ) ) );
			start = i + 1;
		}
	}
	if( parts.empty() || parts.size() > 3 )
		return false;
	double total = 0.0;
	for( size_t i = 0; i < parts.size(); ++i )
	{
		double v;
		if( !ParseNumber( parts[ i ], v ) )
			return false;
		// only the last component may be fractional
		if( i + 1 < parts.size() && v != std::floor( v ) )
			return false;
		total = total * 60.0 + v;
	}
	out = total;
	return true;
}

// "1h30m", "90s", "2m 30s", "1.5h"
bool ParseUnits( const std::string& s, double& out )
{
	double total = 0.0;
	bool any     = false;
	size_t i     = 0;
	while( i < s.size() )
	{
		while( i < s.size() && std::isspace( (unsigned char)s[ i ] ) )
			++i;
		if( i >= s.size() )
			break;
		size_t j = i;
		while( j < s.size() && ( std::isdigit( (unsigned char)s[ j ] ) || s[ j ] == '.' || s[ j ] == ',' ) )
			++j;
		double v;
		if( j == i || !ParseNumber( s.substr( i, j - i ), v ) )
			return false;
		while( j < s.size() && std::isspace( (unsigned char)s[ j ] ) )
			++j;
		size_t k = j;
		while( k < s.size() && std::isalpha( (unsigned char)s[ k ] ) )
			++k;
		const std::string unit = s.substr( j, k - j );
		double mul             = 0.0;
		if( unit == "h" || unit == "hr" || unit == "hrs" || unit == "hour" || unit == "hours" )
			mul = 3600.0;
		else if( unit == "m" || unit == "min" || unit == "mins" || unit == "minute" || unit == "minutes" )
			mul = 60.0;
		else if( unit == "s" || unit == "sec" || unit == "secs" || unit == "second" || unit == "seconds" )
			mul = 1.0;
		else
			return false;
		total += v * mul;
		any = true;
		i   = k;
	}
	out = total;
	return any;
}
}// namespace

bool ParseDuration( const std::string& text, double& seconds )
{
	std::string s = Lower( Trim( text ) );
	// Russian unit letters -> latin (ч/мин/м/сек/с), UTF-8
	struct Rep
	{
		const char* from;
		const char* to;
	};
	static const Rep reps[] = { { "\xD1\x87", "h" }, { "\xD0\xBC\xD0\xB8\xD0\xBD", "m" }, { "\xD0\xBC", "m" },
		                        { "\xD1\x81\xD0\xB5\xD0\xBA", "s" }, { "\xD1\x81", "s" } };
	for( const auto& r : reps )
	{
		size_t pos;
		while( ( pos = s.find( r.from ) ) != std::string::npos )
			s.replace( pos, std::string( r.from ).size(), r.to );
	}
	if( s.empty() )
		return false;
	double v = 0.0;
	bool ok  = false;
	if( s.find_first_of( "hms" ) != std::string::npos )
		ok = ParseUnits( s, v );
	else
		ok = ParseColon( s, v );
	if( !ok )
		return false;
	seconds = v;
	return true;
}

bool ParseTimeOfDay( const std::string& text, double& secondsOfDay )
{
	std::string s = Lower( Trim( text ) );
	int ampm      = 0;
	auto endsWith = [ & ]( const char* suf ) {
		std::string x( suf );
		return s.size() >= x.size() && s.compare( s.size() - x.size(), x.size(), x ) == 0;
	};
	if( endsWith( "pm" ) || endsWith( "am" ) )
	{
		ampm = endsWith( "pm" ) ? 2 : 1;
		s    = Trim( s.substr( 0, s.size() - 2 ) );
	}
	if( s.find( ':' ) == std::string::npos )
	{
		// "19" -> 19:00, "1930" -> 19:30
		double n;
		if( !ParseNumber( s, n ) || n != std::floor( n ) )
			return false;
		if( n >= 100 )
			s = std::to_string( (int)n / 100 ) + ":" + std::to_string( (int)n % 100 );
		else
			s += ":00";
	}
	std::vector< double > parts;
	size_t start = 0;
	for( size_t i = 0; i <= s.size(); ++i )
	{
		if( i == s.size() || s[ i ] == ':' )
		{
			double v;
			if( !ParseNumber( Trim( s.substr( start, i - start ) ), v ) )
				return false;
			parts.push_back( v );
			start = i + 1;
		}
	}
	if( parts.size() < 2 || parts.size() > 3 )
		return false;
	double h = parts[ 0 ], m = parts[ 1 ], sec = parts.size() > 2 ? parts[ 2 ] : 0.0;
	if( ampm )
	{
		if( h < 1 || h > 12 )
			return false;
		if( ampm == 1 && h == 12 )
			h = 0;
		if( ampm == 2 && h != 12 )
			h += 12;
	}
	if( h > 24 || m >= 60 || sec >= 60 || ( h == 24 && ( m > 0 || sec > 0 ) ) )
		return false;
	secondsOfDay = std::fmod( h * 3600.0 + m * 60.0 + sec, 86400.0 );
	return true;
}

std::string FormatTime( double seconds, Format format, int decimals, bool roundUp )
{
	decimals              = decimals < 0 ? 0 : decimals > 2 ? 2 : decimals;
	const double unit     = decimals == 0 ? 1.0 : decimals == 1 ? 0.1 : 0.01;
	const long long scale = decimals == 0 ? 1 : decimals == 1 ? 10 : 100;

	// Round in display units; tiny epsilon so 4.9999999 doesn't flicker.
	double q          = seconds / unit;
	long long ticks   = (long long)( roundUp ? std::ceil( q - 1e-6 ) : std::floor( q + 1e-6 ) );
	const bool neg    = ticks < 0;
	long long a       = neg ? -ticks : ticks;
	long long frac    = a % scale;
	long long whole   = a / scale;
	long long h       = whole / 3600;
	long long m       = ( whole / 60 ) % 60;
	long long s       = whole % 60;
	long long totalM  = whole / 60;

	char buf[ 64 ];
	switch( format )
	{
	case Format::HMS: std::snprintf( buf, sizeof( buf ), "%02lld:%02lld:%02lld", h, m, s ); break;
	case Format::MS: std::snprintf( buf, sizeof( buf ), "%02lld:%02lld", totalM, s ); break;
	case Format::Seconds: std::snprintf( buf, sizeof( buf ), "%lld", whole ); break;
	case Format::HM: std::snprintf( buf, sizeof( buf ), "%02lld:%02lld", h, m ); break;
	case Format::Auto:
	default:
		if( h > 0 )
			std::snprintf( buf, sizeof( buf ), "%lld:%02lld:%02lld", h, m, s );
		else
			std::snprintf( buf, sizeof( buf ), "%02lld:%02lld", m, s );
		break;
	}
	std::string out = buf;
	if( decimals > 0 && format != Format::HM )
	{
		char f[ 8 ];
		std::snprintf( f, sizeof( f ), decimals == 1 ? ".%01lld" : ".%02lld", frac );
		out += f;
	}
	return neg ? "-" + out : out;
}

Snapshot TimerState::Update( double dt, Mode mode, double from, bool fromValid, double to, bool toValid, EndAction end,
                             double warnAt, double critAt, double now )
{
	Snapshot snap;
	if( running && dt > 0.0 )
		elapsed += dt;

	auto phaseFor = [ & ]( double remaining ) {
		if( remaining < 0.0 )
			return Phase::Over;
		if( critAt > 0.0 && remaining <= critAt )
			return Phase::Critical;
		if( warnAt > 0.0 && remaining <= warnAt )
			return Phase::Warning;
		return Phase::Normal;
	};

	if( mode == Mode::Clock )
	{
		snap.displayValue = now;
		snap.countingDown = false;
		snap.remaining    = 0.0;
		return snap;
	}

	if( mode == Mode::UntilTime )
	{
		if( !toValid )
		{
			snap.valid = false;
			return snap;
		}
		double r = to - now;
		// Passed less than 12 h ago -> it's over (overtime); longer ago -> it means tomorrow.
		if( r <= -43200.0 )
			r += 86400.0;
		if( end == EndAction::Loop && r < 0.0 )
			r += 86400.0;
		snap.countingDown = true;
		snap.remaining    = r;
		if( r <= 0.0 && ( end == EndAction::Stop || end == EndAction::StopFlash ) )
		{
			snap.displayValue = 0.0;
			snap.finished     = true;
			snap.phase        = end == EndAction::StopFlash ? Phase::Over : phaseFor( 0.0 );
		}
		else
		{
			snap.displayValue = r;
			snap.phase        = phaseFor( r );
		}
		return snap;
	}

	// --- Timer: From -> To
	if( !fromValid || !toValid )
	{
		snap.valid = false;
		return snap;
	}
	const double duration = std::fabs( to - from );
	const double dir      = to < from ? -1.0 : 1.0;
	snap.countingDown     = to < from;

	if( end == EndAction::Loop && duration > 0.0 && elapsed >= duration )
		elapsed = std::fmod( elapsed, duration );

	double e = elapsed;
	if( e >= duration && end != EndAction::Continue && end != EndAction::Loop )
	{
		e = duration;
		if( !finished )
		{
			finished = true;
			if( running )
			{
				running     = false;
				autoStopped = true;
			}
		}
		elapsed = duration;
	}
	else if( e < duration )
	{
		finished = false;
	}

	snap.finished     = finished;
	snap.displayValue = from + dir * e;
	snap.remaining    = duration - e;
	snap.progress     = duration > 0.0 ? std::min( 1.0, std::max( 0.0, e / duration ) ) : 1.0;

	if( finished )
		snap.phase = end == EndAction::StopFlash ? Phase::Over : phaseFor( 0.0 );
	else
		snap.phase = phaseFor( snap.remaining );
	return snap;
}

}// namespace stimer
