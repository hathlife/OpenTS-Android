/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "ui/uiscreen.h"

#include <functional>
#include <memory>
#include <string>

class UIViewClass;


// Holds the words the import wizard shows and the step it has reached.
class UIImportPresenterClass : public UIPresenterClass
{
	public:
		UIImportPresenterClass(void);
		virtual void Execute(UIIntent const & intent) override;
		virtual void Refresh(void) override;

		void Set_Path(std::string const & path);

		std::function<void(void)> OnChoose;
		std::function<void(void)> OnCancel;

		std::string Heading;
		std::string Body;
		std::string Step;
		std::string Path;
		std::string ChooseLabel;
		std::string CancelLabel;
		bool CanChoose = true;
		bool CanCancel = true;
};


std::unique_ptr<UIViewClass> UI_Import_View(UIImportPresenterClass & presenter);

// Runs the wizard once. Returns true when a folder was imported into game_path.
bool UI_Import_Dialog(std::string const & game_path);

