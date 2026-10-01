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
#include <fstream>
#include <sstream>

constexpr float PI = std::numbers::pi_v<float>;
constexpr float gravityStrength = 40.0f;  // World units / second^2
constexpr float easeRate = 10.0f;       // roll ease rate.
constexpr float moveSpeed = 5.0f;
constexpr float camAhead = 1.0f;    // cam target ahead of the player
constexpr float camHeight = 0.5f;   // cam target above the player
constexpr float maxLashDist = 20.0f;

// Remove leading and trailing whitespace
std::string Trim(const std::string& text)
{
    auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";

    auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string ToString(const olc::vf4d& v)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << "(" << v.x << ", " << v.y << ", "
        << v.z << ", " << v.w << ")";
    return out.str();
}

enum class Motion 
{
    Grounded = 0,
    Falling  = 1,
    Lashing  = 2
};

enum class Gravity
{
    NegY = 0,  // floor
    PosZ = 1,  // left wall
    PosY = 2,  // ceiling
    NegZ = 3,  // right wall
};
constexpr Gravity allGravities[] =
{
    Gravity::NegY,
    Gravity::PosZ,
    Gravity::PosY,
    Gravity::NegZ
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

struct LashInfo
{
    bool hit;
    Gravity wall;
    olc::vf4d hit_point;
};

struct Body
{
    olc::vf4d pos       {0.0f, 0.0f, 0.0f, 1.0f};
    olc::vf4d velocity  {0.0f, 0.0f, 0.0f, 0.0f};
    olc::vf4d halfSize  {0.4f, 0.4f, 0.4f, 0.0f};  // half size
    olc::Pixel tint = olc::Colour::WHITE;

    Gravity gravity = Gravity::NegY;
    const Mesh* mesh = nullptr;
    Motion motion = Motion::Grounded;
    olc::vf4d pull_dir;

    void SetGravity(Gravity g)
    {
        gravity = g;
    }

    void Lash(Gravity wall, olc::vf4d hit_point)
    {
        SetGravity(wall);
        pull_dir = (hit_point - pos).norm();
        pull_dir.w = 0.0f;
        motion = Motion::Lashing;
    }

    void Land()
    {
        motion = Motion::Grounded;
    }

    void Fall(Gravity wall)
    {
        SetGravity(wall);
        motion = Motion::Falling;
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

struct HallPosition
{
    float slice;
    float lane;
};

struct Hall
{
    int length = 0;
    int lanes = 0;
    Mesh mesh;
    std::string name = "None";
    float par = 0.0f;
    HallPosition start = {0, 0};

    using Slice = std::array<std::string, 4>;
    std::vector<Slice> slices;


    float HalfWidth() const
    {
        return lanes * 0.5f;
    }


    bool Load(const std::string& path)
    {

        bool isName = false;
        bool isPar = false;
        bool isStart = false;
        int lineno = 0;

        // Reset member variables
        length = 0;
        lanes = 0;
        name = "None";
        par = 0.0f;
        start = {0, 0};
        slices.clear();

        auto pos = path.find_last_of("/\\");
        std::string filename =
            (pos == std::string::npos) ? path : path.substr(pos + 1);

        std::ifstream file(path);

        // Check if file exists
        if (!file)
        {
            // Could not open file
            std::cerr << filename
                << ": Could not open file" << std::endl;
            return false;
        }

        // Process all the lines
        std::string line;
        while (std::getline(file, line))
        {
            lineno++;  // increment the line number

            // Remove Comment from line
            auto comment = line.find('#');
            if (comment != std::string::npos)
            {
                line.erase(comment);
            }

            // Trim
            line = Trim(line);
            if (line == "") continue;   // skip empty lines

            auto equals = line.find('=');
            if (equals != std::string::npos)
            {
                std::string key = Trim(line.substr(0, equals));
                std::string value = Trim(line.substr(equals + 1));

                if (key == "name")
                {
                    name = value;
                    isName = true;
                }
                else if (key == "par")
                {
                    try 
                    {
                        par = std::stof(value);
                        isPar = true;
                    } catch (const std::exception&)
                    {
                        std::cerr << filename << ":" << lineno 
                            << ": Error processing par =" << std::endl;
                        return false; // invalid
                    }
                }
                else if (key == "start")
                {
                    std::istringstream parser(value);
                    float slice, lane;

                    if (!(parser >> slice >> lane))
                    {
                        std::cerr << filename << ":" << lineno 
                            << ": Error processing start =" << std::endl;
                        return false;  // missing or invalid values
                    }
                    start.slice = slice;
                    start.lane = lane;
                    isStart = true;
                }
            } else {
                // Handle the map lines
                std::istringstream parser(line);
                Slice slice;  // One row: four strings

                if (!(parser >> slice[0] >> slice[1] >> slice[2] >> slice[3]))
                {
                    std::cerr << filename << ":" << lineno 
                        << ": Error with map line" << std::endl;
                    return false;
                }
                // More lanes?
                std::string extra;
                if (parser >> extra)
                {
                    std::cerr << filename << ":" << lineno 
                        << ": Error: more than 4 surfaces" << std::endl;
                    return false;
                }

                // Check the Slice
                auto slanes = slice[0].length();
                if (slice[1].length() != slanes 
                        || slice[2].length() != slanes
                        || slice[3].length() != slanes)
                {
                    std::cerr << filename << ":" << lineno 
                        << ": Error different lane widths" << std::endl;
                    return false;
                }
                if (lanes == 0)
                {
                    // First time being set
                    lanes = slanes;
                } else
                {
                    // Check same lane width as previous slices
                    if (lanes != (int)slanes)
                    {
                        std::cerr << filename << ":" << lineno 
                            << ": Error slices have different lane widths" << std::endl;
                        return false;
                    }
                }
                slices.push_back(slice);
                length++;       // increase hall length by 1 sliace
            }
        }
        if (length == 0)
        {
            std::cerr << filename
                << ": Error: Hall length is zero." << std::endl;
            return false;
        }

        if (!isName)
        {
            std::cerr << filename
                << ": Error: No level name specified." << std::endl;
            return false;
        }
        if (!isPar)
        {
            std::cerr << filename
                << ": Error: No Par time specified." << std::endl;
            return false;
        }
        if (!isStart)
        {
            std::cerr << filename
                << ": Error: No Start tile info specified." << std::endl;
            return false;
        }
        if (start.slice < 0.0f || start.slice > (length - 1))
        {
            std::cerr << filename
                << ": Error: Invalid start.slice." << std::endl;
            return false;
        }
        if (start.lane < 0.0f || start.lane > (lanes - 1))
        {
            std::cerr << filename
                << ": Error: Invalid start.lane." << std::endl;
            return false;
        }
       
        return true;
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
    float roll = 0.0f;
    float facing = 1.0f;
    olc::utils::Camera3D::Ray ray;

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
            body.Land();
        }

    }

    void BodyUpdate (Body& body, float dt)
    {

        if (body.motion == Motion::Lashing)
        {
            body.velocity += body.pull_dir * gravityStrength * dt;
        }
        else if (body.motion == Motion::Falling)
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

    float RollTarget() const
    {
        return static_cast<int>(player.gravity) * PI/2;
    }

    bool IsRolling() const { return std::abs(olc::utils::Camera3D::WrapAngle(RollTarget() - roll)) > 0.01f; }

    olc::vf4d Forward () const
    {
        return {facing, 0.0f, 0.0f, 0.0f};
    }

    void SetCameraTarget(const olc::mf4d& rot)
    {
        const olc::vf4d screenUp = {0.0f, 1.0f, 0.0f, 0.0f};
        const olc::vf4d forward = Forward();

        olc::vf4d cam_target = (rot * player.pos) + (forward * camAhead) + (screenUp * camHeight);

        const float margin = 0.1f;
        const float offsetX =
            forward.x * cam.GetDistance() * std::cos(cam.GetPitch());

        float eyeX = cam_target.x - offsetX;
        eyeX = std::clamp(eyeX, margin, float(hall.length) - margin);

        cam_target.x = eyeX + offsetX;
        cam.SetTarget(cam_target);
    }

    LashInfo CheckWalls(const olc::utils::Camera3D::Ray& ray)
    {
        float min_t = maxLashDist;
        Gravity min_gravity = Gravity::NegY;
        bool hit = false;
        olc::vf4d hit_point;

        for (Gravity gravity : allGravities)
        {
            olc::vf4d g = GravityDirection(gravity);
            float dg = ray.dir.dot(g);
            if (dg <= 0.0f ) continue;
            float t = (hall.HalfWidth() - ray.origin.dot(g)) / dg;
            if (t>0 && t < min_t)
            {
                min_t = t;
                min_gravity = gravity;
                hit = true;
            }
        }

        if (hit)
        {
            hit_point = ray.origin + (ray.dir * min_t);

            // Remove ends of the hall from lashes
            if (hit_point.x >= hall.length || hit_point.x <=0)
            {
                hit = false;
            }
        }


        return {hit, min_gravity, hit_point};
    }


    // Called once at the start, so create things here
    bool OnUserCreate() override
    {
        // Build the Hall
        if (!hall.Load("./assets/levels/level01.txt"))
        {
            return false;
        }
        hall.Build();

        // Build the player
        BuildCubeMesh();
        player.mesh = &cube;

        player.pos = {hall.start.slice + 0.5f, 
                      -hall.HalfWidth() + player.halfSize.y,
                      (hall.HalfWidth() - 0.5f) - hall.start.lane };

        cam.SetPerspective(75.0f * PI / 180.0f, float(ScreenSize().x) / ScreenSize().y, 0.1f, 100.0f);
        cam.SetDistance(3.0f);          // distance in units
        cam.SetYaw(-PI/2);              // turned so we down X axis
        cam.SetPitch(PI/18);            // Look down at 10 degrees.
        cam.SetYawEaseRate(5.0f);
        SetCameraTarget(olc::mf4d());
        return true;
    }


    // Called every frame, so update things here
    bool OnUserUpdate(float dt) override
    {
        dt =std::min(dt, 1.0f / 30.0f);

        /************** Check Controls ****************/

        // Escape quits the game
        if (keyboard.GetKey(olc::Key::ESCAPE).bPressed) return false;

        if (keyboard.GetKey(olc::Key::Q).bPressed)
        {
            cam.TurnYaw(PI);
            facing = -facing;
        }

        if (keyboard.GetKey(olc::Key::K1).bPressed) player.Fall(Gravity::NegY);
        if (keyboard.GetKey(olc::Key::K2).bPressed) player.Fall(Gravity::PosZ);
        if (keyboard.GetKey(olc::Key::K3).bPressed) player.Fall(Gravity::PosY);
        if (keyboard.GetKey(olc::Key::K4).bPressed) player.Fall(Gravity::NegZ);


        // Check WASD only when grounded
        if (player.motion != Motion::Lashing)
        {

            olc::vf4d forward = Forward();

            // Remove the velocity component along forward direction
            player.velocity += forward * (-player.velocity.dot(forward));
            if (keyboard.GetKey(olc::Key::W).bHeld) player.velocity += forward * moveSpeed;
            if (keyboard.GetKey(olc::Key::S).bHeld) player.velocity += forward * -moveSpeed;

            olc::vf4d surfaceUp = -GravityDirection(player.gravity);
            olc::vf4d right_dir = surfaceUp.cross(forward);

            // Remove only the velocity component along right_dir.
            player.velocity += right_dir * (-player.velocity.dot(right_dir));

            if (keyboard.GetKey(olc::Key::A).bHeld)
            {
                player.velocity += right_dir * -moveSpeed;
            }
            if (keyboard.GetKey(olc::Key::D).bHeld)
            {
                player.velocity += right_dir * moveSpeed;
            }
        }


        /************** Game State Update ****************/

        // Update player
        BodyUpdate(player, dt);

        // Update Hall
        // Rotate towards player gravity down
        olc::mf4d rot;        // identity: start
        if (IsRolling())
        {
            float t = std::min(1.0f, easeRate * dt);
            roll += olc::utils::Camera3D::WrapAngle(RollTarget() - roll) * t;
            if (!IsRolling()) roll = RollTarget();
        }
        // Rotate towards player gravity
        rot.rotateX(roll);

        // Camera Update
        // roll cam target with the room and add offset
        SetCameraTarget(rot);
        cam.Update(dt);
        cam.Apply(draw);        // sets view + projection

        // Compute mouse ray every frame
        ray = cam.ScreenToRay(mouse.GetPosition(), ScreenSize());

        // Translate ray to native hall cordinates
        olc::mf4d ray_roll;
        ray_roll.rotateX(-roll);
        ray.origin = ray_roll * ray.origin;
        ray.dir = ray_roll * ray.dir;

        // Check Walls
        LashInfo lash = CheckWalls(ray);

        bool newWall = lash.wall != player.gravity;
        if (mouse.GetButton(0).bPressed && newWall && lash.hit)
        {
            player.Lash(lash.wall, lash.hit_point);
        }

        /************** Drawing ****************/

        // Clear screen to dark blue
        draw.Clear(olc::Colour::VERY_DARK_BLUE);

        draw.SetCullMode(olc::CullMode::ClockWise);
        draw.EnableDepth(true);

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
        if (lash.hit)
        {
            draw.String({2, 20}, "Hit Point: " + ToString(lash.hit_point) + "\n" +
                    "Hit Floor: " + GravityName(lash.wall) + "\n"
                    ,olc::Colour::YELLOW);
        }

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

