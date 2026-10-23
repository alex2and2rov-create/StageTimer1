#include "FFGLTimer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

using namespace ffglex;
using namespace stimer;

enum ParamID : FFUInt32
{
	// Timer
	PT_MODE,
	PT_FROM,
	PT_TO,
	PT_STARTPAUSE,
	PT_START,
	PT_PAUSE,
	PT_RESET,
	PT_RUNNING,
	PT_SWAP,
	PT_ADD_MIN,
	PT_SUB_MIN,
	PT_AT_END,
	PT_RESTART_TRIGGER,
	// Display
	PT_FORMAT,
	PT_DECIMALS,
	PT_FONT,
	PT_GHOST,
	PT_BLINK_COLON,
	PT_LABEL,
	PT_LABEL_SIZE,
	PT_LABEL_POS,
	PT_SIZE,
	PT_POSX,
	PT_POSY,
	PT_SHADOW,
	// Colors
	PT_TEXT_H,
	PT_TEXT_S,
	PT_TEXT_B,
	PT_TEXT_A,
	PT_WARN_AT,
	PT_WARN_H,
	PT_WARN_S,
	PT_WARN_B,
	PT_WARN_A,
	PT_CRIT_AT,
	PT_CRIT_H,
	PT_CRIT_S,
	PT_CRIT_B,
	PT_CRIT_A,
	PT_FLASH_OVER,
	PT_BG_H,
	PT_BG_S,
	PT_BG_B,
	PT_BG_A,
	// Progress bar
	PT_BAR,
	PT_BAR_HEIGHT,
	PT_COUNT
};

static CFFGLPluginInfo PluginInfo(
	PluginFactory< FFGLTimer >,
	"STMR",                                                   // Plugin unique ID
	"Stage Timer",                                            // Plugin name
	2,                                                        // API major version
	1,                                                        // API minor version
	1,                                                        // Plugin major version
	0,                                                        // Plugin minor version
	FF_SOURCE,                                                // Plugin type
	"Countdown / count-up timer: From -> To, until a time of day, or clock.",
	"Stage Timer for Resolume (FFGL)" );

static const char vertexShaderCode[] = R"(#version 410 core
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main()
{
	gl_Position = vPosition;
	uv = vUV;
}
)";

static const char fragmentShaderCode[] = R"(#version 410 core
uniform sampler2D timeTex;   // r = digits, g = ghost segments
uniform sampler2D labelTex;  // r = label
uniform vec2  resolution;
uniform vec4  timeRect;      // x, y (top-left, pixels, y down), w, h
uniform vec4  labelRect;
uniform int   hasLabel;
uniform vec4  textColor;     // straight alpha
uniform vec4  labelColor;
uniform float ghostAlpha;
uniform float shadow;
uniform float shadowOffset;  // pixels
uniform vec4  background;
uniform vec4  barRect;       // pixels; w <= 0 -> no bar
uniform float barProgress;
uniform vec4  barColor;

in vec2 uv;
out vec4 fragColor;

float sampleRect( sampler2D tex, vec4 rect, vec2 p, int ch )
{
	vec2 q = ( p - rect.xy ) / rect.zw;
	if( q.x < 0.0 || q.y < 0.0 || q.x > 1.0 || q.y > 1.0 )
		return 0.0;
	vec4 t = texture( tex, q );
	return ch == 0 ? t.r : t.g;
}

// premultiplied "src over dst"
vec4 over( vec4 src, vec4 dst ) { return src + dst * ( 1.0 - src.a ); }
vec4 premul( vec3 rgb, float a ) { return vec4( rgb * a, a ); }

