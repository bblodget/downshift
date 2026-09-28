/*
    Downshift: Escape a 3D maze by changing gravity.

    OLC CodeJam 2026 entry.

    Copyright (c) 2026 Brandon Blodget
    License: OLC-3, see LICENSE.md for details.
*/


// Define OLC_PGE3_APPLICATION to include the implementation of 
// the Pixel Game Engine as part of this translation unit
#define OLC_PGE3_APPLICATION
#include "olcPixelGameEngine3.h"
#include "camera3D.h"
#include <numbers>
#include <sstream>
#include <iomanip>
#include <algorithm>

constexpr float PI = std::numbers::pi_v<float>;
constexpr float gravityStrength = 40.0f;  // World units / second^2

std::string ToString(const olc::vf4d& v)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << "(" << v.x << ", " << v.y << ", "
        << v.z << ", " << v.w << ")";
    return out.str();
}

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

// The order matches the enum class Gravity
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
    olc::vf4d halfSize  {0.4f, 0.4f, 0.4f, 0.0f};  // half size
    olc::Pixel tint = olc::Colour::WHITE;

    Gravity gravity = Gravity::NegY;
    const Mesh* mesh = nullptr;
    bool grounded = true;

    void SetGravity(Gravity g)
    {
        if (gravity != g)
        {
            grounded = false;
            gravity = g;
        }
    }
};

// Add one face as two triangles.  Note, we are using clockwise culling.
void Face(Mesh& mesh, olc::vf4d a, olc::vf4d b, olc::vf4d c, olc::vf4d d, olc::Pixel color)
{
    for (auto v : { a, b, c,  a, c, d })
    {
        mesh.pos.push_back({v.x, v.y, v.z, 1.0f });
        mesh.col.push_back(color);
    }
}

// cw picks which side of the wall is visible (we cull ClockWise).
//   true:  visible from the side w_step x x_step points to
//   false: visible from the opposite side
// PGE3 coordinate system is Left handed.
// So if floor drawn with cw=true:
// Index Finger point +Z (w_step, into the screen) x Middle finger to right +X (x_step)
// Thumb point +Y, see the floor from above.
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

struct Hall
{
    int length = 30;
    int lanes = 3;
    Mesh mesh;
    float HalfWidth() const
    {
        return lanes * 0.5f;
    }
    void Build()
    {
        float xw = 0.0f;            // x west pos
        float xe = (float)length;  // x east pos
        float hw = HalfWidth();    

        // Empty the mesh before building
        mesh.pos.clear();
        mesh.col.clear();

        // Draw Floor -Y Bottom
        Wall(mesh, {0,-hw,-hw}, {1, 0, 0, 0}, {0, 0, 1, 0}, length, lanes, olc::Pixel( 60, 60,  60), true);
        // Draw Ceiling +Y Top
        Wall(mesh, {0, hw,-hw}, {1, 0, 0, 0}, {0, 0, 1, 0}, length, lanes, olc::Pixel( 220, 220,  220), false);
        // Draw Left Wall, +Z North
        Wall(mesh, {0,-hw, hw}, {1, 0, 0, 0}, {0, 1, 0, 0}, length, lanes, olc::Pixel( 60, 60,  150), true);
        // Draw Right Wall, -Z South
        Wall(mesh, {0,-hw, -hw}, {1, 0, 0, 0}, {0, 1, 0, 0}, length, lanes, olc::Pixel( 150, 60,  60), false);

        // Draw End of Tunnel, +X East
        Wall(mesh, {xe,-hw, -hw}, {0, 0, 1, 0}, {0, 1, 0, 0}, lanes, lanes, olc::Pixel( 60, 150,  60), false);
        // Draw End of Tunnel, -X West
        Wall(mesh, {xw,-hw, -hw}, {0, 0, 1, 0}, {0, 1, 0, 0}, lanes, lanes, olc::Pixel( 150, 150,  60), true);
    }
};

class Downshift : public olc::PixelGameEngine
{
public:
    Downshift()
    {
        sAppName = "Downshift";
    }

    Hall hall;
    Mesh cube;
    olc::utils::Camera3D    cam;
    Body player;

    void BuildCubeMesh()
    {
        // Empty the cube mesh before building
        cube.pos.clear();
        cube.col.clear();

        float hw=0.5f;
        int length=1;
        int lanes=1;

        // Draw Cube -Y Bottom
        Wall(cube, {-hw, -hw,-hw}, {1, 0, 0, 0}, {0, 0, 1, 0}, length, lanes, olc::Colour::WHITE * 0.35f, false);
        // Draw Cube +Y Top
        Wall(cube, {-hw, hw,-hw}, {1, 0, 0, 0}, {0, 0, 1, 0}, length, lanes, olc::Colour::WHITE, true);
        // Draw Cube Left Wall, +Z North
        Wall(cube, {-hw,-hw, hw}, {1, 0, 0, 0}, {0, 1, 0, 0}, length, lanes, olc::Colour::WHITE * 0.75f , false);
        // Draw Cube Right Wall, -Z South
        Wall(cube, {-hw,-hw, -hw}, {1, 0, 0, 0}, {0, 1, 0, 0}, length, lanes, olc::Colour::WHITE * 0.6f, true);

        // Draw Back of Cube, +X East
        Wall(cube, {hw ,-hw, -hw}, {0, 0, 1, 0}, {0, 1, 0, 0}, lanes, lanes, olc::Colour::WHITE * 0.5f, true);
        // Draw Front of Cube, -X West
        Wall(cube, {-hw ,-hw, -hw}, {0, 0, 1, 0}, {0, 1, 0, 0}, lanes, lanes, olc::Colour::WHITE * 0.8f, false);
    }

