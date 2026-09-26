/*
	Downshift: Escape a 3D maze by changing gravity.

	OLC CodeJam 2026 entry.

    Copyright (c) 2026 Brandon Blodget
	License: GPLv3, see LICENSE.txt for details.
*/


// Define OLC_PGE3_APPLICATION to include the implementation of 
// the Pixel Game Engine as part of this translation unit
#define OLC_PGE3_APPLICATION
#include "olcPixelGameEngine3.h"

class Downshift : public olc::PixelGameEngine
{
public:
	Downshift()
	{
		sAppName = "Downshift";
	}

public:
	// Called once at the start, so create things here
	bool OnUserCreate() override
	{
		// Nothing to do here, so return true
		return true;
	}

	// Called every frame, so update things here
	bool OnUserUpdate(float fElapsedTime) override
	{
		// Clear screen to dark blue
		draw.Clear(olc::Colour::VERY_DARK_BLUE);

		// Successful frame
		return true;
	}
};


int main()
{
	olc::PGEConfig cfg;
	cfg.vScreenSize = { 480, 270 };
	cfg.vPixelSize  = { 2, 2 };
	cfg.bVSync      = true;

	Downshift app;
	if (app.Construct(cfg))
		app.Start();
	return 0;
}