void main()
{
	vec2 p = vec2( uv.x, 1.0 - uv.y ) * resolution;
	vec4 col = premul( background.rgb, background.a );

	// progress bar (track + fill)
	if( barRect.z > 0.0 )
	{
		vec2 q = ( p - barRect.xy ) / barRect.zw;
		if( q.x >= 0.0 && q.y >= 0.0 && q.x <= 1.0 && q.y <= 1.0 )
		{
			bool filled = q.x <= barProgress;
			col = over( premul( barColor.rgb, barColor.a * ( filled ? 1.0 : 0.22 ) ), col );
		}
	}

	// drop shadow
	if( shadow > 0.0 )
	{
		vec2 o = vec2( shadowOffset );
		float s = sampleRect( timeTex, timeRect, p - o, 0 );
		if( hasLabel != 0 )
			s = max( s, sampleRect( labelTex, labelRect, p - o, 0 ) );
		col = over( vec4( 0.0, 0.0, 0.0, s * shadow * max( textColor.a, labelColor.a ) ), col );
	}

	// ghost segments (unlit LCD segments)
	float g = sampleRect( timeTex, timeRect, p, 1 );
	col = over( premul( textColor.rgb, g * ghostAlpha * textColor.a ), col );

	// label
	if( hasLabel != 0 )
	{
		float l = sampleRect( labelTex, labelRect, p, 0 );
		col = over( premul( labelColor.rgb, l * labelColor.a ), col );
	}

	// digits
	float d = sampleRect( timeTex, timeRect, p, 0 );
	col = over( premul( textColor.rgb, d * textColor.a ), col );

	fragColor = col.a > 0.0 ? vec4( col.rgb / col.a, col.a ) : vec4( 0.0 );
}
)";

void FFGLTimer::AddColorParams( unsigned int first, const char* name, float h, float s, float b, float a )
{
	// Hue/Saturation/Brightness/Alpha in a row shows up as one colour picker in Resolume.
	std::string n = name;
	SetParamInfo( first + 0, n.c_str(), FF_TYPE_HUE, h );
	SetParamInfo( first + 1, ( n + "_saturation" ).c_str(), FF_TYPE_SATURATION, s );
	SetParamInfo( first + 2, ( n + "_brightness" ).c_str(), FF_TYPE_BRIGHTNESS, b );
	SetParamInfo( first + 3, ( n + "_alpha" ).c_str(), FF_TYPE_ALPHA, a );
}

