#pragma once
// CPU text rasteriser based on stb_truetype with embedded fonts.
// Digits are placed in fixed-width cells (tabular figures) so a running timer
// never jitters, whatever the font.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace stimer
{
enum class FontId
{
	InterBold = 0,
	InterLight,
	Mono,
	Digital,
	Count
};

struct Bitmap
{
	int width    = 0;
	int height   = 0;
	int channels = 1;
	std::vector< uint8_t > pixels;// top row first
};

class TextRasterizer
{
public:
	TextRasterizer();
	~TextRasterizer();

	bool Ok() const { return ok; }

	// Width of `text` in pixels for a digit (ink) height of 1 px.
	float MeasureTimeWidth( const std::string& text, FontId font ) const;

	// Renders a time string. Channel 0 = text, channel 1 = "ghost" (all segments lit,
	// see `ghost`; may be empty). Digit ink height is `digitPx`; `pad` px margin all round.
	// When `hideColons` is set ':' keeps its space but is not drawn (blinking colon).
	void RenderTime( const std::string& text, const std::string& ghost, FontId font, int digitPx, int pad,
	                 bool hideColons, Bitmap& out );

	// Renders a UTF-8 line of text with font size (ascent - descent) of `px`. One channel.
	void RenderLabel( const std::string& utf8, FontId font, int px, int pad, Bitmap& out );

	// Digit ink box for `digitPx`: distance from baseline to the top of '0'.
	static std::vector< uint32_t > DecodeUtf8( const std::string& s );

private:
	struct Font;
	struct Glyph
	{
		int w = 0, h = 0, x0 = 0, y0 = 0;// bitmap box relative to pen position / baseline
		float advance = 0;               // px
		std::vector< uint8_t > alpha;
	};

	const Glyph& GetGlyph( FontId font, float scale, uint32_t cp );
	float DigitScale( FontId font, int digitPx ) const;// font units -> px so that '0' is digitPx tall
	float CellAdvance( FontId font, float scale, uint32_t cp ) const;

	std::unique_ptr< Font > fonts[ (int)FontId::Count ];
	bool ok = false;

	// glyph cache: key = font | scale bits | codepoint
	std::map< std::tuple< int, uint32_t, uint32_t >, Glyph > cache;
	size_t cacheBytes = 0;
};

}// namespace stimer
