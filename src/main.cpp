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
#include "camera3D.h"
#include <numbers>

constexpr float PI = std::numbers::pi_v<float>;

class Downshift : public olc::PixelGameEngine
{
public:
	Downshift()
	{
		sAppName = "Downshift";
	}

    std::vector<olc::vf4d>  pos;  // 36 vertices, 12 triangles
    std::vector<olc::Pixel> col;  // one color per vertex
    olc::utils::Camera3D    cam;

    // Add one face as two triangles.  Corners a,b,c,d go clockwise seen from outside.
    void Face(olc::vf4d a, olc::vf4d b, olc::vf4d c, olc::vf4d d, olc::Pixel color)
    {
        for (auto v : { a, b, c,  a, c, d })
        {
            pos.push_back({v.x, v.y, v.z, 1.0f });
            col.push_back(color);
        }
    }

	// Called once at the start, so create things here
	bool OnUserCreate() override
	{
        float xw = 0.0f;  // x west pos
        float xe = 30.0f;  // x east pos
        int width = 3;
        float hw = (float)width / 2;  // half width
        int length = (int)xe;
        
        for (int i=0; i<length; i++)
        {
            int j=i+1;
            float mc = 1.0;  // mulitply color
            if (i & 1) mc=0.85;
            float ii = (float)i;
            float jj = (float)j;

            Face({ii,-hw,-hw}, {ii,hw,-hw},  {jj,hw,-hw}, {jj,-hw,-hw}, olc::Pixel(150, 60, 60) * mc); // -Z south
            Face({jj,-hw,hw}, {jj,hw,hw},   {ii,hw,hw},  {ii,-hw,hw},  olc::Pixel( 60, 60, 150) * mc); // +Z  north
            Face({ii,hw,-hw}, {ii,hw,hw},   {jj,hw,hw},  {jj,hw,-hw},  olc::Pixel(220,220, 220) * mc); // +Y  top


            for (int k=0; k<width; k++) 
            {
                float kk = (float)k;
                if ((i+k) & 1) mc=0.85; else mc=1.0;
                Face({ii,-hw,-hw+kk}, {jj,-hw,-hw+kk}, {jj,-hw,-hw+kk+1}, {ii,-hw,-hw+kk+1},  olc::Pixel( 60, 60,  60) * mc); // -Y  bottom

            }

        }

        Face({xw,-hw,hw}, {xw,hw,hw},   {xw,hw,-hw}, {xw,-hw,-hw}, olc::Pixel(150,150,  60) ); // -X  west
        Face({xe,-hw,-hw}, {xe,hw,-hw},  {xe,hw,hw},  {xe,-hw,hw},  olc::Pixel( 60,150,  60) ); // +X  east
        
        
        cam.SetPerspective(60.0f * PI / 180.0f, float(ScreenSize().x) / ScreenSize().y, 0.1f, 100.0f);
        cam.SetTarget({4, 0, 0});       // look down the hall
        cam.SetDistance(2.0f);          // from 2 units away
        cam.SetYaw(-PI/2);              // turned so we down X axis
        cam.SetPitch(0.0f);             // level view
		return true;
	}

	// Called every frame, so update things here
	bool OnUserUpdate(float dt) override
	{
        // Escape quits the game
        if (keyboard.GetKey(olc::Key::ESCAPE).bPressed) return false;

        cam.Update(dt);
        cam.Apply(draw);        // sets view + projection

		// Clear screen to dark blue
		draw.Clear(olc::Colour::VERY_DARK_BLUE);

        draw.SetCullMode(olc::CullMode::ClockWise);
        draw.EnableDepth(true);

        olc::mf4d model;        // identity: cube sits at the origin
        draw.SetModelMatrix(model);
        draw.Mesh(olc::Structure::List, pos, col);

		// Successful frame
		return true;
	}
};


int main()
{
	olc::PGEConfig cfg;
	cfg.vScreenSize = { 640, 360 };
	cfg.vPixelSize  = { 2, 2 };
	cfg.bVSync      = true;

	Downshift app;
	if (app.Construct(cfg))
		app.Start();
	return 0;
}