FFGLTimer::FFGLTimer()
{
	SetMinInputs( 0 );
	SetMaxInputs( 0 );

	// --- Timer
	SetOptionParamInfo( PT_MODE, "Mode", 3, 0.0f );
	SetParamElementInfo( PT_MODE, 0, "Timer (From > To)", 0.0f );
	SetParamElementInfo( PT_MODE, 1, "Until Time of Day", 1.0f );
	SetParamElementInfo( PT_MODE, 2, "Clock", 2.0f );
	SetParamInfo( PT_FROM, "From", FF_TYPE_TEXT, fromText.c_str() );
	SetParamInfo( PT_TO, "To", FF_TYPE_TEXT, toText.c_str() );
	SetParamInfof( PT_STARTPAUSE, "Start/Pause", FF_TYPE_EVENT );
	SetParamInfof( PT_START, "Start", FF_TYPE_EVENT );
	SetParamInfof( PT_PAUSE, "Pause", FF_TYPE_EVENT );
	SetParamInfof( PT_RESET, "Reset", FF_TYPE_EVENT );
	SetParamInfo( PT_RUNNING, "Running", FF_TYPE_BOOLEAN, false );
	SetParamInfof( PT_SWAP, "Swap From/To", FF_TYPE_EVENT );
	SetParamInfof( PT_ADD_MIN, "+1 Min", FF_TYPE_EVENT );
	SetParamInfof( PT_SUB_MIN, "-1 Min", FF_TYPE_EVENT );
	SetOptionParamInfo( PT_AT_END, "At End", 4, 0.0f );
	SetParamElementInfo( PT_AT_END, 0, "Stop", 0.0f );
	SetParamElementInfo( PT_AT_END, 1, "Stop & Flash", 1.0f );
	SetParamElementInfo( PT_AT_END, 2, "Loop", 2.0f );
	SetParamElementInfo( PT_AT_END, 3, "Continue (overtime)", 3.0f );
	SetParamInfo( PT_RESTART_TRIGGER, "Restart On Trigger", FF_TYPE_BOOLEAN, false );

	// --- Display
	SetOptionParamInfo( PT_FORMAT, "Format", 5, 0.0f );
	SetParamElementInfo( PT_FORMAT, 0, "Auto (1:05:00 / 05:00)", 0.0f );
	SetParamElementInfo( PT_FORMAT, 1, "HH:MM:SS", 1.0f );
	SetParamElementInfo( PT_FORMAT, 2, "MM:SS", 2.0f );
	SetParamElementInfo( PT_FORMAT, 3, "Seconds", 3.0f );
	SetParamElementInfo( PT_FORMAT, 4, "HH:MM", 4.0f );
	SetOptionParamInfo( PT_DECIMALS, "Fraction", 3, 0.0f );
	SetParamElementInfo( PT_DECIMALS, 0, "None", 0.0f );
	SetParamElementInfo( PT_DECIMALS, 1, "1/10 s", 1.0f );
	SetParamElementInfo( PT_DECIMALS, 2, "1/100 s", 2.0f );
	SetOptionParamInfo( PT_FONT, "Font", 4, 0.0f );
	SetParamElementInfo( PT_FONT, 0, "Sans Bold", 0.0f );
	SetParamElementInfo( PT_FONT, 1, "Sans Light", 1.0f );
	SetParamElementInfo( PT_FONT, 2, "Mono", 2.0f );
	SetParamElementInfo( PT_FONT, 3, "Digital (7-segment)", 3.0f );
	SetParamInfo( PT_GHOST, "Ghost Segments", FF_TYPE_STANDARD, ghost );
	SetParamInfo( PT_BLINK_COLON, "Blink Colon", FF_TYPE_BOOLEAN, false );
	SetParamInfo( PT_LABEL, "Label", FF_TYPE_TEXT, "" );
	SetParamInfo( PT_LABEL_SIZE, "Label Size", FF_TYPE_STANDARD, labelSize );
	SetOptionParamInfo( PT_LABEL_POS, "Label Position", 2, 0.0f );
	SetParamElementInfo( PT_LABEL_POS, 0, "Above", 0.0f );
	SetParamElementInfo( PT_LABEL_POS, 1, "Below", 1.0f );
	SetParamInfo( PT_SIZE, "Size", FF_TYPE_STANDARD, size );
	SetParamRange( PT_SIZE, 0.05f, 1.0f );
	SetParamInfo( PT_POSX, "Position X", FF_TYPE_STANDARD, posX );
	SetParamInfo( PT_POSY, "Position Y", FF_TYPE_STANDARD, posY );
	SetParamInfo( PT_SHADOW, "Shadow", FF_TYPE_STANDARD, shadow );

	// --- Colors
	AddColorParams( PT_TEXT_H, "Text Color", textColor.h, textColor.s, textColor.b, textColor.a );
	SetParamInfo( PT_WARN_AT, "Warning At", FF_TYPE_TEXT, warnText.c_str() );
	AddColorParams( PT_WARN_H, "Warning Color", warnColor.h, warnColor.s, warnColor.b, warnColor.a );
	SetParamInfo( PT_CRIT_AT, "Critical At", FF_TYPE_TEXT, critText.c_str() );
	AddColorParams( PT_CRIT_H, "Critical Color", critColor.h, critColor.s, critColor.b, critColor.a );
	SetParamInfo( PT_FLASH_OVER, "Flash Overtime", FF_TYPE_BOOLEAN, true );
	AddColorParams( PT_BG_H, "Background", bgColor.h, bgColor.s, bgColor.b, bgColor.a );

	// --- Progress bar
	SetOptionParamInfo( PT_BAR, "Progress Bar", 4, 0.0f );
	SetParamElementInfo( PT_BAR, 0, "Off", 0.0f );
	SetParamElementInfo( PT_BAR, 1, "Under Timer", 1.0f );
	SetParamElementInfo( PT_BAR, 2, "Screen Bottom", 2.0f );
	SetParamElementInfo( PT_BAR, 3, "Screen Top", 3.0f );
	SetParamInfo( PT_BAR_HEIGHT, "Bar Height", FF_TYPE_STANDARD, barHeight );

	for( FFUInt32 id = PT_FORMAT; id <= PT_SHADOW; ++id )
		SetParamGroup( id, "Display" );
	for( FFUInt32 id = PT_TEXT_H; id <= PT_BG_A; ++id )
		SetParamGroup( id, "Colors" );
	for( FFUInt32 id = PT_BAR; id <= PT_BAR_HEIGHT; ++id )
		SetParamGroup( id, "Progress Bar" );

	UpdateVisibility( false );
}

FFResult FFGLTimer::InitGL( const FFGLViewportStruct* vp )
{
#if defined( __linux__ )
	glewInit();// Linux builds are only used for offline testing; FFGL itself only initialises GLEW on Windows
#endif
	if( !text.Ok() || !shader.Compile( vertexShaderCode, fragmentShaderCode ) || !quad.Initialise() )
	{
		DeInitGL();
		return FF_FAIL;
	}
	return CFFGLPlugin::InitGL( vp );
}

