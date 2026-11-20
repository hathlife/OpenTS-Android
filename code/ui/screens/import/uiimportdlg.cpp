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
#include "androidfolder.h"
#include "ui/screens/import/uiimport.h"
#include "ui/uienginehost.h"
#include "ui/uishell.h"
#include "ui/uiview.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>


namespace
{

enum class ImportStep
{
	ChooseFolder,
	Picking,
	Copying,
	Cancelling,
	Done,
	Failed,
};


std::string Format_Copy_Progress(std::uint64_t done, std::uint64_t total)
{
	constexpr std::uint64_t mb = 1024ULL * 1024ULL;
	char buffer[96];

	if (total > 0) {
		std::snprintf(buffer, sizeof(buffer), "Copying: %llu / %llu MB.",
			static_cast<unsigned long long>(done / mb),
			static_cast<unsigned long long>(total / mb));
	} else {
		std::snprintf(buffer, sizeof(buffer), "Copying: %llu MB.",
			static_cast<unsigned long long>(done / mb));
	}
	return(buffer);
}

}


bool UI_Import_Dialog(std::string const & game_path)
{
	UIImportPresenterClass presenter;
	presenter.Set_Path(game_path);

	ImportStep step = ImportStep::ChooseFolder;

	auto apply_step = [&](void) {
		switch (step) {
			case ImportStep::ChooseFolder:
				presenter.ChooseLabel = "Choose folder";
				presenter.CancelLabel = "Quit";
				presenter.CanChoose = true;
				presenter.CanCancel = true;
				break;

			case ImportStep::Picking:
				presenter.Step = "Waiting for the folder.";
				presenter.ChooseLabel = "Choose folder";
				presenter.CancelLabel = "Cancel";
				presenter.CanChoose = false;
				presenter.CanCancel = false;
				break;

			case ImportStep::Copying:
				presenter.ChooseLabel = "Choose folder";
				presenter.CancelLabel = "Cancel";
				presenter.CanChoose = false;
				presenter.CanCancel = true;
				break;

			case ImportStep::Cancelling:
				presenter.Step = "Cancelling...";
				presenter.ChooseLabel = "Choose folder";
				presenter.CancelLabel = "Cancel";
				presenter.CanChoose = false;
				presenter.CanCancel = false;
				break;

			case ImportStep::Done:
				presenter.Step = "Game files copied.";
				presenter.ChooseLabel = "Start game";
				presenter.CancelLabel = "Quit";
				presenter.CanChoose = true;
				presenter.CanCancel = true;
				break;

			case ImportStep::Failed:
				presenter.ChooseLabel = "Choose folder";
				presenter.CancelLabel = "Quit";
				presenter.CanChoose = true;
				presenter.CanCancel = true;
				break;
		}
	};

	apply_step();

	presenter.OnChoose = [&](void) {
		if (step == ImportStep::Done) {
			presenter.Result = UI_RESULT_ACCEPTED;
			return;
		}

		Android_Forget_Game_Folder();
		if (!Android_Pick_Game_Folder()) {
			step = ImportStep::Failed;
			presenter.Step = "Import failed: this build has no folder picker.";
			apply_step();
			return;
		}

		step = ImportStep::Picking;
		apply_step();
	};

	presenter.OnCancel = [&](void) {
		if (step == ImportStep::Copying) {
			if (Android_Cancel_Game_Folder_Copy()) {
				step = ImportStep::Cancelling;
				apply_step();
			}
			return;
		}

		presenter.Result = UI_RESULT_CANCELLED;
	};

	std::unique_ptr<UIViewClass> view = UI_Import_View(presenter);
	UIResult const result = UIShell.Run_Modal(*view, [&](void) {
		bool ended = UI_Service_Game();

		if (step == ImportStep::Picking) {
			std::string folder;
			if (Android_Game_Folder_Answered(folder)) {
				if (folder.empty()) {
					step = ImportStep::ChooseFolder;
					presenter.Step.clear();
					apply_step();
				} else if (Android_Start_Game_Folder_Copy(folder, game_path)) {
					step = ImportStep::Copying;
					presenter.Step = Format_Copy_Progress(0, 0);
					apply_step();
				} else {
					step = ImportStep::Failed;
					presenter.Step = "Import failed: could not start copying.";
					apply_step();
				}
			}
		} else if (step == ImportStep::Copying || step == ImportStep::Cancelling) {
			bool ok = false;
			std::string message;
			if (Android_Game_Folder_Copy_Result(ok, message)) {
				if (ok) {
					step = ImportStep::Done;
					if (!message.empty()) {
						presenter.Set_Path(message);
					}
					apply_step();
				} else if (message == "cancelled") {
					step = ImportStep::ChooseFolder;
					presenter.Step.clear();
					apply_step();
				} else {
					step = ImportStep::Failed;
					presenter.Step = "Import failed: " + message;
					apply_step();
				}
			} else if (step == ImportStep::Copying) {
				std::uint64_t done = 0;
				std::uint64_t total = 0;
				Android_Game_Folder_Copy_Progress(done, total);
				presenter.Step = Format_Copy_Progress(done, total);
			}
		}

		return(ended);
	});

	return(result == UI_RESULT_ACCEPTED && step == ImportStep::Done);
}
