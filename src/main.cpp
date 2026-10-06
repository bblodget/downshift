/*
    Downshift: 3D speed-run through hallways in space.

    OLC CodeJam 2026 entry.

    Copyright (c) 2026 Brandon Blodget
License: OLC-3, see LICENSE.md for details.
*/


// Define OLC_PGE3_APPLICATION to include the implementation of 
// the Pixel Game Engine as part of this translation unit
#define OLC_PGE3_APPLICATION
#include "olcPixelGameEngine3.h"
#include "miniaudio.h"
#include "olcPGEX3_Miniaudio.h"
#include "camera3D.h"
#include <numbers>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <functional>
#include <cmath>
#include <limits>
#include <random>

constexpr float PI = std::numbers::pi_v<float>;
constexpr float gravityStrength = 40.0f;  // World units / second^2
constexpr float easeRate = 10.0f;       // roll ease rate.
constexpr float moveSpeed = 5.0f;
constexpr float camAhead = 1.0f;    // cam target ahead of the player
constexpr float camHeight = 0.5f;   // cam target above the player
constexpr float maxLashDist = 20.0f;
constexpr float tolerance = 0.01f;  // floating point tolerance
constexpr float fallTime = 1.5f;  // Fall before respawn
constexpr float spinSpeed = 2.0f; // rad/sec
constexpr float spinAngle = (2.0f * PI)/spinSpeed;  // radians
constexpr int coinSides = 12;
constexpr float coinThickness = 0.075f;
constexpr float coinRadius = 0.25f;
constexpr olc::Pixel coinColor = olc::Pixel(255,200, 40);
constexpr float pickupRadius = 0.85f;
constexpr float jumpSpeed = 12.0f; 
constexpr float lift = coinRadius + 0.60f;
constexpr float coyoteTime = 0.12f;
constexpr float drawShrink = 0.98f;
constexpr float musicVolume = 0.5f;

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

enum class MusicState
{
    Paused = 0,
    Intro  = 1,
    Loop   = 2,
    Disabled = 3
};

enum class GameState
{
    Intro = 0,
    Playing = 1,
    Complete = 2,
    End = 3
};

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
    olc::vf4d leftDir;
    const char* name;
};

// The order matches the enum class Gravity
const GravityInfo gravityTable[] =
{
    {{0.0f, -1.0f,  0.0f, 0.0f}, {0.0f,  0.0f,  1.0f, 0.0f}, "floor"},
    {{0.0f,  0.0f,  1.0f, 0.0f}, {0.0f,  1.0f,  0.0f, 0.0f}, "left wall"},
    {{0.0f,  1.0f,  0.0f, 0.0f}, {0.0f,  0.0f, -1.0f, 0.0f}, "ceiling"},
    {{0.0f,  0.0f, -1.0f, 0.0f}, {0.0f, -1.0f,  0.0f, 0.0f}, "right wall"}
};

// Level Data
const std::string levelFiles[] =
{
    "./assets/levels/level01.txt",
    "./assets/levels/level02.txt",
    "./assets/levels/level03.txt",
    "./assets/levels/level04.txt",
    "./assets/levels/level05.txt",
};

olc::vf4d GravityDirection(Gravity gravity)
{
    return gravityTable[static_cast<int>(gravity)].direction;
}

olc::vf4d GravityLeftDir(Gravity gravity)
{
    return gravityTable[static_cast<int>(gravity)].leftDir;
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

struct Coin 
{
    olc::vf4d pos = {0.0f, 0.0f, 0.0f, 1.0f};
    Gravity surface = Gravity::NegY;
    bool collected = false;
};

struct Body
{
    olc::vf4d pos       {0.0f, 0.0f, 0.0f, 1.0f};
    olc::vf4d velocity  {0.0f, 0.0f, 0.0f, 0.0f};
    olc::vf4d halfSize  {0.4f, 0.4f, 0.4f, 0.0f};  // half size
    olc::Pixel tint = olc::Colour::WHITE;
    float coyoteTimer = coyoteTime;

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
        coyoteTimer = coyoteTime;
        motion = Motion::Grounded;
    }

    void Fall(Gravity wall)
    {
        SetGravity(wall);
        motion = Motion::Falling;
    }

    void Jump()
    {
        olc::vf4d surfaceUp = -GravityDirection(gravity);
        velocity += surfaceUp * jumpSpeed;
        motion = Motion::Falling;
        coyoteTimer = 0.0f;
    }


};