FFResult FFGLTimer::DeInitGL()
{
	shader.FreeGLResources();
	quad.Release();
	for( Tex* t : { &timeTex, &labelTex } )
	{
		if( t->id )
			glDeleteTextures( 1, &t->id );
		*t = Tex();
	}
	renderedTime.clear();
	renderedLabel.clear();
	renderedDigitPx = 0;
	return FF_SUCCESS;
}

void FFGLTimer::ToRGB( const HSBA& c, float out[ 4 ] )
{
	float h = c.h >= 1.0f ? 0.0f : c.h;
	HSVtoRGB( h, c.s, c.b, out[ 0 ], out[ 1 ], out[ 2 ] );
	out[ 3 ] = c.a;
}

double FFGLTimer::LocalSecondsOfDay() const
{
	using namespace std::chrono;
	const auto now = system_clock::now();
	std::time_t t  = system_clock::to_time_t( now );
	std::tm lt{};
#if defined( _WIN32 )
	localtime_s( &lt, &t );
#else
	localtime_r( &t, &lt );
#endif
	const double frac = duration< double >( now.time_since_epoch() ).count();
	return lt.tm_hour * 3600.0 + lt.tm_min * 60.0 + lt.tm_sec + ( frac - std::floor( frac ) );
}

void FFGLTimer::ParseInputs()
{
	fromValid = ParseDuration( fromText, fromSec );
	if( mode == (int)Mode::UntilTime )
		toValid = ParseTimeOfDay( toText, toSec );
	else
		toValid = ParseDuration( toText, toSec );
	if( !ParseDuration( warnText, warnSec ) )
		warnSec = 0;
	if( !ParseDuration( critText, critSec ) )
		critSec = 0;
}

void FFGLTimer::UpdateVisibility( bool raise )
{
	const bool timerMode = mode == (int)Mode::Timer;
	const bool clockMode = mode == (int)Mode::Clock;
	SetParamVisibility( PT_FROM, timerMode, raise );
	SetParamVisibility( PT_TO, !clockMode, raise );
	for( FFUInt32 id : { PT_STARTPAUSE, PT_START, PT_PAUSE, PT_RESET, PT_RUNNING, PT_SWAP, PT_ADD_MIN, PT_SUB_MIN, PT_RESTART_TRIGGER } )
		SetParamVisibility( id, timerMode, raise );
	SetParamVisibility( PT_AT_END, !clockMode, raise );
	SetParamDisplayName( PT_TO, mode == (int)Mode::UntilTime ? "Target Time" : "", raise );
	SetParamVisibility( PT_GHOST, font == (int)FontId::Digital, raise );
}

void FFGLTimer::SetRunning( bool run, bool notifyHost )
{
	if( run == timer.IsRunning() )
		return;
	if( run )
		timer.Start();
	else
		timer.Pause();
	if( notifyHost )
		RaiseParamEvent( PT_RUNNING, FF_EVENT_FLAG_VALUE );
}

bool FFGLTimer::Pressed( unsigned int index, float value )
{
	// Hosts send a non-zero value when the button is pressed (and 0 on release).
	(void)index;
	return value > 0.5f;
}

void FFGLTimer::Upload( Tex& tex, const Bitmap& bmp )
{
	if( !tex.id )
		glGenTextures( 1, &tex.id );
	Scoped2DTextureBinding binding( tex.id );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	const GLenum fmt      = bmp.channels == 2 ? GL_RG : GL_RED;
	const GLint  internal = bmp.channels == 2 ? GL_RG8 : GL_R8;
	if( tex.w == bmp.width && tex.h == bmp.height )
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, bmp.width, bmp.height, fmt, GL_UNSIGNED_BYTE, bmp.pixels.data() );
	else
	{
		glTexImage2D( GL_TEXTURE_2D, 0, internal, bmp.width, bmp.height, 0, fmt, GL_UNSIGNED_BYTE, bmp.pixels.data() );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	}
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );// FFGL: leave the default state
	tex.w = bmp.width;
	tex.h = bmp.height;
}

