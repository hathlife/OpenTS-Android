/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "_ui.h"
#include "globals.h"
#include "goptions.h"
#include "ui/screens/display/uidisplay.h"
#include "ui/uienginehost.h"
#include "ui/uishell.h"
#include "ui/uiview.h"
#include "misc.h"
#include "sidebar.h"

#include "gamewindow.h"
#include "mainopt.h"
#include "video.h"
#include "wwmouse.h"

#include <cstdio>


namespace
{

enum {
	MIN_WIDTH = 640,
	MIN_HEIGHT = 400,
	MAX_WIDTH = 4096,
	MAX_HEIGHT = 4096
};


class UIDisplayEngineServiceClass : public UIDisplayServiceClass
{
	public:
		virtual void Set_Stretch_Movies(bool on) override
		{
			Options.StretchMovies = on;
		}

		virtual void Set_System_Cursor(bool on) override
		{
			Options.SystemCursor = on;
			Refresh_Window_Arrow();
		}

		virtual void Set_View_Scale(int mode) override
		{
			Options.ViewScale = mode;
			Video_Refresh_View();
		}

		virtual void Set_Zoom(int percent) override
		{
			Options.ZoomPercent = percent;
			Game_Frame_Sync_To_Content(true);
		}

		virtual void Set_Sidebar(int percent) override
		{
			Options.SidebarPercent = percent;

			int const sidebar = (percent > 0) ? percent : 100;
			SidebarClass::Set_Width(SidebarClass::SIDE_WIDTH * sidebar / 100);

			// The battle split is held in the surfaces, so a running one is rebuilt.
			if (ScenarioActive) {
				Change_Display_Mode(VideoModeWidth, VideoModeHeight);
			}
		}
};

UIDisplayEngineServiceClass _Service;

}


UIDisplayServiceClass & UI_Display_Service(void)
{
	return(_Service);
}


void UI_Display_State(UIDisplayState & state)
{
	state = UIDisplayState();
	state.StretchMovies = Options.StretchMovies;
	state.SystemCursor = Options.SystemCursor;

	// One row is selected: a zoom that changes the frame size, otherwise the scale in use.
	int selected = -1;

	// The zoom choices name the percent of the base frame the battle is drawn in. Only the
	// steps the smallest frame allows are listed.
	int const zoom_steps[] = { 0, 100, 200, 300, 400, 500 };
	for (int percent : zoom_steps) {
		UIDisplayMode entry;
		char buffer[32];
		if (percent == 0) {
			std::snprintf(buffer, sizeof(buffer), "Zoom 100%% (auto)");
		} else {
			std::snprintf(buffer, sizeof(buffer), "Zoom %d%%", percent);
		}
		entry.Label = buffer;
		entry.Zoom = percent;
		if (selected < 0 && percent != 0 && percent == Options.ZoomPercent) {
			selected = (int)state.Modes.size();
		}
		state.Modes.push_back(entry);
	}

	int const sidebar_steps[] = { 0, 150, 200 };
	for (int percent : sidebar_steps) {
		UIDisplayMode entry;
		char buffer[32];
		if (percent == 0) {
			std::snprintf(buffer, sizeof(buffer), "Sidebar 100%%");
		} else {
			std::snprintf(buffer, sizeof(buffer), "Sidebar %d%%", percent);
		}
		entry.Label = buffer;
		entry.Sidebar = percent;
		if (selected < 0 && percent != 0 && percent == Options.SidebarPercent) {
			selected = (int)state.Modes.size();
		}
		state.Modes.push_back(entry);
	}

	char const * const scale_names[] = { "Fit the screen", "Whole pixels", "Actual size" };
	for (int mode = 0; mode < 3; mode++) {
		UIDisplayMode entry;
		entry.Label = scale_names[mode];
		entry.ViewScale = mode;
		if (selected < 0 && mode == Options.ViewScale) {
			selected = (int)state.Modes.size();
		}
		state.Modes.push_back(entry);
	}

	int * modes = EnumDisplayModes(MIN_WIDTH, MIN_HEIGHT, MAX_WIDTH, MAX_HEIGHT);
	if (modes != NULL) {
		for (int * entry = modes; *entry != 0; entry += 2) {
			UIDisplayMode mode;
			mode.Width = entry[0];
			mode.Height = entry[1];

			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%d x %d", mode.Width, mode.Height);
			mode.Label = buffer;

			if (selected < 0 && mode.Width == Options.ScreenWidth && mode.Height == Options.ScreenHeight) {
				selected = (int)state.Modes.size();
			}
			state.Modes.push_back(mode);
		}

		delete [] modes;
	}

	state.Selected = selected;
}


std::optional<UIDisplayMode> UI_Display_Dialog(void)
{
	UIDisplayState state;
	UI_Display_State(state);

	UIDisplayPresenterClass presenter(UI_Display_Service(), state);
	std::unique_ptr<UIViewClass> view = UI_Display_View(presenter);

	if (UI_Run_Modal(*view) == UI_RESULT_FAILED_TO_OPEN) {
		return(std::nullopt);
	}

	return(presenter.Picked);
}


bool UI_Confirm_Mode_Dialog(void)
{
	UIConfirmModePresenterClass presenter(UIShell.Clock());
	std::unique_ptr<UIViewClass> view = UI_Confirm_Mode_View(presenter);

	UIResult result = UI_Run_Modal(*view);

	if (result == UI_RESULT_FAILED_TO_OPEN) {
		return(true);
	}

	return(result == UI_RESULT_ACCEPTED);
}
