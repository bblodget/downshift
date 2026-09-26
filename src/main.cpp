/*
	Jam template for olcPixelGameEngine3.

	What you get:
	  - PGEConfig construction (vsync on, fixed logical resolution)
	  - Escape quits, focus check pauses input
	  - F1 toggles a debug overlay (FPS, frame time, GPU task metrics)
	  - A minimal game-mode switcher (Title -> Play -> Pause)
	  - Assets loaded relative to the executable, proven by a test image

	Replace the modes with your game. Keep main() and the overlay.
*/

#define OLC_PGE3_APPLICATION
#include "olcPixelGameEngine3.h"

#include <array>
#include <memory>
#include <string>

// ---------------------------------------------------------------------------
// Game modes
// ---------------------------------------------------------------------------
enum class ModeID { Title, Play, Pause, Quit };

class Game;   // forward, modes get a reference to the engine

struct Mode
{
	virtual ~Mode() = default;
	virtual void OnEnter(Game&) {}
	virtual void OnExit(Game&) {}
	// Return the mode to switch to (or the same one to stay)
	virtual ModeID OnUpdate(Game&, float dt) = 0;
};

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------
class Game : public olc::PixelGameEngine
{
public:
	Game() { sAppName = "Jam Game"; }

	// Shared state modes can touch. Add your world here.
	olc::Image imgTest;
	bool bShowDebug = false;

	bool OnUserCreate() override
	{
		if (!CreateImageFromFile(imgTest, "./assets/test.png"))
			return false;   // wrong working directory: run from the build dir

		modes[size_t(ModeID::Title)] = std::make_unique<TitleMode>();
		modes[size_t(ModeID::Play)]  = std::make_unique<PlayMode>();
		modes[size_t(ModeID::Pause)] = std::make_unique<PauseMode>();
		SwitchMode(ModeID::Title);
		return true;
	}

	bool OnUserUpdate(float dt) override
	{
		// Global keys
		if (keyboard.GetKey(olc::Key::ESCAPE).bPressed) return false;
		if (keyboard.GetKey(olc::Key::F1).bPressed) bShowDebug = !bShowDebug;

		// Don't let held keys stick while the window is unfocused
		if (!IsFocused())
		{
			draw.Clear(olc::Colour::VERY_DARK_GREY);
			draw.String((ScreenSize() - draw.GetTextSize("Paused (unfocused)")) / 2, "Paused (unfocused)");
			return true;
		}

		auto metrics = draw.GetDrawMetrics();
		draw.ResetDrawMetrics();

		// Run the current mode
		ModeID next = CurrentMode().OnUpdate(*this, dt);
		if (next == ModeID::Quit) return false;
		if (next != current) SwitchMode(next);

		// Debug overlay, always in screen space
		if (bShowDebug)
		{
			draw.WorldReset();
			draw.ResetShader();
			std::string s =
				"FPS " + std::to_string(GetFPS()) +
				"  dt " + std::to_string(int(dt * 1000000.0f)) + "us\n" +
				"GPU tasks " + std::to_string(metrics.nGPUTasks) +
				"  up " + std::to_string(metrics.nCPUtoGPUTransfers) +
				"  down " + std::to_string(metrics.nGPUtoCPUTransfers);
			olc::vf2d sz = draw.GetTextSize(s);
			draw.FilledRect({ 0, 0 }, sz + olc::vf2d{ 4, 4 }, olc::PixelF(0, 0, 0, 0.6f));
			draw.String({ 2, 2 }, s, olc::Colour::YELLOW);
		}
		return true;
	}

private:
	std::array<std::unique_ptr<Mode>, 4> modes;   // indexed by ModeID
	ModeID current = ModeID::Title;
	Mode& CurrentMode() { return *modes[size_t(current)]; }

	void SwitchMode(ModeID id)
	{
		if (modes[size_t(current)]) CurrentMode().OnExit(*this);
		current = id;
		CurrentMode().OnEnter(*this);
	}

	// ---- The modes. Replace these with your game. -------------------------
	struct TitleMode : Mode
	{
		ModeID OnUpdate(Game& g, float) override
		{
			g.draw.Clear(olc::Colour::VERY_DARK_BLUE);
			g.draw.ImageRect(g.imgTest, { 96, 40 }, { 64, 64 });
			g.draw.StringProp({ 8, 120 }, "JAM GAME", olc::Colour::WHITE, { 3, 3 });
			g.draw.StringProp({ 8, 160 }, "SPACE to play   ESC to quit   F1 debug", olc::Colour::GREY);
			return g.keyboard.GetKey(olc::Key::SPACE).bPressed ? ModeID::Play : ModeID::Title;
		}
	};

	struct PlayMode : Mode
	{
		olc::vf2d pos;
		void OnEnter(Game& g) override { pos = g.ScreenSize() / 2; }
		ModeID OnUpdate(Game& g, float dt) override
		{
			if (g.keyboard.GetKey(olc::Key::P).bPressed) return ModeID::Pause;
			olc::vf2d v;
			if (g.keyboard.GetKey(olc::Key::LEFT).bHeld)  v.x -= 1;
			if (g.keyboard.GetKey(olc::Key::RIGHT).bHeld) v.x += 1;
			if (g.keyboard.GetKey(olc::Key::UP).bHeld)    v.y -= 1;
			if (g.keyboard.GetKey(olc::Key::DOWN).bHeld)  v.y += 1;
			pos += v * 80.0f * dt;

			g.draw.Clear(olc::Colour::BLACK);
			g.draw.FilledCircle(pos.round(), 8, olc::Colour::CYAN);
			g.draw.StringProp({ 8, 8 }, "Arrows move, P pauses", olc::Colour::GREY);
			return ModeID::Play;
		}
	};

	struct PauseMode : Mode
	{
		ModeID OnUpdate(Game& g, float) override
		{
			// Note: previous frame is not preserved; draw what you need
			g.draw.Clear(olc::Colour::VERY_DARK_GREY);
			g.draw.String((g.ScreenSize() - g.draw.GetTextSize("PAUSED")) / 2, "PAUSED");
			return g.keyboard.GetKey(olc::Key::P).bPressed ? ModeID::Play : ModeID::Pause;
		}
	};
};

// ---------------------------------------------------------------------------
int main()
{
	olc::PGEConfig cfg;
	cfg.vScreenSize = { 256, 240 };   // logical pixels
	cfg.vPixelSize  = { 4, 4 };       // window = 1024x960
	cfg.bVSync      = true;
	cfg.bResizeable = true;

	Game game;
	if (game.Construct(cfg))
		game.Start();
	return 0;
}