FFResult FFGLTimer::ProcessOpenGL( ProcessOpenGLStruct* )
{
	using clock = std::chrono::steady_clock;
	const auto now = clock::now();

	// --- clock / trigger handling
	double dt = 0.0;
	if( haveLastFrame )
	{
		dt = std::chrono::duration< double >( now - lastFrame ).count();
		// The clip was not being played for a while: it was just (re)triggered.
		if( restartOnTrigger && dt > 1.0 && mode == (int)Mode::Timer )
		{
			timer.Reset();
			dt = 0.0;
			SetRunning( true, true );
		}
	}
	else if( restartOnTrigger && mode == (int)Mode::Timer )
	{
		timer.Reset();
		SetRunning( true, true );
	}
	lastFrame     = now;
	haveLastFrame = true;

	if( mode != lastMode || font != lastFont )
	{
		if( mode != lastMode )
			ParseInputs();
		UpdateVisibility( lastMode != -1 );
		lastMode = mode;
		lastFont = font;
	}

	const double tod = LocalSecondsOfDay();
	Snapshot snap    = timer.Update( dt, (Mode)mode, fromSec, fromValid, toSec, toValid, (EndAction)endAction, warnSec, critSec, tod );
	if( timer.ConsumeAutoStopped() )
		RaiseParamEvent( PT_RUNNING, FF_EVENT_FLAG_VALUE );

	// --- build strings
	const int dec       = std::max( 0, std::min( 2, decimals ) );
	const FontId fontId = (FontId)std::max( 0, std::min( 3, font ) );
	std::string timeStr;
	if( !snap.valid )
		timeStr = "--:--";
	else if( mode == (int)Mode::Clock )
	{
		Format f = (Format)format;
		if( f == Format::Auto || f == Format::MS || f == Format::Seconds )
			f = Format::HMS;
		timeStr = FormatTime( snap.displayValue, f, f == Format::HM ? 0 : dec, false );
	}
	else
	{
		timeStr = FormatTime( snap.displayValue, (Format)format, dec, snap.countingDown );
	}

	// Digital font: unlit segments behind every digit ("8" lights all segments).
	std::string ghostStr;
	if( fontId == FontId::Digital && ghost > 0.001f )
	{
		ghostStr = timeStr;
		for( auto& c : ghostStr )
			if( ( c >= '0' && c <= '9' ) || c == '-' )
				c = '8';
	}

	// --- layout (pixels)
	const float W = (float)std::max< GLuint >( 1, currentViewport.width );
	const float H = (float)std::max< GLuint >( 1, currentViewport.height );

	// Size the digits from a template so the text doesn't jump when e.g. a minus sign appears.
	std::string templ = timeStr;
	for( auto& c : templ )
		if( c >= '0' && c <= '9' )
			c = '0';
	if( endAction == (int)EndAction::Continue && mode != (int)Mode::Clock && templ[ 0 ] != '-' )
		templ = "-" + templ;
	const bool hasLabel    = !label.empty();
	const float labelRatio = hasLabel ? 0.15f + 0.85f * labelSize : 0.0f;// label height relative to digit height
	const float widthUnit  = std::max( 0.1f, text.MeasureTimeWidth( templ, fontId ) );
	float digitPx          = std::min( W * 0.94f / widthUnit, H * 0.8f / ( 1.0f + labelRatio * 1.3f ) );
	digitPx *= std::max( 0.05f, std::min( 1.0f, size ) );
	const int digitPxI = std::max( 6, (int)std::lround( digitPx ) );
	const int labelPxI = hasLabel ? std::max( 6, (int)std::lround( digitPxI * labelRatio ) ) : 0;
	pad                = std::max( 2, digitPxI / 12 );

	// Blinking colon while running (on for the first half of every second).
	bool hideColons = false;
	if( blinkColon && ( mode == (int)Mode::Clock || ( timer.IsRunning() && !snap.finished ) ) )
	{
		double v   = mode == (int)Mode::Clock ? tod : timer.Elapsed();
		hideColons = std::fmod( std::fabs( v ), 1.0 ) >= 0.5;
	}

	if( timeStr != renderedTime || ghostStr != renderedGhost || (int)fontId != renderedFont || digitPxI != renderedDigitPx ||
	    hideColons != renderedHideColons || !timeTex.id )
	{
		text.RenderTime( timeStr, ghostStr, fontId, digitPxI, pad, hideColons, timeBmp );
		Upload( timeTex, timeBmp );
		renderedTime       = timeStr;
		renderedGhost      = ghostStr;
		renderedFont       = (int)fontId;
		renderedDigitPx    = digitPxI;
		renderedHideColons = hideColons;
	}
	if( hasLabel && ( label != renderedLabel || labelPxI != renderedLabelPx || !labelTex.id ) )
	{
		// Labels always use a text font (the 7-segment font has no letters).
		const FontId lf = fontId == FontId::Digital ? FontId::InterBold : fontId;
		text.RenderLabel( label, lf, labelPxI, pad, labelBmp );
		Upload( labelTex, labelBmp );
		renderedLabel   = label;
		renderedLabelPx = labelPxI;
	}

	// Block = label + gap + digits; positioned by its centre.
	const float gap    = hasLabel ? digitPxI * 0.18f : 0.0f;
	const float timeH  = (float)digitPxI;
	const float labelH = hasLabel ? (float)labelPxI : 0.0f;
	const float blockH = timeH + gap + labelH;
	const float cx     = posX * W;
	const float cy     = ( 1.0f - posY ) * H;// Position Y: 0 = bottom, 1 = top (like Resolume's transform)
	const float top    = cy - blockH * 0.5f;
	const bool below   = labelPos == 1;
	const float timeTop  = below ? top : top + labelH + gap;
	const float labelTop = below ? top + timeH + gap : top;

	const float tw = (float)timeBmp.width, th = (float)timeBmp.height;
	float timeRect[ 4 ] = { std::round( cx - tw * 0.5f ), std::round( timeTop - pad ), tw, th };
	float labelRect[ 4 ] = { 0, 0, 1, 1 };
	if( hasLabel )
	{
		const float lw = (float)labelBmp.width, lh = (float)labelBmp.height;
		labelRect[ 0 ] = std::round( cx - lw * 0.5f );
		labelRect[ 1 ] = std::round( labelTop - pad );
		labelRect[ 2 ] = lw;
		labelRect[ 3 ] = lh;
	}

	// --- colours
	float textRGBA[ 4 ], labelRGBA[ 4 ], bgRGBA[ 4 ];
	const HSBA& stateColor = snap.phase == Phase::Normal ? textColor : snap.phase == Phase::Warning ? warnColor : critColor;
	ToRGB( mode == (int)Mode::Clock ? textColor : stateColor, textRGBA );
	ToRGB( textColor, labelRGBA );
	ToRGB( bgColor, bgRGBA );

	// Flash: 0.6 s on / 0.4 s off
	const bool flashing = snap.phase == Phase::Over &&
	                      ( endAction == (int)EndAction::StopFlash || ( endAction == (int)EndAction::Continue && flashOver ) );
	if( flashing )
	{
		using namespace std::chrono;
		const double t = duration< double >( now.time_since_epoch() ).count();
		if( std::fmod( t, 1.0 ) > 0.6 )
			textRGBA[ 3 ] *= 0.15f;
	}

	// --- progress bar
	float barRect[ 4 ] = { 0, 0, 0, 0 };
	const bool barAvailable = mode == (int)Mode::Timer && snap.valid;
	if( barMode != 0 && barAvailable )
	{
		const float bh = std::max( 2.0f, H * 0.1f * barHeight );
		if( barMode == 1 )
		{
			const float bw = std::max( 4.0f, tw - 2.0f * pad );
			const float y  = below ? timeTop + timeH + gap + labelH + digitPxI * 0.12f : timeTop + timeH + digitPxI * 0.15f;
			barRect[ 0 ]   = std::round( cx - bw * 0.5f );
			barRect[ 1 ]   = std::round( y );
			barRect[ 2 ]   = bw;
			barRect[ 3 ]   = bh;
		}
		else
		{
			const float margin = W * 0.03f;
			barRect[ 0 ]       = margin;
			barRect[ 1 ]       = barMode == 2 ? H - margin * 0.6f - bh : margin * 0.6f;
			barRect[ 2 ]       = W - 2.0f * margin;
			barRect[ 3 ]       = bh;
		}
	}
	// The bar shows time used for count-ups and time left for countdowns.
	const float barFill = (float)( snap.countingDown ? 1.0 - snap.progress : snap.progress );

	// --- draw
	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	glActiveTexture( GL_TEXTURE1 );
	glBindTexture( GL_TEXTURE_2D, hasLabel ? labelTex.id : 0 );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, timeTex.id );

	glUniform1i( shader.FindUniform( "timeTex" ), 0 );
	glUniform1i( shader.FindUniform( "labelTex" ), 1 );
	glUniform2f( shader.FindUniform( "resolution" ), W, H );
	glUniform4fv( shader.FindUniform( "timeRect" ), 1, timeRect );
	glUniform4fv( shader.FindUniform( "labelRect" ), 1, labelRect );
	glUniform1i( shader.FindUniform( "hasLabel" ), hasLabel ? 1 : 0 );
	glUniform4fv( shader.FindUniform( "textColor" ), 1, textRGBA );
	glUniform4fv( shader.FindUniform( "labelColor" ), 1, labelRGBA );
	glUniform1f( shader.FindUniform( "ghostAlpha" ), ghostStr.empty() ? 0.0f : ghost );
	glUniform1f( shader.FindUniform( "shadow" ), shadow );
	glUniform1f( shader.FindUniform( "shadowOffset" ), std::max( 1.0f, digitPxI * 0.035f ) );
	glUniform4fv( shader.FindUniform( "background" ), 1, bgRGBA );
	glUniform4fv( shader.FindUniform( "barRect" ), 1, barRect );
	glUniform1f( shader.FindUniform( "barProgress" ), barFill );
	glUniform4fv( shader.FindUniform( "barColor" ), 1, textRGBA );

	quad.Draw();

	glActiveTexture( GL_TEXTURE1 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return FF_SUCCESS;
}

