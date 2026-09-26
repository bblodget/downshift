/*
    Camera3D

	A small 3D camera for olcPixelGameEngine3. Builds the view and projection
	matrices so you don't have to, and answers "which way is forward" questions.

	Two modes:
	  Orbit   - looks at a target point from (yaw, pitch, distance). Optional
	            snapping of yaw to fixed steps with easing, for grid games.
	  Free    - position + yaw + pitch, fly around with WASD and mouse-look.

	Usage:
	  olc::utils::Camera3D cam;
	  cam.SetPerspective(60.0f * PI / 180.0f, aspect, 0.1f, 100.0f);
	  cam.SetTarget({0,0,0}); cam.SetDistance(9.0f); cam.SetPitch(0.6f);
	  ...each frame...
	  cam.Update(fElapsedTime);
	  cam.Apply(draw);                  // sets view + projection on the draw context
	  draw.SetModelMatrix(...); draw.Mesh(...);

	Conventions:
	  +Y is up. mf4d helpers reset to identity, so everything is composed
	  with '*', applied right-to-left. The perspective() helper is the OpenGL
	  form (looks down -Z), and PGE3's world looks down +Z, so this class
	  includes the rotateX(PI) flip the example does by hand.

	AI Disclosure:
      This file was created with the help of Claude Code.

	Licence (OLC-3), see olcPixelGameEngine3.h
*/

#pragma once
#include <olcPixelGameEngine3.h>
#include <cmath>
#include <numbers>

namespace olc::utils
{
	class Camera3D
	{
	public:
		enum class Mode { Orbit, Free };

	public:
		Camera3D() = default;

		// ---- Projection -----------------------------------------------------
		void SetPerspective(float fFovRadians, float fAspect, float fNear, float fFar)
		{
			m_matProj.perspective(fFovRadians, fAspect, fNear, fFar);
		}

		// ---- Mode -----------------------------------------------------------
		void SetMode(Mode m) { m_mode = m; }
		Mode GetMode() const { return m_mode; }

		// ---- Orbit parameters ----------------------------------------------
		void SetTarget(const olc::vf4d& v) { m_vTarget = v; }
		const olc::vf4d& GetTarget() const { return m_vTarget; }
		void SetDistance(float d) { m_fDistance = d; }
		float GetDistance() const { return m_fDistance; }

		// ---- Shared orientation --------------------------------------------
		// Yaw: rotation about +Y, in radians, always kept in (-PI, PI].
		//     yaw      facing
		//     0        +Z
		//    -PI/2     +X
		//     PI/2     -X
		//    +-PI      -Z
		// Pitch: positive looks down from above.
		//
		// SetYaw snaps immediately. SetYawTarget / TurnYaw ease toward the new
		// yaw in Update(), at SetYawEaseRate's rate.
		void SetYaw(float r) { m_fYaw = WrapAngle(r); m_fYawTarget = m_fYaw; }
		float GetYaw() const { return m_fYaw; }
		// Ease to an absolute yaw, the shortest way round
		void SetYawTarget(float r) { m_fYawTarget = m_fYaw + WrapAngle(r - m_fYaw); }
		// Ease by a relative amount, in the direction given (TurnYaw(PI) = turn around)
		void TurnYaw(float fDelta) { m_fYawTarget += fDelta; }
		float GetYawTarget() const { return WrapAngle(m_fYawTarget); }
		// How fast eased turns happen, in 1/seconds. Default 10.
		// Larger turns faster, smaller turns slower. A turn looks finished
		// after about 3 / fRate seconds: 10 -> ~0.3s, 5 -> ~0.6s, 20 -> ~0.15s.
		void SetYawEaseRate(float fRate) { m_fEaseRate = fRate; }
		bool IsTurning() const { return std::abs(m_fYawTarget - m_fYaw) > 0.001f; }
		void SetPitch(float r) { m_fPitch = r; }
		float GetPitch() const { return m_fPitch; }
		void SetPitchLimits(float lo, float hi) { m_fPitchMin = lo; m_fPitchMax = hi; }

		// Free-mode position (ignored in Orbit; Orbit derives it)
		void SetPosition(const olc::vf4d& v) { m_vPos = v; }
		const olc::vf4d& GetPosition() const { return m_vPos; }

