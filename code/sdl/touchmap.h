/*******************************************************************************
 *                                O P E N T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <SDL3/SDL.h>

//
// Translates touch contacts into the mouse gestures the game already understands.
//
// The game is a mouse game: it reads the cursor and the button edges, so a touchscreen has
// nothing to drive it with. SDL would click the left button once per contact, which leaves
// every right-click command and every drag unreachable. This maps each contact to what it
// means instead:
//
// - a quick release clicks the left button, which selects and presses the interface;
// - a drag past the threshold holds the left button from where the finger landed, which is
//   the select box over the battlefield and a slider or list drag over the interface;
// - two fingers pan the view, reported as a displacement rather than as mouse input so the
//   game's own right-drag handling does not treat the pan as a click when it ends;
// - a long, still contact presses the right button, which is the game's cancel and its
//   context command.
//
// A contact only moves the cursor until its gesture is unambiguous. A third contact is
// ignored, as is a second one outside a match.
//

/// <summary>
/// One pan step, in window pixels, with the direction the camera should move.
/// </summary>
struct TouchPan
{
	int X;
	int Y;
};

/// <summary>
/// Drops every tracked contact. A stream can end without a release, such as when the window
/// loses focus with fingers down, and a phantom contact would turn the next tap into a pan.
/// </summary>
void Touch_Reset(void);

/// <summary>
/// Maps one touch event to the mouse events it means and to the view motion it asks for.
/// The synthetic events are handed to the given sink in dispatch order.
/// </summary>
/// <param name="type">SDL_EVENT_FINGER_DOWN, _MOTION, _UP, or _CANCELED.</param>
/// <param name="fingerid">The contact's identity, stable for its lifetime.</param>
/// <param name="x">The contact's position, 0 to 1 across the window.</param>
/// <param name="y">The contact's position, 0 to 1 down the window.</param>
/// <param name="timestamp">SDL's millisecond clock.</param>
/// <param name="width">The window's width in pixels.</param>
/// <param name="height">The window's height in pixels.</param>
/// <param name="sink">Receives each synthetic event, in order.</param>
void Touch_Handle_Event(Uint32 type, SDL_FingerID fingerid, float x, float y,
	Uint64 timestamp, int width, int height, void (*sink)(SDL_Event const &));

/// <summary>
/// Releases a press whose hold has elapsed. The event pump calls this every pass, so a click
/// lasts long enough for the game to sample it.
/// </summary>
void Touch_Tick(Uint64 timestamp, void (*sink)(SDL_Event const &));

/// <summary>
/// Takes the view motion accumulated since the last call.
/// </summary>
/// <param name="pan">Receives the camera displacement in window pixels.</param>
/// <returns>bool; False when two fingers are not panning right now.</returns>
bool Touch_Take_Pan(TouchPan & pan);

/// <summary>
/// Whether a two-finger pan owns the view, so edge scrolling can stand aside.
/// </summary>
bool Touch_Pan_Active(void);

/// <summary>
/// Where the pointer is, in window pixels.
/// </summary>
/// <returns>bool; False before the first contact.</returns>
bool Touch_Pointer_Position(int & x, int & y);

/// <summary>
/// Whether the given mouse button is held by the touch layer.
/// </summary>
bool Touch_Button_Down(Uint8 button);