FFResult FFGLTimer::SetFloatParameter( unsigned int index, float value )
{
	const bool on = value > 0.5f;
	auto opt      = [ & ]() { return (int)std::lround( value ); };
	switch( index )
	{
	case PT_MODE:
		mode = opt();
		ParseInputs();
		break;
	case PT_STARTPAUSE:
		if( Pressed( index, value ) )
		{
			// Pressing start on a finished timer starts it over.
			if( !timer.IsRunning() )
			{
				Snapshot s = timer.Update( 0.0, (Mode)mode, fromSec, fromValid, toSec, toValid, (EndAction)endAction, warnSec, critSec, 0.0 );
				if( s.finished )
					timer.Reset();
			}
			SetRunning( !timer.IsRunning(), true );
		}
		break;
	case PT_START:
		if( Pressed( index, value ) )
		{
			Snapshot s = timer.Update( 0.0, (Mode)mode, fromSec, fromValid, toSec, toValid, (EndAction)endAction, warnSec, critSec, 0.0 );
			if( s.finished )
				timer.Reset();
			SetRunning( true, true );
		}
		break;
	case PT_PAUSE:
		if( Pressed( index, value ) )
			SetRunning( false, true );
		break;
	case PT_RESET:
		if( Pressed( index, value ) )
			timer.Reset();
		break;
	case PT_RUNNING:
		SetRunning( on, false );
		break;
	case PT_SWAP:
		if( Pressed( index, value ) )
		{
			std::swap( fromText, toText );
			ParseInputs();
			timer.Reset();
			RaiseParamEvent( PT_FROM, FF_EVENT_FLAG_VALUE );
			RaiseParamEvent( PT_TO, FF_EVENT_FLAG_VALUE );
		}
		break;
	case PT_ADD_MIN:
		if( Pressed( index, value ) )
			timer.AdjustRemaining( 60.0 );
		break;
	case PT_SUB_MIN:
		if( Pressed( index, value ) )
			timer.AdjustRemaining( -60.0 );
		break;
	case PT_AT_END: endAction = opt(); break;
	case PT_RESTART_TRIGGER: restartOnTrigger = on; break;
	case PT_FORMAT: format = opt(); break;
	case PT_DECIMALS: decimals = opt(); break;
	case PT_FONT: font = opt(); break;
	case PT_GHOST: ghost = value; break;
	case PT_BLINK_COLON: blinkColon = on; break;
	case PT_LABEL_SIZE: labelSize = value; break;
	case PT_LABEL_POS: labelPos = opt(); break;
	case PT_SIZE: size = value; break;
	case PT_POSX: posX = value; break;
	case PT_POSY: posY = value; break;
	case PT_SHADOW: shadow = value; break;
	case PT_TEXT_H: textColor.h = value; break;
	case PT_TEXT_S: textColor.s = value; break;
	case PT_TEXT_B: textColor.b = value; break;
	case PT_TEXT_A: textColor.a = value; break;
	case PT_WARN_H: warnColor.h = value; break;
	case PT_WARN_S: warnColor.s = value; break;
	case PT_WARN_B: warnColor.b = value; break;
	case PT_WARN_A: warnColor.a = value; break;
	case PT_CRIT_H: critColor.h = value; break;
	case PT_CRIT_S: critColor.s = value; break;
	case PT_CRIT_B: critColor.b = value; break;
	case PT_CRIT_A: critColor.a = value; break;
	case PT_FLASH_OVER: flashOver = on; break;
	case PT_BG_H: bgColor.h = value; break;
	case PT_BG_S: bgColor.s = value; break;
	case PT_BG_B: bgColor.b = value; break;
	case PT_BG_A: bgColor.a = value; break;
	case PT_BAR: barMode = opt(); break;
	case PT_BAR_HEIGHT: barHeight = value; break;
	default: return FF_FAIL;
	}
	return FF_SUCCESS;
}