void Triangle(Mesh& mesh, olc::vf4d a, olc::vf4d b, olc::vf4d c, olc::Pixel color)
{
    for (auto v: { a, b, c})
    {
        mesh.pos.push_back({v.x, v.y, v.z, 1.0f});
        mesh.col.push_back(color);
    }
}


// Add one face as two triangles.  Note, we are using clockwise culling.
void Face(Mesh& mesh, olc::vf4d a, olc::vf4d b, olc::vf4d c, olc::vf4d d, olc::Pixel color)
{
    Triangle(mesh, a, b, c, color);
    Triangle(mesh, a, c, d, color);
}

// cw picks which side of the wall is visible (we cull ClockWise).
//   true:  visible from the side w_step x x_step points to
//   false: visible from the opposite side
// PGE3 coordinate system is Left handed.
// So if floor drawn with cw=true:
// Index Finger point +Z (w_step, into the screen) x Middle finger to right +X (x_step)
// Thumb point +Y, see the floor from above.
void Wall(Mesh& mesh, olc::vf4d start, olc::vf4d x_step, olc::vf4d w_step, 
          int x_max, int w_max, olc::Pixel color, bool cw = true,
          std::function<bool(int, int)> isSolid = {})
{
    float mc = 1.0f;  // mulitply color
    for (int i=0; i<x_max; i++) 
    {
        for (int j=0; j<w_max; j++)
        {
            if (isSolid && !isSolid(i,j))
                continue;  // skip missing tiles

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
    Mesh gateMesh;
    std::string name = "None";
    float parTime = 0.0f;
    HallPosition start = {0, 0};

    using Slice = std::array<std::string, 4>;
    std::vector<Slice> slices;

    std::vector<Coin> coins;
    std::vector<std::string> levelText;

    bool GateOpen() const
    {
        return (CoinsCollected() == static_cast<int>(coins.size()));
    }


    int CoinsCollected() const
    {
        int count = 0;
        for (const Coin& coin : coins)
        {
            if (coin.collected)
                count++;
        }
        return count;
    }

    bool Contains(const olc::vf4d& pos) const
    {
        return (pos.x < length && pos.x > 0
                    && pos.y < HalfWidth() + tolerance
                    && pos.y > -HalfWidth() - tolerance
                    && pos.z < HalfWidth() + tolerance
                    && pos.z > -HalfWidth() - tolerance
               );
    }

    float HalfWidth() const
    {
        return lanes * 0.5f;
    }

    bool IsSolid(Gravity surface, int slice , int lane) const
    {
        // Nothing solid outside the hall.
        if (slice < 0 || slice > length-1 
                || lane <0 || lane > lanes - 1)
        {
            return false;
        }
        const auto& slice_row = slices.at(slice);
        const auto& surface_row = slice_row[static_cast<int>(surface)];
        auto tile_char = surface_row[lane];
        return tile_char == '*' || tile_char == 'c';
    }

    bool IsSolidAt(Gravity surface, const olc::vf4d& pos) const
    {
        olc::vf4d left_dir = GravityLeftDir(surface); 
        int slice = static_cast<int>(std::floor(pos.x));
        int lane = static_cast<int>(std::floor(HalfWidth() - pos.dot(left_dir)));

        return IsSolid(surface, slice, lane);
    }

    void LoadCoins()
    {
        coins.clear();
        int slice_num = 0;

        // Loop through all the hall slices
        for (const Slice& s : slices)
        {
            // Loop through all the gravities
            for (Gravity gravity : allGravities)
            {
                int i = static_cast<int>(gravity);
                olc::vf4d g = GravityDirection(gravity);
                olc::vf4d lg = GravityLeftDir(gravity);

                // Check the slice for coins
                for (auto p = s[i].find_first_of("co");
                        p != std::string::npos;
                        p = s[i].find_first_of("co", p + 1))
                {
                    // Create a coin at this slice index (p)
                    Coin coin;
                    coin.surface = gravity;
                    coin.pos.x = slice_num + 0.5f;
                    coin.pos += g *(HalfWidth() - lift);
                    coin.pos += lg * (HalfWidth() - 0.5f 
                            - static_cast<float>(p));
                    coins.push_back(coin);
                }
            }
            slice_num++;
        }
    }

    bool Load(const std::string& path)
    {

        bool isName = false;
        bool isPar = false;
        bool isStart = false;
        bool isText = false;
        int lineno = 0;

        // Reset member variables
        length = 0;
        lanes = 0;
        name = "None";
        parTime = 0.0f;
        start = {0, 0};
        slices.clear();
        levelText.clear();

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
                        parTime = std::stof(value);
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
                else if (key == "text")
                {
                    bool endtextFound = false;
                    while (std::getline(file, line))
                    {
                        if (line.starts_with("endtext"))
                        {
                            endtextFound = true;
                            break;
                        }
                        levelText.push_back(line);
                    }
                    if (!endtextFound)
                    {
                        std::cerr << filename << ":" << lineno 
                            << ": Error endtext not found =" << std::endl;
                        return false;
                    }
                    isText = true;
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
        if (!isText)
        {
            std::cerr << filename
                << ": Error: No text section found." << std::endl;
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
        LoadCoins();
        return true;
    }


    void Build()
    {
        float xe = (float)length;  // x east pos
        float hw = HalfWidth();    

        // Empty the mesh before building
        mesh.pos.clear();
        mesh.col.clear();
        gateMesh.pos.clear();
        gateMesh.col.clear();

        // Draw Floor -Y Bottom
        Wall(mesh, {0,-hw,-hw}, {1, 0, 0, 0}, {0, 0, 1, 0}, 
                length, lanes, olc::Pixel( 60, 60,  60), true,
                [&](int i, int j)
                { return IsSolid(Gravity::NegY, i, lanes - 1 - j); }
                );
        // Draw Ceiling +Y Top
        Wall(mesh, {0, hw,-hw}, {1, 0, 0, 0}, {0, 0, 1, 0}, 
                length, lanes, olc::Pixel( 220, 220,  220), false,
                [&](int i, int j)
                { return IsSolid(Gravity::PosY, i, j); }
                );
        // Draw Left Wall, +Z North
        Wall(mesh, {0,-hw, hw}, {1, 0, 0, 0}, {0, 1, 0, 0}, 
                length, lanes, olc::Pixel( 60, 60,  150), true,
                [&](int i, int j)
                { return IsSolid(Gravity::PosZ, i, lanes - 1 - j); }
                );
        // Draw Right Wall, -Z South
        Wall(mesh, {0,-hw, -hw}, {1, 0, 0, 0}, {0, 1, 0, 0}, 
                length, lanes, olc::Pixel( 150, 60,  150), false,
                [&](int i, int j)
                { return IsSolid(Gravity::NegZ, i, j); }
                );

        // Draw the Gate!
        // Draw End of Tunnel, +X East
        Wall(gateMesh, {xe,-hw, -hw}, {0, 0, 1, 0}, {0, 1, 0, 0}, 
                lanes, lanes, olc::Pixel( 220, 220,  220), false);
    }
};

class Downshift : public olc::PixelGameEngine
{
public:
    Downshift()
    {
        sAppName = "Downshift";
        if (!InstallSystemExtension(&audio))
        {
            std::cerr 
                << "Warning: Audio Extension failed to load, no sound" 
                << std::endl;
        }
    }

    Hall hall;
    Mesh cube;
    Mesh crystal;
    Mesh coinMesh;
    olc::utils::Camera3D    cam;
    Body player;
    float roll = 0.0f;
    float facing = 1.0f;
    olc::utils::Camera3D::Ray ray;
    float outTime = 0.0f;
    float spinTime = 0.0f;
    float levelTime = 0.0f;
    bool showDebug = false;
    bool levelStart = false;
    std::string completionComment = "How did this get here?";
    int levelIndex = 0;
    GameState gameState = GameState::Intro;
    olc::ext::Miniaudio::AudioEngine audio;
    olc::ext::Miniaudio::Sound sndCoin, sndFall, sndGate;
    olc::ext::Miniaudio::Sound  sndJump, sndLand, sndLash;
    olc::ext::Miniaudio::Sound  sndParBeat, sndParMissed;
    olc::ext::Miniaudio::Sound sndTheme, sndThemeIntro;
    bool gateWasOpen = false;
    MusicState musicState = MusicState::Paused;

    void StartIntroMusic()
    {
        if (musicState != MusicState::Disabled)
        {
            musicState = MusicState::Intro;
            sndThemeIntro.Seek(0.0f);
            sndThemeIntro.SetVolume(musicVolume);
            sndThemeIntro.Play(false);
        }
    }

    void StartLoopMusic()
    {
        if (musicState != MusicState::Disabled)
        {
            sndThemeIntro.Pause();
            musicState = MusicState::Loop;
            sndTheme.Seek(0.0f);
            sndTheme.SetVolume(musicVolume);
            sndTheme.Play(true);
        }
    }

    void DrawPanel(const std::vector<std::string>& lines,
            olc::Pixel color = olc::Colour::YELLOW)
    {
        if (lines.empty())
            return;

        const std::string title = "Downshift";
        const vf2d titleScale = {1.2f, 1.2f};
        std::string text = "\n\n";

        if (gameState == GameState::Intro)
        {
            text += "Level " + std::to_string(levelIndex+1) + ": ";
            text += hall.name + '\n';
            text += "Par time: " + std::to_string(int(hall.parTime))
                + " seconds \n";
            text += "\n";
        }

        for (const auto& line : lines)
        {
            text += line + '\n';
        }
        if (gameState == GameState::End)
        {
            text += "\nPress spacebar to play again";
        }
        else
        {
            if (gameState == GameState::Intro)
            {
                text += "\nPress N to skip to next level";
            }
            text += "\nPress spacebar to continue";
            if (gameState == GameState::Complete)
            {
                text += "\nPress R to replay this level";
            }
        }

        const float padding = 8.0f;

        const auto titleSize = draw.GetTextSize(title, false, titleScale);
        const auto textSize = draw.GetTextSize(text);

        olc::vf2d panelSize 
        {
            std::max(textSize.x, titleSize.x) + padding * 2,
            titleSize.y + textSize.y + padding * 2
        };

        olc::vf2d origin
        {
            (ScreenSize().x - panelSize.x) * 0.5f,
            (ScreenSize().y - panelSize.y) * 0.5f
        };

        olc::vf2d textOrgin
        {
            origin.x + padding,
            origin.y + padding
        };


        // TODO: Draw background and border using origin and panelSize.
        draw.FilledRoundedRect(origin, panelSize, 10.0f, olc::Pixel(16, 16, 16, 192));

        // Remove the title's characters, preserving its line and spacing.
        //
        draw.String(
                {origin.x + padding, origin.y + padding},
                title, color, titleScale);

        draw.String(
                {origin.x + padding, origin.y + titleSize.y +  padding},
                text, color);
    }

    bool LastLevel()
    {
        return levelIndex == static_cast<int>(std::size(levelFiles))-1;
    }

    bool LoadLevel(int index)
    {
        if (index >= static_cast<int>(std::size(levelFiles))
                || index < 0)
        {
            return false;
        }
        Hall tmpHall;
        if (tmpHall.Load(levelFiles[index]))
        {
            hall = std::move(tmpHall);
            hall.Build();
            ResetLevel();
            levelIndex = index;
            return true;
        }
        // Load failed so restart previous working level
        ResetLevel();
        return false;
    }

    std::string CompletionComment(bool beatPar)
    {
        static std::mt19937 rng {std::random_device{}()};

        static const char* fast[] =
        {
            "Gravity-defying speed!",
            "You made that look easy.",
            "The hallway never stood a chance.",
            "Downshift? More like overdrive!"
        };

        static const char* slow[] =
        {
            "Taking the scenic route?",
            "All change collected. Eventually.",
            "Gravity was working overtime.",
            "Next time, less sightseeing!"
        };

        std::uniform_int_distribution<int> pick(0,3);
        return beatPar ? fast[pick(rng)] : slow[pick(rng)];
    }

    void CollectCoins()
    {
        for (Coin& coin : hall.coins)
        {
            // skip collected coins
            if (coin.collected)
                continue;

            // Check if we are close to the coin
            if ((player.pos - coin.pos).mag() < pickupRadius)
            {
                coin.collected = true;
                sndCoin.Play();
            }
        }
    }

    void DrawCoins(const olc::mf4d& rot)
    {
        for (const Coin& coin : hall.coins)
        {
            // skip collected coins
            if (coin.collected)
                continue;

            olc::mf4d ctr;
            ctr.translate(coin.pos);

            olc::mf4d surfaceRot;
            surfaceRot.rotateX(
                -static_cast<int>(coin.surface) * PI / 2.0f);

            olc::mf4d crot;
            crot.rotateY(spinTime * spinSpeed);

            draw.SetModelMatrix(rot * ctr * surfaceRot * crot);
            draw.Mesh(olc::Structure::List, coinMesh.pos, coinMesh.col);
        }
    }

    void BuildCoinMesh(float thickness, float r, olc::Pixel color)
    {
        // Empty the coin mesh before building
        coinMesh.pos.clear();
        coinMesh.col.clear();

        std::array<olc::vf4d, coinSides> in_front, in_back;
        std::array<olc::vf4d, coinSides> out_front, out_back;
        float ht = thickness/2;     // the half width
        float angle = 2 * PI / coinSides;
        float inner = r * 0.8f;
        float outer = ht * 1.05f;

        for (int i=0; i<coinSides; i++)
        {
            float xi = inner * std::cos(i * angle);
            float yi = inner * std::sin(i * angle);
            float xo = r * std::cos(i * angle);
            float yo = r * std::sin(i * angle);
            in_front[i] = {xi, yi,  ht};
            in_back[i]  = {xi, yi, -ht};
            out_front[i] = {xo, yo,  outer};
            out_back[i]  = {xo, yo, -outer};
        }

        olc::vf4d fcenter = {0.0f, 0.0f, ht};
        olc::vf4d bcenter = {0.0f, 0.0f, -ht};

        for (int i=0; i<coinSides; i++)
        {
            int j=(i+1)%coinSides;
            // Inner coinMesh
            Triangle(coinMesh, fcenter, in_front[i], in_front[j], color);
            Triangle(coinMesh, bcenter, in_back[j], in_back[i], color);

            // Outer coinMesh
            Face(coinMesh, in_front[j], in_front[i], out_front[i], out_front[j], color * 0.8f);
            Face(coinMesh, in_back[i], in_back[j], out_back[j], out_back[i], color * 0.8f);

            // Coin rim
            Face(coinMesh, out_front[j], out_front[i], out_back[i], out_back[j], color * 0.6f);
        }
    }

    void BuildCrystalMesh(float h, float r, olc::Pixel color)
    {
        // Empty the crystal mesh before building
        crystal.pos.clear();
        crystal.col.clear();

        // Points
        olc::vf4d h1 = {0.0f, h   , 0.0f};
        olc::vf4d h2 = {0.0f, -h  , 0.0f};
        olc::vf4d r1 = {r   , 0.0f, 0.0f};
        olc::vf4d r2 = {0.0f, 0.0f, r   };
        olc::vf4d r3 = {-r  , 0.0f, 0.0f};
        olc::vf4d r4 = {0.0f, 0.0f, -r  };

        float light = 1.0f;
        float dark = 0.85f;

        // Triangles
        Triangle(crystal, h1, r2, r1, color * light);
        Triangle(crystal, h1, r3, r2, color * dark);
        Triangle(crystal, h1, r4, r3, color * light);
        Triangle(crystal, h1, r1, r4, color * dark);

        light = 0.64f;
        dark = 0.5f;

        Triangle(crystal, h2, r1, r2, color * light);
        Triangle(crystal, h2, r2, r3, color * dark);
        Triangle(crystal, h2, r3, r4, color * light);
        Triangle(crystal, h2, r4, r1, color * dark);
    }

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

    void ClampToSurface(Body& body, const olc::vf4d& dir, float min, float max,
            bool min_solid, bool max_solid, const olc::vf4d& prev_body_pos)
    {
        float previous = prev_body_pos.dot(dir);
        float distance = body.pos.dot(dir);

        bool hitMin = min_solid
            && previous >= min - tolerance // previous above surface
            && distance < min;  // current below surface

        bool hitMax = max_solid
            && previous <= max + tolerance // previous below surface
            && distance > max;  // current above surface

        if (!hitMin && !hitMax)
            return;

        // Min or Max bound?
        float bound = hitMin ? min : max;

        // Correct only the position component along dir.
        body.pos += dir * (bound - distance);

        // Remove only the velocity component along dir.
        body.velocity += dir * (-body.velocity.dot(dir));

        float gravityAlongDir = GravityDirection(body.gravity).dot(dir);

        if ((hitMin && gravityAlongDir < 0.0f) ||
            (hitMax && gravityAlongDir > 0.0f))
        {
            body.Land();
        }

    }

    void BodyUpdate (Body& body, float dt)
    {
        olc::vf4d prev_body_pos = body.pos;

        if (body.motion == Motion::Grounded)
        {
            if (!hall.IsSolidAt(body.gravity, body.pos))
            {
                body.Fall(body.gravity);
            }
        }

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
        // min_solid is false. open back: no start wall
        ClampToSurface(body, {1.0f, 0.0f, 0.0f, 0.0f}, min_value, max_value,
                false, true, prev_body_pos);

        max_value = (hall.HalfWidth()-body.halfSize.y);
        min_value = -max_value;
        ClampToSurface(body, {0.0f, 1.0f, 0.0f, 0.0f}, min_value, max_value,
                hall.IsSolidAt(Gravity::NegY, body.pos),
                hall.IsSolidAt(Gravity::PosY, body.pos),
                prev_body_pos
                );

        max_value = (hall.HalfWidth()-body.halfSize.z);
        min_value = -max_value;
        ClampToSurface(body, {0.0f, 0.0f, 1.0f, 0.0f}, min_value, max_value,
                hall.IsSolidAt(Gravity::NegZ, body.pos),
                hall.IsSolidAt(Gravity::PosZ, body.pos),
                prev_body_pos
                );
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
        // Allow eye to be out the back
        eyeX = std::min(eyeX, float(hall.length) - margin);

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

            // Remove if hit_point not in the hall
            if (!hall.Contains(hit_point) 
                    || !hall.IsSolidAt(min_gravity, hit_point)
               )
            {
                hit = false;
            }
        }


        return {hit, min_gravity, hit_point};
    }

    void ResetLevel()
    {
        levelTime = 0.0f;
        levelStart = false;
        gameState = GameState::Intro;
        hall.LoadCoins();
        ResetPlayer();
    }

    void ResetPlayer()
    {
        roll = 0.0f;
        facing = 1.0f;
        outTime = 0.0f;
        player.velocity = {0.0f, 0.0f, 0.0f, 0.0f};
        player.gravity = Gravity::NegY;
        player.motion = Motion::Grounded;
        player.pull_dir = {0.0f, 0.0f, 0.0f, 0.0f};
        player.pos = {hall.start.slice + 0.5f, 
                      -hall.HalfWidth() + player.halfSize.y,
                      (hall.HalfWidth() - 0.5f) - hall.start.lane };

        player.Land();
        cam.SetYaw(-PI/2);              // turned so we down X axis
        SetCameraTarget(olc::mf4d());

    }


    // Called once at the start, so create things here
    bool OnUserCreate() override
    {
        // Load Sounds
        if (!audio.CreateSoundFromFile(sndCoin,"assets/sfx/coin.wav"))
            std::cerr << "Failed to load coin.wav" << std::endl;
        if (!audio.CreateSoundFromFile(sndFall,"assets/sfx/fall.wav"))
            std::cerr << "Failed to load fall.wav" << std::endl;
        if (!audio.CreateSoundFromFile(sndGate,"assets/sfx/gate.wav"))
            std::cerr << "Failed to load gate.wav" << std::endl;
        if (!audio.CreateSoundFromFile(sndJump,"assets/sfx/jump.wav"))
            std::cerr << "Failed to load jump.wav" << std::endl;
        if (!audio.CreateSoundFromFile(sndLand,"assets/sfx/land.wav"))
            std::cerr << "Failed to load land.wav" << std::endl;
        if (!audio.CreateSoundFromFile(sndLash,"assets/sfx/lash.wav"))
            std::cerr << "Failed to load lash.wav" << std::endl;
        if (!audio.CreateSoundFromFile(sndParBeat,"assets/sfx/parBeat.wav"))
            std::cerr << "Failed to load parBeat.wav" << std::endl;
        if (!audio.CreateSoundFromFile(sndParMissed,"assets/sfx/parMissed.wav"))
            std::cerr << "Failed to load parMissed.wav" << std::endl;
        if (!audio.CreateSoundFromFile(sndTheme,"assets/music/theme.mp3"))
            std::cerr << "Failed to load theme.mp3" << std::endl;
        if (!audio.CreateSoundFromFile(sndThemeIntro,"assets/music/theme_intro.mp3"))
            std::cerr << "Failed to load theme_intro.mp3" << std::endl;

        // Setup Camera
        cam.SetPerspective(75.0f * PI / 180.0f, float(ScreenSize().x) / ScreenSize().y, 0.3f, 100.0f);
        cam.SetDistance(3.0f);          // distance in units
        cam.SetPitch(PI/18);            // Look down at 10 degrees.
        cam.SetYawEaseRate(5.0f);

        // Build the Hall
        if (!LoadLevel(0))
        {
            return false;
        }

        // Build the crystal mesh
        // XXX BuildCrystalMesh(0.35f, 0.2f, olc::Colour::YELLOW);

        // Build the coin mesh
        BuildCoinMesh(coinThickness, coinRadius, coinColor);

        // Build the player
        BuildCubeMesh();
        player.mesh = &cube;

        return true;
    }


    // Called every frame, so update things here
    bool OnUserUpdate(float dt) override
    {
        dt =std::min(dt, 1.0f / 30.0f);
        spinTime = std::fmod(spinTime + dt, spinAngle);

        if (levelStart && gameState == GameState::Playing)
        {
            levelTime += dt;
        }

        /************** Check Controls ****************/

        // Escape quits the game
#ifndef __EMSCRIPTEN__
        if (keyboard.GetKey(olc::Key::ESCAPE).bPressed) return false;
#endif

        // Toggle debug HUD
        if (keyboard.GetKey(olc::Key::OEM_3).bPressed)
            showDebug = !showDebug;

        // Press R to restart
        if (keyboard.GetKey(olc::Key::R).bPressed)
        {
            LoadLevel(levelIndex);
        }

        // Press M to toggle music
        if (keyboard.GetKey(olc::Key::M).bPressed)
        {
            if (musicState != MusicState::Disabled)
            {
                sndThemeIntro.Pause();
                sndTheme.Pause();
                musicState = MusicState::Disabled;
            }
            else
            {
                musicState = MusicState::Paused;
                if (gameState == GameState::Playing)
                {
                    StartIntroMusic();
                }
            }
        }

        switch (gameState)
        {
            case GameState::Intro:
            {
                if (keyboard.GetKey(olc::Key::SPACE).bPressed)
                {
                    gameState = GameState::Playing;
                    if (musicState == MusicState::Paused)
                    {
                        StartIntroMusic();
                    }
                }
                if (keyboard.GetKey(olc::Key::N).bPressed)
                {
                    // Allow skipping to next level
                    LoadLevel((levelIndex + 1) % static_cast<int>(std::size(levelFiles)));
                }
                break;
            }
            case GameState::Complete:
            {
                if (keyboard.GetKey(olc::Key::SPACE).bPressed)
                {
                    if (LastLevel())
                    {
                        gameState = GameState::End;
                    }
                    else
                    {
                        LoadLevel(levelIndex + 1);
                    }
                }
                break;
            }
            case GameState::End:
            {
                if (keyboard.GetKey(olc::Key::SPACE).bPressed)
                {
                    LoadLevel(0);
                }
                break;
            }
            case GameState::Playing:
            {
                if (keyboard.GetKey(olc::Key::Q).bPressed)
                {
                    cam.TurnYaw(PI);
                    facing = -facing;
                }

                if (keyboard.GetKey(olc::Key::K1).bPressed) player.Fall(Gravity::NegY);
                if (keyboard.GetKey(olc::Key::K2).bPressed) player.Fall(Gravity::PosZ);
                if (keyboard.GetKey(olc::Key::K3).bPressed) player.Fall(Gravity::PosY);
                if (keyboard.GetKey(olc::Key::K4).bPressed) player.Fall(Gravity::NegZ);

                // Check Spacebar for jump
                if (player.motion == Motion::Grounded 
                        || (player.motion == Motion::Falling &&
                            player.coyoteTimer > 0.0f))
                {
                    if (keyboard.GetKey(olc::Key::SPACE).bPressed)
                    {
                        if (showDebug && player.motion != Motion::Grounded)
                        {
                            std::cout << "CoyoteTime! " << player.coyoteTimer << std::endl;
                        }
                        player.Jump();
                        sndJump.Play();
                    }
                }

                // Check WASD when not Lashing
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
                break;
            } 
        }

        /************** Game State Update ****************/

        // Start the level timer?
        if (!levelStart)
        {
            if (player.velocity.x != 0.0f 
                || player.velocity.y != 0.0f
                || player.velocity.z != 0.0f)
            {
                levelStart = true;
            }
        }

        // Update player
        if (gameState == GameState::Playing)
        {
            Motion oldMotion = player.motion;
            BodyUpdate(player, dt);
            if (player.motion != oldMotion 
                    && player.motion == Motion::Grounded)
            {
                sndLand.Play();
            }
            CollectCoins();
            if (player.motion == Motion::Falling)
            {
                player.coyoteTimer -= dt;
            }
        }

        // Check the Music
        if (musicState == MusicState::Intro)
        {
            if (!sndThemeIntro.IsPlaying())
            {
                StartLoopMusic();
            }
        }

        // Check the Gate
        if (hall.GateOpen() && !gateWasOpen)
        {
            sndGate.Play();
        }
        gateWasOpen = hall.GateOpen();

        if (gameState == GameState::Playing && 
                hall.GateOpen() && player.pos.x >=
                hall.length - player.halfSize.x - tolerance) 
        {
            bool beatPar = levelTime <= hall.parTime;
            completionComment = CompletionComment(beatPar);
            sndThemeIntro.Pause();
            sndTheme.Pause();
            if (musicState != MusicState::Disabled)
            {
                musicState = MusicState::Paused;
            }
            gameState = GameState::Complete;
            if (beatPar)
            {
                sndParBeat.Play();
            }
            else
            {
                sndParMissed.Play();
            }
        }

        // Out of bounds check
        if (!hall.Contains(player.pos)) 
        {
            if (outTime == 0.0f)
            {
                // We just started falling, play sound
                sndFall.Play();
            }
            outTime += dt;
            if (outTime > fallTime)
            {
                ResetPlayer();
            } 
        } else
        {
            outTime = 0.0f;
        }

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
        if (gameState == GameState::Playing && mouse.GetButton(0).bPressed && newWall && lash.hit)
        {
            // Check that the player is in the hall
            if (hall.Contains(player.pos))
            {
                player.Lash(lash.wall, lash.hit_point);
                sndLash.Play();
            }
        }

        /************** Drawing ****************/

        // Clear screen to dark blue
        draw.Clear(olc::Colour::VERY_DARK_BLUE);

        draw.SetCullMode(olc::CullMode::ClockWise);
        draw.EnableDepth(true);

        // Draw Hallway
        draw.SetModelMatrix(rot);
        draw.Mesh(olc::Structure::List, hall.mesh.pos, hall.mesh.col);
        olc::Pixel gateColor = hall.GateOpen() ? olc::Colour::DARK_GREEN :
            olc::Colour::RED ;
        draw.Mesh(olc::Structure::List, hall.gateMesh.pos, 
                hall.gateMesh.col, gateColor);

        // Draw Player Cube
        olc::mf4d tr, sc;
        tr.translate(player.pos);
        olc::vf4d s = player.halfSize * 2.0f * drawShrink;
        sc.scale(s.x, s.y, s.z);
        // Matrices apply right to left
        // So scale, then translate, then rotate
        draw.SetModelMatrix(rot * tr * sc);
        draw.Mesh(olc::Structure::List, player.mesh->pos, player.mesh->col, player.tint);

        // Draw Coin
        DrawCoins(rot);

        // Draw Gravity Arrow
        if (showDebug)
        {
            draw.SetModelMatrix(rot);
            draw.EnableDepth(false);
            draw.Line(player.pos, player.pos + (GravityDirection(player.gravity)*1.25f),
                    olc::Colour::TANGERINE, olc::Colour::TANGERINE);
            draw.EnableDepth(true);
        }

        // Draw HUD
        draw.WorldReset();

        std::ostringstream coinStr;
        coinStr << "Coins: " << hall.CoinsCollected() << "/"
            << hall.coins.size() << "\n"
            "Level: " << levelIndex + 1;
        draw.String({2,2}, coinStr.str(), olc::Colour::YELLOW);

        std::ostringstream timeStr;
        timeStr << std::fixed << std::setprecision(2) 
            << "Time: " << levelTime << "\n"
            << "Par : " << hall.parTime ;
        olc::vf2d size = draw.GetTextSize(timeStr.str());
        float x = ScreenSize().x - size.x - 2;
        draw.String({x,2}, timeStr.str(), olc::Colour::YELLOW);

        if (gameState == GameState::Intro)
        {
            DrawPanel(hall.levelText);
        }


        if (gameState == GameState::Complete)
        {
            std::ostringstream completeStr;

            std::ostringstream parStr;
            parStr << std::fixed << std::setprecision(2)
                << "Par Time : " << hall.parTime;

            std::ostringstream timeStr;
            timeStr << std::fixed << std::setprecision(2)
                << "Your Time: " << levelTime;

            DrawPanel({
                "Level Complete!",
                "",
                parStr.str(),
                timeStr.str(),
                "",
                completionComment
            });
        }
        if (gameState == GameState::End)
        {
            DrawPanel({
                "Congratulations!",
                "",
                "You completed the game!",
            });
        }

        if (showDebug)
        {
            draw.String({ 2, 100 }, std::string("Gravity: ") + GravityName(player.gravity)
                    + "\nPosition: " + ToString(player.pos), olc::Colour::YELLOW);
            if (lash.hit)
            {
                draw.String({2, 120}, "Hit Point: " + ToString(lash.hit_point) + "\n" +
                        "Hit Floor: " + GravityName(lash.wall) + "\n"
                        ,olc::Colour::YELLOW);
            }
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

