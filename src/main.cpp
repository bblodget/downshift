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

enum class Gravity
{
    NegY = 0,  // floor
    PosZ = 1,  // left wall
    PosY = 2,  // ceiling
    NegZ = 3,  // right wall
};

struct GravityInfo
{
    olc::vf4d direction;
    const char* name;
};

const GravityInfo gravityTable[] =
{
    {{0.0f, -1.0f,  0.0f, 0.0f}, "floor"},
    {{0.0f,  0.0f,  1.0f, 0.0f}, "left wall"},
    {{0.0f,  1.0f,  0.0f, 0.0f}, "ceiling"},
    {{0.0f,  0.0f, -1.0f, 0.0f}, "right wall"}
};

olc::vf4d GravityDirection(Gravity gravity)
{
    return gravityTable[static_cast<int>(gravity)].direction;
}

const char* GravityName(Gravity gravity)
{
    return gravityTable[static_cast<int>(gravity)].name;
}

struct Mesh
{
    std::vector<olc::vf4d>  pos;  // vertices
    std::vector<olc::Pixel> col;  // one color per vertex
};

struct Body
{
    olc::vf4d pos       {0.0f, 0.0f, 0.0f, 1.0f};
    olc::vf4d velocity  {0.0f, 0.0f, 0.0f, 0.0f};
    olc::vf4d size      {0.8f, 0.8f, 0.8f, 0.0f};  // full size
    olc::vf4d halfSize  {0.4f, 0.4f, 0.4f, 0.0f};  // half size
    olc::Pixel tint = olc::Colour::WHITE;

    Gravity gravity = Gravity::NegY;
    Mesh* mesh = {nullptr};
};


class Downshift : public olc::PixelGameEngine
{
public:
	Downshift()
	{
		sAppName = "Downshift";
	}

    Mesh hall;
    Mesh cube;
    olc::utils::Camera3D    cam;

    // Add one face as two triangles.  Corners a,b,c,d go clockwise seen from outside.
    void Face(Mesh& mesh, olc::vf4d a, olc::vf4d b, olc::vf4d c, olc::vf4d d, olc::Pixel color)
    {
        for (auto v : { a, b, c,  a, c, d })
        {
            mesh.pos.push_back({v.x, v.y, v.z, 1.0f });
            mesh.col.push_back(color);
        }
    }

    void Wall(Mesh& mesh, olc::vf4d start, olc::vf4d x_step, olc::vf4d w_step, 
              int x_max, int w_max, olc::Pixel color, bool cw = true)
    {
        float mc = 1.0f;  // mulitply color
        for (int i=0; i<x_max; i++) 
        {
            for (int j=0; j<w_max; j++)
            {
                if ((i+j) & 1) mc=0.85f; else mc=1.0f;
                olc::vf4d offset = start + (x_step * i) + (w_step * j);
                if (cw)
                    Face(mesh, offset, offset+x_step, offset+x_step+w_step, offset+w_step, color * mc);
                else
                    Face(mesh, offset, offset+w_step, offset+x_step+w_step, offset+x_step, color * mc);
            }
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

        // Draw Floor -Y Bottom
        Wall(hall, {0,-hw,-hw}, {1, 0, 0, 0}, {0, 0, 1, 0}, length, width, olc::Pixel( 60, 60,  60), true);
        // Draw Ceiling +Y Top
        Wall(hall, {0, hw,-hw}, {1, 0, 0, 0}, {0, 0, 1, 0}, length, width, olc::Pixel( 220, 220,  220), false);
        // Draw Left Wall, +Z North
        Wall(hall, {0,-hw, hw}, {1, 0, 0, 0}, {0, 1, 0, 0}, length, width, olc::Pixel( 60, 60,  150), true);
        // Draw Right Wall, -Z South
        Wall(hall, {0,-hw, -hw}, {1, 0, 0, 0}, {0, 1, 0, 0}, length, width, olc::Pixel( 150, 60,  60), false);

        // Draw End of Tunnel, +X East
        Wall(hall, {xe,-hw, -hw}, {0, 0, 1, 0}, {0, 1, 0, 0}, width, width, olc::Pixel( 60, 150,  60), false);
        // Draw End of Tunnel, -X West
        Wall(hall, {xw,-hw, -hw}, {0, 0, 1, 0}, {0, 1, 0, 0}, width, width, olc::Pixel( 150, 150,  60), true);

        cam.SetPerspective(60.0f * PI / 180.0f, float(ScreenSize().x) / ScreenSize().y, 0.1f, 100.0f);
        cam.SetTarget({xe/2, 0, 0});    // look down the hall from middle
        cam.SetDistance(2.0f);          // from 2 units away
        cam.SetYaw(-PI/2);              // turned so we down X axis
        cam.SetPitch(0.0f);             // level view
        cam.SetYawEaseRate(5.0f);
		return true;
	}

	// Called every frame, so update things here
	bool OnUserUpdate(float dt) override
	{
        // Escape quits the game
        if (keyboard.GetKey(olc::Key::ESCAPE).bPressed) return false;

        if (keyboard.GetKey(olc::Key::Q).bPressed)
        {
            cam.TurnYaw(PI);
        }

        cam.Update(dt);
        cam.Apply(draw);        // sets view + projection

		// Clear screen to dark blue
		draw.Clear(olc::Colour::VERY_DARK_BLUE);

        draw.SetCullMode(olc::CullMode::ClockWise);
        draw.EnableDepth(true);

        olc::mf4d model;        // identity: cube sits at the origin
        draw.SetModelMatrix(model);
        draw.Mesh(olc::Structure::List, hall.pos, hall.col);

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