float FFGLTimer::GetFloatParameter( unsigned int index )
{
	switch( index )
	{
	case PT_MODE: return (float)mode;
	case PT_RUNNING: return timer.IsRunning() ? 1.0f : 0.0f;
	case PT_AT_END: return (float)endAction;
	case PT_RESTART_TRIGGER: return restartOnTrigger ? 1.0f : 0.0f;
	case PT_FORMAT: return (float)format;
	case PT_DECIMALS: return (float)decimals;
	case PT_FONT: return (float)font;
	case PT_GHOST: return ghost;
	case PT_BLINK_COLON: return blinkColon ? 1.0f : 0.0f;
	case PT_LABEL_SIZE: return labelSize;
	case PT_LABEL_POS: return (float)labelPos;
	case PT_SIZE: return size;
	case PT_POSX: return posX;
	case PT_POSY: return posY;
	case PT_SHADOW: return shadow;
	case PT_TEXT_H: return textColor.h;
	case PT_TEXT_S: return textColor.s;
	case PT_TEXT_B: return textColor.b;
	case PT_TEXT_A: return textColor.a;
	case PT_WARN_H: return warnColor.h;
	case PT_WARN_S: return warnColor.s;
	case PT_WARN_B: return warnColor.b;
	case PT_WARN_A: return warnColor.a;
	case PT_CRIT_H: return critColor.h;
	case PT_CRIT_S: return critColor.s;
	case PT_CRIT_B: return critColor.b;
	case PT_CRIT_A: return critColor.a;
	case PT_FLASH_OVER: return flashOver ? 1.0f : 0.0f;
	case PT_BG_H: return bgColor.h;
	case PT_BG_S: return bgColor.s;
	case PT_BG_B: return bgColor.b;
	case PT_BG_A: return bgColor.a;
	case PT_BAR: return (float)barMode;
	case PT_BAR_HEIGHT: return barHeight;
	default: return 0.0f;// events
	}
}