    void ClampToSurface(Body& body, olc::vf4d dir, float min, float max)
    {
        float distance = body.pos.dot(dir);
        float clamped = std::clamp(distance, min, max);
        if (distance == clamped)
            return;

        // Correct only the position component along dir.
        body.pos += dir * (clamped - distance);

        // Remove only the velocity component along dir.
        body.velocity += dir * (-body.velocity.dot(dir));

        float gravityAlongDir = GravityDirection(body.gravity).dot(dir);

        if ((distance < min && gravityAlongDir < 0.0f) ||
            (distance > max && gravityAlongDir > 0.0f))
        {
            body.grounded = true;
        }

    }

    void BodyUpdate (Body& body, float dt)
    {

        if (!body.grounded)
        {
            body.velocity += GravityDirection(body.gravity) * gravityStrength * dt;
        }
        body.pos += body.velocity * dt;

        float max_value = hall.length - body.halfSize.x;
        float min_value = body.halfSize.x;
        ClampToSurface(body, {1.0f, 0.0f, 0.0f, 0.0f}, min_value, max_value);

        max_value = (hall.HalfWidth()-body.halfSize.y);
        min_value = -max_value;
        ClampToSurface(body, {0.0f, 1.0f, 0.0f, 0.0f}, min_value, max_value);

        max_value = (hall.HalfWidth()-body.halfSize.z);
        min_value = -max_value;
        ClampToSurface(body, {0.0f, 0.0f, 1.0f, 0.0f}, min_value, max_value);
    }


    // Called once at the start, so create things here
    bool OnUserCreate() override
    {
        // Build the Hall
        hall.Build();

        // Build the player
        BuildCubeMesh();
        player.mesh = &cube;
        player.pos = {hall.length/2.0f+2, -hall.HalfWidth() + player.halfSize.y, 0,};

        cam.SetPerspective(60.0f * PI / 180.0f, float(ScreenSize().x) / ScreenSize().y, 0.1f, 100.0f);
        cam.SetTarget({hall.length/2.0f, 0, 0});    // look down the hall from middle
        cam.SetDistance(2.0f);          // from 2 units away
        cam.SetYaw(-PI/2);              // turned so we down X axis
        cam.SetPitch(0.0f);             // level view
        cam.SetYawEaseRate(5.0f);
        return true;
    }

    // Called every frame, so update things here
    bool OnUserUpdate(float dt) override
    {
        dt =std::min(dt, 1.0f / 30.0f);

        // Escape quits the game
        if (keyboard.GetKey(olc::Key::ESCAPE).bPressed) return false;

        if (keyboard.GetKey(olc::Key::Q).bPressed)
        {
            cam.TurnYaw(PI);
        }


        if (keyboard.GetKey(olc::Key::K1).bPressed) player.SetGravity(Gravity::NegY);
        if (keyboard.GetKey(olc::Key::K2).bPressed) player.SetGravity(Gravity::PosZ);
        if (keyboard.GetKey(olc::Key::K3).bPressed) player.SetGravity(Gravity::PosY);
        if (keyboard.GetKey(olc::Key::K4).bPressed) player.SetGravity(Gravity::NegZ);

        // Update player
        BodyUpdate(player, dt);

        cam.Update(dt);
        cam.Apply(draw);        // sets view + projection

        // Clear screen to dark blue
        draw.Clear(olc::Colour::VERY_DARK_BLUE);

        draw.SetCullMode(olc::CullMode::ClockWise);
        draw.EnableDepth(true);

        // Rotate towards player gravity down
        olc::mf4d rot;        // identity: start
        rot.rotateX(static_cast<int>(player.gravity) * PI/2);    // rotate by 90 degrees

        // Draw Hallway
        draw.SetModelMatrix(rot);
        draw.Mesh(olc::Structure::List, hall.mesh.pos, hall.mesh.col);

        // Draw Player Cube
        olc::mf4d tr, sc;
        tr.translate(player.pos);
        olc::vf4d s = player.halfSize * 2;
        sc.scale(s.x, s.y, s.z);
        // Matrices apply right to left
        // So scale, then translate, then rotate
        draw.SetModelMatrix(rot * tr * sc);
        draw.Mesh(olc::Structure::List, player.mesh->pos, player.mesh->col, player.tint);

        // Draw Gravity Arrow
        draw.SetModelMatrix(rot);
        draw.EnableDepth(false);
        draw.Line(player.pos, player.pos + (GravityDirection(player.gravity)*1.25f),
                olc::Colour::TANGERINE, olc::Colour::TANGERINE);
        draw.EnableDepth(true);

        // Draw HUD
        draw.WorldReset();
		draw.String({ 2, 2 }, std::string("Gravity: ") + GravityName(player.gravity) 
                + "\nPosition: " + ToString(player.pos), olc::Colour::YELLOW); 

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