		// ---- Yaw steps ------------------------------------------------------
		// Convenience for grid games: turn in whole steps of fStepRadians.
		// StepYaw is relative to wherever the yaw currently is heading.
		void SetYawSnap(float fStepRadians, float fEaseRate = 10.0f)
		{
			m_fYawStep = fStepRadians; m_fEaseRate = fEaseRate;
		}
		void StepYaw(int nSteps) { TurnYaw(float(nSteps) * m_fYawStep); }

		// ---- Per-frame -------------------------------------------------------
		void Update(float fElapsedTime)
		{
			m_fPitch = std::clamp(m_fPitch, m_fPitchMin, m_fPitchMax);

			// Ease yaw toward its target
			if (IsTurning())
			{
				float t = std::min(1.0f, m_fEaseRate * fElapsedTime);
				m_fYaw += (m_fYawTarget - m_fYaw) * t;
				if (!IsTurning()) m_fYaw = m_fYawTarget;
			}

			// Keep yaw in (-PI, PI], shifting the target by the same amount so an
			// in-progress turn continues the same way round
			float fWrapped = WrapAngle(m_fYaw);
			m_fYawTarget += fWrapped - m_fYaw;
			m_fYaw = fWrapped;

			RebuildView();
		}

		// Sets view and projection on a Draw context
		void Apply(olc::Draw& draw) const
		{
			draw.SetProjectionMatrix(m_matProj);
			draw.SetViewMatrix(m_matView);
		}

		// Convenience for Free mode: WASD/Space/Ctrl move, arrows or mouse delta look
		void HandleFreeFly(const olc::hw::Keyboard& kb, float fElapsedTime,
			float fMoveSpeed = 5.0f, float fTurnSpeed = 2.0f)
		{
			olc::vf4d f = Forward(), r = Right();
			float s = fMoveSpeed * fElapsedTime;
			if (kb.GetKey(olc::Key::W).bHeld) m_vPos = m_vPos + f * s;
			if (kb.GetKey(olc::Key::S).bHeld) m_vPos = m_vPos - f * s;
			if (kb.GetKey(olc::Key::D).bHeld) m_vPos = m_vPos + r * s;
			if (kb.GetKey(olc::Key::A).bHeld) m_vPos = m_vPos - r * s;
			if (kb.GetKey(olc::Key::SPACE).bHeld) m_vPos.y += s;
			if (kb.GetKey(olc::Key::CTRL).bHeld)  m_vPos.y -= s;
			float t = fTurnSpeed * fElapsedTime;
			if (kb.GetKey(olc::Key::LEFT).bHeld)  { m_fYaw -= t; m_fYawTarget -= t; }
			if (kb.GetKey(olc::Key::RIGHT).bHeld) { m_fYaw += t; m_fYawTarget += t; }
			if (kb.GetKey(olc::Key::UP).bHeld)    m_fPitch -= t;
			if (kb.GetKey(olc::Key::DOWN).bHeld)  m_fPitch += t;
		}
		void MouseLook(const olc::vf2d& vDelta, float fSensitivity = 0.005f)
		{
			m_fYaw += vDelta.x * fSensitivity; m_fYawTarget += vDelta.x * fSensitivity;
			m_fPitch += vDelta.y * fSensitivity;
		}

		// ---- Directions in world space (unit, +Y up) -----------------------
		// Horizontal forward: ignores pitch, so it's the "screen up" grid direction
		// Note the -sin: the view matrix rotates the WORLD by +yaw, which is the
		// camera turning by -yaw. Verified numerically against the projection.
		olc::vf4d ForwardFlat() const { return { -std::sin(m_fYaw), 0.0f, std::cos(m_fYaw), 0.0f }; }
		olc::vf4d Right() const       { return {  std::cos(m_fYaw), 0.0f, std::sin(m_fYaw), 0.0f }; }
		olc::vf4d Forward() const
		{
			float cp = std::cos(m_fPitch);
			return { -std::sin(m_fYaw) * cp, -std::sin(m_fPitch), std::cos(m_fYaw) * cp, 0.0f };
		}
		olc::vf4d Up() const { return { 0.0f, 1.0f, 0.0f, 0.0f }; }