FFResult FFGLTimer::SetTextParameter( unsigned int index, const char* value )
{
	std::string v = value ? value : "";
	switch( index )
	{
	case PT_FROM:
		if( v != fromText )
		{
			fromText = v;
			ParseInputs();
			timer.Reset();
		}
		break;
	case PT_TO:
		if( v != toText )
		{
			toText = v;
			ParseInputs();
		}
		break;
	case PT_WARN_AT:
		warnText = v;
		ParseInputs();
		break;
	case PT_CRIT_AT:
		critText = v;
		ParseInputs();
		break;
	case PT_LABEL: label = v; break;
	default: return FF_FAIL;
	}
	return FF_SUCCESS;
}

char* FFGLTimer::GetTextParameter( unsigned int index )
{
	switch( index )
	{
	case PT_FROM: return const_cast< char* >( fromText.c_str() );
	case PT_TO: return const_cast< char* >( toText.c_str() );
	case PT_WARN_AT: return const_cast< char* >( warnText.c_str() );
	case PT_CRIT_AT: return const_cast< char* >( critText.c_str() );
	case PT_LABEL: return const_cast< char* >( label.c_str() );
	default: return nullptr;
	}
}

char* FFGLTimer::GetParameterDisplay( unsigned int index )
{
	switch( index )
	{
	case PT_SIZE:
	case PT_POSX:
	case PT_POSY:
	case PT_SHADOW:
	case PT_GHOST:
	case PT_LABEL_SIZE:
	case PT_BAR_HEIGHT:
		std::snprintf( displayBuf, sizeof( displayBuf ), "%.0f%%", GetFloatParameter( index ) * 100.0f );
		return displayBuf;
	default: return CFFGLPlugin::GetParameterDisplay( index );
	}
}
