#pragma once
#include <FFGLSDK.h>
#include <chrono>
#include <string>
#include "TextRasterizer.h"
#include "TimerLogic.h"

class FFGLTimer : public CFFGLPlugin
{
public:
	FFGLTimer();

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char* GetTextParameter( unsigned int index ) override;
	char* GetParameterDisplay( unsigned int index ) override;

private:
	struct HSBA
	{
		float h, s, b, a;
	};
	struct Tex
	{
		GLuint id = 0;
		int w = 0, h = 0;
	};

	bool Pressed( unsigned int index, float value );// rising edge of an event param
	void SetRunning( bool run, bool notifyHost );
	void UpdateVisibility( bool raise );
	void ParseInputs();
	void Upload( Tex& tex, const stimer::Bitmap& bmp );
	static void ToRGB( const HSBA& c, float out[ 4 ] );
	void AddColorParams( unsigned int first, const char* name, float h, float s, float b, float a );
	double LocalSecondsOfDay() const;

	// --- parameters
	int mode       = 0;
	std::string fromText = "00:05:00", toText = "00:00:00";
	std::string warnText = "1:00", critText = "0:10";
	std::string label;
	int endAction        = 0;
	bool restartOnTrigger = false;
	int format           = 0;
	int decimals         = 0;
	int font             = 0;
	float ghost          = 0.12f;
	bool blinkColon      = false;
	float labelSize      = 0.3f;
	int labelPos         = 0;
	float size           = 0.8f;
	float posX = 0.5f, posY = 0.5f;
	float shadow         = 0.3f;
	bool flashOver       = true;
	int barMode          = 0;
	float barHeight      = 0.15f;
	HSBA textColor{ 0.0f, 0.0f, 1.0f, 1.0f };
	HSBA warnColor{ 0.11f, 1.0f, 1.0f, 1.0f };
	HSBA critColor{ 0.0f, 1.0f, 1.0f, 1.0f };
	HSBA bgColor{ 0.0f, 0.0f, 0.0f, 0.0f };

	// --- parsed inputs
	double fromSec = 300, toSec = 0, warnSec = 60, critSec = 10;
	bool fromValid = true, toValid = true;

	// --- runtime
	stimer::TimerState timer;
	stimer::TextRasterizer text;
	std::chrono::steady_clock::time_point lastFrame;
	bool haveLastFrame = false;
	bool hostNeedsRunningUpdate = false;
	int lastMode = -1, lastFont = -1;

	// --- render cache
	std::string renderedTime, renderedGhost, renderedLabel;
	int renderedFont = -1, renderedDigitPx = 0, renderedLabelPx = 0;
	bool renderedHideColons = false;
	stimer::Bitmap timeBmp, labelBmp;
	Tex timeTex, labelTex;
	int pad = 0;

	ffglex::FFGLShader shader;
	ffglex::FFGLScreenQuad quad;
	char displayBuf[ 64 ] = {};
};
