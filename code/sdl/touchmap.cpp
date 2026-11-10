/*******************************************************************************
 *                                O P E N T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "sdl/touchmap.h"

#include "dbgprint.h"

#include <cmath>
#include <cstring>

namespace
{

/// A contact held this long without leaving its origin presses the right button on release.
constexpr Uint64 LONG_PRESS_MS = 400;

/// A contact that stayed within this many pixels of its origin is still a candidate for the
/// long press. The game starts its rubber band at the system drag distance, which the platform
/// layer reports as four pixels, so a contact that moved further has already begun a band and
/// must never turn into a right click under it.
constexpr float LONG_PRESS_SLOP_PX = 4.0f;

/// Movement past this many logical pixels from the origin turns a contact into a drag.
constexpr float DRAG_THRESHOLD_LOGICAL_PX = 15.0f;

/// Two contacts cannot share a point, so a contact landing here repeats a tracked one.
constexpr float DUPLICATE_CONTACT_PX = 2.0f;

/// Contacts tracked at once: one drives the cursor, a pair pans the view.
constexpr int MAX_FINGERS = 2;

/// A lone contact presses the left button only after this long. Two fingers land within a few
/// tens of milliseconds of each other, so a pan cancels the press before it becomes a click.
/// A press and release is a click to the game, and a click on the battlefield with units
/// selected is a move order, which a pan must not issue.
constexpr Uint64 PRESS_DELAY_MS = 90;

/// A synthesized click stays pressed this long. The game samples the buttons once a frame, so
/// a press and release in the same instant is invisible to it.
constexpr Uint64 CLICK_HOLD_MS = 200;

struct Finger
{
	SDL_FingerID Id = 0;
	float X = 0.0f;
	float Y = 0.0f;
	bool Used = false;
};

enum class Gesture
{
	/// One contact down, no button yet; the cursor follows it.
	Pending,
	/// A held left button: the select box, or a slider and list drag.
	Drag,
	/// Two contacts panning the view.
	Pan,
	/// A gesture finished; the rest of the contact's events are consumed.
	Done,
};

struct Mapper
{
	Finger Fingers[MAX_FINGERS];
	Gesture Current = Gesture::Pending;
	float OriginX = 0.0f;
	float OriginY = 0.0f;
	Uint64 Started = 0;
	bool SuppressTap = false;

	float PanLastX = 0.0f;
	float PanLastY = 0.0f;
	int PanX = 0;
	int PanY = 0;

	// The device has no mouse, so the touch layer is the pointer: the platform layer answers
	// the game's cursor and button queries from here.
	float PointerX = 0.0f;
	float PointerY = 0.0f;
	bool HasPointer = false;
	bool LeftDown = false;
	bool RightDown = false;
	Uint64 LeftPressedAt = 0;

	// A press waiting for the next pump pass, so the interface sees the pointer before the
	// button, and the release that follows once it has been held long enough.
	bool PressPending = false;
	Uint64 PressAt = 0;
	Uint8 PressButton = 0;
	float PressX = 0.0f;
	float PressY = 0.0f;

	bool ReleasePending = false;
	Uint8 ReleaseButton = 0;
	float ReleaseX = 0.0f;
	float ReleaseY = 0.0f;
	Uint64 ReleaseAt = 0;
	bool PanPending = false;
	bool Panning = false;
};

Mapper MapperState;

int Tracked_Count(void)
{
	int count = 0;
	for (Finger const & finger : MapperState.Fingers) {
		if (finger.Used) {
			count++;
		}
	}
	return(count);
}

bool Tracked(SDL_FingerID id)
{
	for (Finger const & finger : MapperState.Fingers) {
		if (finger.Used && finger.Id == id) {
			return(true);
		}
	}
	return(false);
}

void Tracked_Move(SDL_FingerID id, float x, float y)
{
	for (Finger & finger : MapperState.Fingers) {
		if (finger.Used && finger.Id == id) {
			finger.X = x;
			finger.Y = y;
		}
	}
}

void Tracked_Add(SDL_FingerID id, float x, float y)
{
	for (Finger & finger : MapperState.Fingers) {
		if (!finger.Used) {
			finger.Used = true;
			finger.Id = id;
			finger.X = x;
			finger.Y = y;
			return;
		}
	}
}

void Tracked_Remove(SDL_FingerID id)
{
	Finger kept[MAX_FINGERS];
	int count = 0;
	for (Finger const & finger : MapperState.Fingers) {
		if (finger.Used && finger.Id != id) {
			kept[count++] = finger;
		}
	}

	MapperState.Fingers[0] = {};
	MapperState.Fingers[1] = {};
	for (int index = 0; index < count; index++) {
		MapperState.Fingers[index] = kept[index];
	}
}

void Centroid(float & x, float & y)
{
	float sumx = 0.0f;
	float sumy = 0.0f;
	int count = 0;
	for (Finger const & finger : MapperState.Fingers) {
		if (finger.Used) {
			sumx += finger.X;
			sumy += finger.Y;
			count++;
		}
	}
	float const divisor = (float)(count > 0 ? count : 1);
	x = sumx / divisor;
	y = sumy / divisor;
}

float Distance(float ax, float ay, float bx, float by)
{
	return(std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by)));
}

float Drag_Threshold(void)
{
	// SDL reports a contact against the window, so a threshold measured in window pixels
	// already scales with the display.
	return(DRAG_THRESHOLD_LOGICAL_PX);
}

void Emit_Motion(void (*sink)(SDL_Event const &), float x, float y)
{
	MapperState.PointerX = x;
	MapperState.PointerY = y;
	MapperState.HasPointer = true;

	SDL_Event event;
	SDL_zero(event);
	event.type = SDL_EVENT_MOUSE_MOTION;
	event.motion.x = x;
	event.motion.y = y;
	event.motion.xrel = 0.0f;
	event.motion.yrel = 0.0f;
	sink(event);
}

void Schedule_Release(float x, float y, Uint8 button, Uint64 at)
{
	MapperState.ReleasePending = true;
	MapperState.ReleaseButton = button;
	MapperState.ReleaseX = x;
	MapperState.ReleaseY = y;
	MapperState.ReleaseAt = at;
}


void Emit_Button(void (*sink)(SDL_Event const &), float x, float y, Uint8 button, bool down)
{
	MapperState.PointerX = x;
	MapperState.PointerY = y;
	MapperState.HasPointer = true;

	if (button == SDL_BUTTON_LEFT) {
		MapperState.LeftDown = down;
	} else if (button == SDL_BUTTON_RIGHT) {
		MapperState.RightDown = down;
	}

	SDL_Event event;
	SDL_zero(event);
	event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
	event.button.x = x;
	event.button.y = y;
	event.button.button = button;
	event.button.down = down;
	event.button.clicks = 1;
	sink(event);
}

/// Arms a click where the contact lifted. The pump presses it on its next pass, once the
/// interface has seen the pointer there, and releases it after the hold.
void Emit_Click(void (*sink)(SDL_Event const &), float x, float y, Uint8 button, Uint64 timestamp)
{
	(void)sink;
	(void)timestamp;

	// Anything still held is released first, so two clicks cannot share one hold.
	MapperState.ReleasePending = false;

	MapperState.PressPending = true;
	MapperState.PressButton = button;
	MapperState.PressX = x;
	MapperState.PressY = y;
	MapperState.ReleaseButton = button;
	MapperState.ReleaseX = x;
	MapperState.ReleaseY = y;
}

void On_Finger_Down(SDL_FingerID id, float x, float y, Uint64 timestamp, int width, int height,
	void (*sink)(SDL_Event const &))
{
	(void)width;
	(void)height;

	int const tracked = Tracked_Count();

	// A copy of a tracked contact rather than a new one. Two contacts cannot share a point.
	for (Finger const & finger : MapperState.Fingers) {
		if (finger.Used && Distance(finger.X, finger.Y, x, y) <= DUPLICATE_CONTACT_PX) {
			return;
		}
	}

	// A third contact is ignored, and so is a second one outside a match.
	if (tracked >= MAX_FINGERS) {
		return;
	}

	Tracked_Add(id, x, y);

	if (tracked == 0) {
		MapperState.Current = Gesture::Pending;
		MapperState.OriginX = x;
		MapperState.OriginY = y;
		MapperState.Started = timestamp;
		MapperState.SuppressTap = false;

		// The cursor follows the contact at once, but the button waits: a second contact turns
		// this gesture into a pan, and a press that was already sent would be released as a
		// click, which the game reads as a move order.
		Emit_Motion(sink, x, y);
		MapperState.PressPending = true;
		MapperState.PressAt = timestamp + PRESS_DELAY_MS;
		MapperState.PressButton = SDL_BUTTON_LEFT;
		MapperState.PressX = x;
		MapperState.PressY = y;
		return;
	}

	// The second contact takes over as a view pan. A press still waiting is simply dropped, so
	// no click reaches the game; one already sent has to be released.
	MapperState.PressPending = false;
	if (MapperState.LeftDown) {
		Emit_Button(sink, x, y, SDL_BUTTON_LEFT, false);
	}

	float cx = 0.0f;
	float cy = 0.0f;
	Centroid(cx, cy);
	MapperState.Current = Gesture::Pan;
	MapperState.PanLastX = cx;
	MapperState.PanLastY = cy;
	MapperState.Panning = true;
	Emit_Motion(sink, cx, cy);
}

void On_Finger_Motion(SDL_FingerID id, float x, float y, Uint64 timestamp,
	void (*sink)(SDL_Event const &))
{
	if (!Tracked(id)) {
		return;
	}

	Tracked_Move(id, x, y);

	switch (MapperState.Current) {
		case Gesture::Done:
			return;

		case Gesture::Pending: {
			float const threshold = Drag_Threshold();

			// The contact stays pending while it is down, whatever its age: the long press is
			// decided when it lifts, so a hold that turns into a drag keeps the button down and
			// the game keeps extending the select box until the finger leaves.
			if (Distance(x, y, MapperState.OriginX, MapperState.OriginY) > threshold) {
				// Past the threshold the contact is a select box or a slider drag. The button is
				// already down from where it landed, so the box starts at the origin.
				MapperState.Current = Gesture::Drag;
				MapperState.SuppressTap = false;
				Emit_Motion(sink, x, y);
				return;
			}

			Emit_Motion(sink, x, y);
			return;
		}

		case Gesture::Drag:
			Emit_Motion(sink, x, y);
			return;

		case Gesture::Pan: {
			float cx = 0.0f;
			float cy = 0.0f;
			Centroid(cx, cy);
			MapperState.PanX += (int)std::lround(MapperState.PanLastX - cx);
			MapperState.PanY += (int)std::lround(MapperState.PanLastY - cy);
			MapperState.PanPending = true;
			MapperState.PanLastX = cx;
			MapperState.PanLastY = cy;
			Emit_Motion(sink, cx, cy);
			return;
		}
	}
}

void Reanchor_As_Pending(Uint64 timestamp, SDL_FingerID lifting)
{
	for (Finger const & finger : MapperState.Fingers) {
		if (finger.Used && finger.Id != lifting) {
			MapperState.Current = Gesture::Pending;
			MapperState.OriginX = finger.X;
			MapperState.OriginY = finger.Y;
			MapperState.Started = timestamp;
			// The surviving contact rode a multi-finger gesture, so lifting it must not click.
			MapperState.SuppressTap = true;
			return;
		}
	}
	MapperState.Current = Gesture::Done;
	MapperState.Started = timestamp;
}

void On_Finger_Up(SDL_FingerID id, float x, float y, Uint64 timestamp, bool canceled,
	void (*sink)(SDL_Event const &))
{
	if (!Tracked(id)) {
		return;
	}

	int const remaining = Tracked_Count() - 1;

	switch (MapperState.Current) {
		case Gesture::Done:
			return;

		case Gesture::Pending: {
			Uint64 const elapsed = timestamp >= MapperState.Started ? timestamp - MapperState.Started : 0;
			bool const suppressed = MapperState.SuppressTap;
			MapperState.SuppressTap = false;
			MapperState.Current = Gesture::Done;

			bool const held = elapsed >= LONG_PRESS_MS;
			bool const still = Distance(x, y, MapperState.OriginX, MapperState.OriginY) <= LONG_PRESS_SLOP_PX;

			Emit_Motion(sink, x, y);

			// A tap that lasted less than one pass of the game loop would be pressed and
			// released inside the same sample, and the game would not see a click at all. The
			// release waits out the shortest hold the loop can observe.
			if (MapperState.PressPending) {
				// The contact lifted before the press was due: press on the next pass and release
				// after the hold, so the game sees the click. Without the scheduled release the
				// button stayed down and the click only completed on the next tap.
				MapperState.PressAt = timestamp;
				MapperState.ReleaseX = x;
				MapperState.ReleaseY = y;
				MapperState.ReleaseButton = SDL_BUTTON_LEFT;
				MapperState.ReleasePending = true;
				MapperState.ReleaseAt = timestamp + CLICK_HOLD_MS;
			} else {
				Uint64 const heldsince = timestamp >= MapperState.LeftPressedAt
					? timestamp - MapperState.LeftPressedAt
					: 0;
				if (heldsince < CLICK_HOLD_MS) {
					Schedule_Release(x, y, SDL_BUTTON_LEFT, MapperState.LeftPressedAt + CLICK_HOLD_MS);
				} else {
					Emit_Button(sink, x, y, SDL_BUTTON_LEFT, false);
				}
			}

			// A pan's contact and a cancelled one were never a click, but the button has to come
			// up either way.
			if (!suppressed && !canceled && held && still) {
				Emit_Click(sink, x, y, SDL_BUTTON_RIGHT, timestamp);
			}
			return;
		}

		case Gesture::Drag:
			if (remaining == 0) {
				MapperState.Current = Gesture::Done;
			} else {
				Reanchor_As_Pending(timestamp, id);
			}
			// The band box ends where the finger lifted, not at its last move.
			Emit_Motion(sink, x, y);
			Emit_Button(sink, x, y, SDL_BUTTON_LEFT, false);
			return;

		case Gesture::Pan:
			if (remaining == 0) {
				MapperState.Current = Gesture::Done;
				MapperState.Panning = false;
			} else {
				Reanchor_As_Pending(timestamp, id);
			}
			return;
	}
}

} // namespace


void Touch_Reset(void)
{
	// A contact stream can end without a lift, so a button that is still down is scheduled to
	// come up on the next pass rather than left down for the rest of the session.
	bool const release = MapperState.LeftDown || MapperState.RightDown;
	Uint8 const button = MapperState.RightDown ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
	float const x = MapperState.PointerX;
	float const y = MapperState.PointerY;

	MapperState = Mapper();

	if (release) {
		MapperState.ReleasePending = true;
		MapperState.ReleaseButton = button;
		MapperState.ReleaseX = x;
		MapperState.ReleaseY = y;
		MapperState.ReleaseAt = 0;
	}
}

void Touch_Handle_Event(Uint32 type, SDL_FingerID fingerid, float x, float y,
	Uint64 timestamp, int width, int height, void (*sink)(SDL_Event const &))
{
	if (sink == nullptr || width <= 0 || height <= 0) {
		return;
	}

	float const px = x * (float)width;
	float const py = y * (float)height;

	switch (type) {
		case SDL_EVENT_FINGER_DOWN:
			On_Finger_Down(fingerid, px, py, timestamp, width, height, sink);
			break;

		case SDL_EVENT_FINGER_MOTION:
			On_Finger_Motion(fingerid, px, py, timestamp, sink);
			break;

		case SDL_EVENT_FINGER_UP:
			On_Finger_Up(fingerid, px, py, timestamp, false, sink);
			Tracked_Remove(fingerid);
			break;

		case SDL_EVENT_FINGER_CANCELED:
			On_Finger_Up(fingerid, px, py, timestamp, true, sink);
			Tracked_Remove(fingerid);
			break;

		default:
			break;
	}
}

void Touch_Tick(Uint64 timestamp, void (*sink)(SDL_Event const &))
{
	if (MapperState.PressPending && timestamp >= MapperState.PressAt) {
		MapperState.PressPending = false;
		Emit_Motion(sink, MapperState.PressX, MapperState.PressY);
		Emit_Button(sink, MapperState.PressX, MapperState.PressY, MapperState.PressButton, true);
		MapperState.LeftPressedAt = timestamp;

		// A context click is a press and a release with nothing in between, so its release is
		// scheduled here. The left button is a held gesture: it comes up when the contact does,
		// which is what keeps a select box growing while the finger is down.
		if (MapperState.PressButton == SDL_BUTTON_RIGHT) {
			MapperState.ReleaseX = MapperState.PressX;
			MapperState.ReleaseY = MapperState.PressY;
			MapperState.ReleaseButton = SDL_BUTTON_RIGHT;
			MapperState.ReleasePending = true;
			MapperState.ReleaseAt = timestamp + CLICK_HOLD_MS;
		}
		return;
	}

	if (!MapperState.ReleasePending || timestamp < MapperState.ReleaseAt) {
		return;
	}

	MapperState.ReleasePending = false;
	Emit_Button(sink, MapperState.ReleaseX, MapperState.ReleaseY, MapperState.ReleaseButton, false);
}

bool Touch_Take_Pan(TouchPan & pan)
{
	if (!MapperState.PanPending) {
		return(false);
	}

	pan.X = MapperState.PanX;
	pan.Y = MapperState.PanY;
	MapperState.PanX = 0;
	MapperState.PanY = 0;
	MapperState.PanPending = false;
	return(true);
}

bool Touch_Pan_Active(void)
{
	return(MapperState.Panning);
}

bool Touch_Pointer_Position(int & x, int & y)
{
	if (!MapperState.HasPointer) {
		return(false);
	}

	x = (int)std::lround(MapperState.PointerX);
	y = (int)std::lround(MapperState.PointerY);
	return(true);
}

bool Touch_Button_Down(Uint8 button)
{
	if (button == SDL_BUTTON_LEFT) {
		return(MapperState.LeftDown);
	}
	if (button == SDL_BUTTON_RIGHT) {
		return(MapperState.RightDown);
	}
	return(false);
}