		// For grid games: which world axis does "screen up/right" map to right now?
		// Returns a unit axis vector (+-X or +-Z) chosen from the current yaw.
		olc::vf4d ScreenUpAxis() const    { return SnapToAxis(ForwardFlat()); }
		olc::vf4d ScreenRightAxis() const { return SnapToAxis(Right()); }

		// Camera position in world space (derived in Orbit mode)
		olc::vf4d Eye() const
		{
			if (m_mode == Mode::Free) return m_vPos;
			return m_vTarget - Forward() * m_fDistance;
		}

		// ---- Picking ---------------------------------------------------------
		// Ray through a screen pixel: origin at the eye, unit direction.
		// vScreen in logical pixels, vScreenSize the draw target size.
		struct Ray { olc::vf4d origin, dir; };
		Ray ScreenToRay(const olc::vf2d& vScreen, const olc::vi2d& vScreenSize) const
		{
			// NDC for a PGE3 render target: x right, and +y is DOWN (row 0 is the
			// top of the target; this is why the view carries a rotateX(PI) flip).
			float nx = (vScreen.x / float(vScreenSize.x)) * 2.0f - 1.0f;
			float ny = (vScreen.y / float(vScreenSize.y)) * 2.0f - 1.0f;
			olc::mf4d inv = (m_matProj * m_matView).invert();
			olc::vf4d pNear = inv * olc::vf4d{ nx, ny, -1.0f, 1.0f };
			olc::vf4d pFar  = inv * olc::vf4d{ nx, ny,  1.0f, 1.0f };
			pNear = pNear / pNear.w; pFar = pFar / pFar.w;
			olc::vf4d d = pFar - pNear; d.w = 0.0f;
			return { pNear, d.norm() };
		}

		const olc::mf4d& GetViewMatrix() const { return m_matView; }
		const olc::mf4d& GetProjectionMatrix() const { return m_matProj; }

	public:
		// Wrap an angle into (-PI, PI]
		static float WrapAngle(float r)
		{
			constexpr float PI = std::numbers::pi_v<float>;
			constexpr float TWO_PI = 2.0f * PI;
			r = std::fmod(r + PI, TWO_PI);
			if (r <= 0.0f) r += TWO_PI;
			return r - PI;
		}

	private:
		static olc::vf4d SnapToAxis(const olc::vf4d& v)
		{
			if (std::abs(v.x) >= std::abs(v.z))
				return { v.x < 0 ? -1.0f : 1.0f, 0.0f, 0.0f, 0.0f };
			return { 0.0f, 0.0f, v.z < 0 ? -1.0f : 1.0f, 0.0f };
		}

		void RebuildView()
		{
			constexpr float PI = std::numbers::pi_v<float>;
			olc::mf4d mFlip, mYaw, mPitch, mT, mBack;
			mFlip.rotateX(PI);              // OpenGL -Z projection -> PGE3 +Z world
			mYaw.rotateY(m_fYaw);
			mPitch.rotateX(-m_fPitch);
			if (m_mode == Mode::Orbit)
			{
				mT.translate(-m_vTarget.x, -m_vTarget.y, -m_vTarget.z);
				mBack.translate(0.0f, 0.0f, m_fDistance);
				m_matView = mFlip * mBack * mPitch * mYaw * mT;
				m_vPos = Eye();
			}
			else
			{
				mT.translate(-m_vPos.x, -m_vPos.y, -m_vPos.z);
				m_matView = mFlip * mPitch * mYaw * mT;
			}
		}

	private:
		Mode      m_mode = Mode::Orbit;
		olc::mf4d m_matProj, m_matView;
		olc::vf4d m_vTarget{ 0, 0, 0, 1 };
		olc::vf4d m_vPos{ 0, 0, 0, 1 };
		float m_fDistance = 10.0f;
		float m_fYaw = 0.0f, m_fYawTarget = 0.0f;
		float m_fPitch = 0.5f, m_fPitchMin = -1.5f, m_fPitchMax = 1.5f;
		float m_fYawStep = PI_OVER_2, m_fEaseRate = 10.0f;
		static constexpr float PI_OVER_2 = std::numbers::pi_v<float> / 2.0f;
	};
}
