#include "TextRasterizer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include "FontDigital.h"
#include "FontInterBold.h"
#include "FontInterLight.h"
#include "FontMonoBold.h"

namespace stimer
{
struct TextRasterizer::Font
{
	stbtt_fontinfo info{};
	int ascent = 0, descent = 0, lineGap = 0;
	float zeroHeight   = 1.0f;// ink height of '0' in font units
	float digitAdvance = 0.0f;// widest digit, font units
};

namespace
{
bool IsDigit( uint32_t cp )
{
	return cp >= '0' && cp <= '9';
}

// Typographic minus for text fonts (DSEG's '-' already is the middle segment).
uint32_t MapTimeChar( FontId font, uint32_t cp )
{
	return ( cp == '-' && font != FontId::Digital ) ? 0x2212u : cp;
}

uint32_t FloatBits( float f )
{
	uint32_t u;
	std::memcpy( &u, &f, 4 );
	return u;
}
}// namespace

std::vector< uint32_t > TextRasterizer::DecodeUtf8( const std::string& s )
{
	std::vector< uint32_t > out;
	size_t i = 0;
	while( i < s.size() )
	{
		unsigned char c = (unsigned char)s[ i ];
		uint32_t cp     = 0xFFFD;
		int len         = 1;
		if( c < 0x80 )
			cp = c;
		else if( ( c >> 5 ) == 0x6 && i + 1 < s.size() )
		{
			cp  = ( ( c & 0x1F ) << 6 ) | ( s[ i + 1 ] & 0x3F );
			len = 2;
		}
		else if( ( c >> 4 ) == 0xE && i + 2 < s.size() )
		{
			cp  = ( ( c & 0x0F ) << 12 ) | ( ( s[ i + 1 ] & 0x3F ) << 6 ) | ( s[ i + 2 ] & 0x3F );
			len = 3;
		}
		else if( ( c >> 3 ) == 0x1E && i + 3 < s.size() )
		{
			cp = ( ( c & 0x07 ) << 18 ) | ( ( s[ i + 1 ] & 0x3F ) << 12 ) | ( ( s[ i + 2 ] & 0x3F ) << 6 ) |
			     ( s[ i + 3 ] & 0x3F );
			len = 4;
		}
		out.push_back( cp );
		i += len;
	}
	return out;
}

TextRasterizer::TextRasterizer()
{
	const unsigned char* data[] = { kFontInterBold, kFontInterLight, kFontMonoBold, kFontDigital };
	ok                          = true;
	for( int i = 0; i < (int)FontId::Count; ++i )
	{
		fonts[ i ] = std::make_unique< Font >();
		Font& f    = *fonts[ i ];
		if( !stbtt_InitFont( &f.info, data[ i ], stbtt_GetFontOffsetForIndex( data[ i ], 0 ) ) )
		{
			ok = false;
			continue;
		}
		stbtt_GetFontVMetrics( &f.info, &f.ascent, &f.descent, &f.lineGap );
		int x0, y0, x1, y1;
		if( stbtt_GetCodepointBox( &f.info, '0', &x0, &y0, &x1, &y1 ) && y1 > y0 )
			f.zeroHeight = (float)( y1 - y0 );
		else
			f.zeroHeight = (float)f.ascent * 0.7f;
		for( uint32_t d = '0'; d <= '9'; ++d )
		{
			int adv, lsb;
			stbtt_GetCodepointHMetrics( &f.info, (int)d, &adv, &lsb );
			f.digitAdvance = std::max( f.digitAdvance, (float)adv );
		}
	}
}

TextRasterizer::~TextRasterizer() = default;

float TextRasterizer::DigitScale( FontId font, int digitPx ) const
{
	return (float)digitPx / fonts[ (int)font ]->zeroHeight;
}

float TextRasterizer::CellAdvance( FontId font, float scale, uint32_t cp ) const
{
	const Font& f = *fonts[ (int)font ];
	if( IsDigit( cp ) )
		return f.digitAdvance * scale;
	int adv, lsb;
	stbtt_GetCodepointHMetrics( &f.info, (int)cp, &adv, &lsb );
	return adv * scale;
}

const TextRasterizer::Glyph& TextRasterizer::GetGlyph( FontId font, float scale, uint32_t cp )
{
	auto key = std::make_tuple( (int)font, FloatBits( scale ), cp );
	auto it  = cache.find( key );
	if( it != cache.end() )
		return it->second;

	// Keep memory bounded when the size is being dragged around.
	if( cacheBytes > 64u * 1024u * 1024u )
	{
		cache.clear();
		cacheBytes = 0;
	}

	const Font& f = *fonts[ (int)font ];
	Glyph g;
	int adv, lsb;
	stbtt_GetCodepointHMetrics( &f.info, (int)cp, &adv, &lsb );
	g.advance = adv * scale;
	int x0, y0, x1, y1;
	stbtt_GetCodepointBitmapBox( &f.info, (int)cp, scale, scale, &x0, &y0, &x1, &y1 );
	g.x0 = x0;
	g.y0 = y0;
	g.w  = std::max( 0, x1 - x0 );
	g.h  = std::max( 0, y1 - y0 );
	if( g.w > 0 && g.h > 0 )
	{
		g.alpha.assign( (size_t)g.w * g.h, 0 );
		stbtt_MakeCodepointBitmap( &f.info, g.alpha.data(), g.w, g.h, g.w, scale, scale, (int)cp );
	}
	cacheBytes += g.alpha.size() + 64;
	return cache.emplace( key, std::move( g ) ).first->second;
}

float TextRasterizer::MeasureTimeWidth( const std::string& text, FontId font ) const
{
	const float scale = 1.0f / fonts[ (int)font ]->zeroHeight;
	float w           = 0.0f;
	for( char c : text )
		w += CellAdvance( font, scale, MapTimeChar( font, (unsigned char)c ) );
	return w;
}

void TextRasterizer::RenderTime( const std::string& text, const std::string& ghost, FontId font, int digitPx, int pad,
                                 bool hideColons, Bitmap& out )
{
	digitPx           = std::max( 4, digitPx );
	const float scale = DigitScale( font, digitPx );
	const Font& f     = *fonts[ (int)font ];

	float width = 0.0f;
	for( char c : text )
		width += CellAdvance( font, scale, MapTimeChar( font, (unsigned char)c ) );

	// Vertical box: from digit top to a little below baseline (for '-' / descenders of ':' etc.)
	const int baseline = pad + digitPx;
	out.width          = std::max( 1, (int)std::ceil( width ) + 2 * pad );
	out.height         = digitPx + 2 * pad;
	out.channels       = 2;
	out.pixels.assign( (size_t)out.width * out.height * 2, 0 );
	(void)f;

	auto drawString = [ & ]( const std::string& s, int channel, bool skipColon ) {
		float pen = (float)pad;
		for( char ch : s )
		{
			const uint32_t cp = MapTimeChar( font, (unsigned char)ch );
			const float cell  = CellAdvance( font, scale, cp );
			if( !( skipColon && cp == ':' ) && cp != ' ' )
			{
				const Glyph& g = GetGlyph( font, scale, cp );
				// centre the glyph in its cell (matters for proportional digits)
				const float offset = IsDigit( cp ) ? ( cell - g.advance ) * 0.5f : 0.0f;
				const int gx       = (int)std::lround( pen + offset ) + g.x0;
				const int gy       = baseline + g.y0;
				for( int y = 0; y < g.h; ++y )
				{
					const int ty = gy + y;
					if( ty < 0 || ty >= out.height )
						continue;
					for( int x = 0; x < g.w; ++x )
					{
						const int tx = gx + x;
						if( tx < 0 || tx >= out.width )
							continue;
						uint8_t& dst = out.pixels[ ( (size_t)ty * out.width + tx ) * 2 + channel ];
						dst          = std::max( dst, g.alpha[ (size_t)y * g.w + x ] );
					}
				}
			}
			pen += cell;
		}
	};

	drawString( text, 0, hideColons );
	if( !ghost.empty() )
		drawString( ghost, 1, false );
}

void TextRasterizer::RenderLabel( const std::string& utf8, FontId font, int px, int pad, Bitmap& out )
{
	px            = std::max( 4, px );
	const Font& f = *fonts[ (int)font ];
	const float scale = (float)px / (float)( f.ascent - f.descent );
	const auto cps    = DecodeUtf8( utf8 );

	float width = 0.0f;
	for( size_t i = 0; i < cps.size(); ++i )
	{
		width += GetGlyph( font, scale, cps[ i ] ).advance;
		if( i + 1 < cps.size() )
			width += scale * stbtt_GetCodepointKernAdvance( &f.info, (int)cps[ i ], (int)cps[ i + 1 ] );
	}

	const int baseline = pad + (int)std::lround( f.ascent * scale );
	out.width          = std::max( 1, (int)std::ceil( width ) + 2 * pad );
	out.height         = px + 2 * pad;
	out.channels       = 1;
	out.pixels.assign( (size_t)out.width * out.height, 0 );

	float pen = (float)pad;
	for( size_t i = 0; i < cps.size(); ++i )
	{
		const Glyph& g = GetGlyph( font, scale, cps[ i ] );
		const int gx   = (int)std::lround( pen ) + g.x0;
		const int gy   = baseline + g.y0;
		for( int y = 0; y < g.h; ++y )
		{
			const int ty = gy + y;
			if( ty < 0 || ty >= out.height )
				continue;
			for( int x = 0; x < g.w; ++x )
			{
				const int tx = gx + x;
				if( tx < 0 || tx >= out.width )
					continue;
				uint8_t& dst = out.pixels[ (size_t)ty * out.width + tx ];
				dst          = std::max( dst, g.alpha[ (size_t)y * g.w + x ] );
			}
		}
		pen += g.advance;
		if( i + 1 < cps.size() )
			pen += scale * stbtt_GetCodepointKernAdvance( &f.info, (int)cps[ i ], (int)cps[ i + 1 ] );
	}
}

}// namespace stimer
