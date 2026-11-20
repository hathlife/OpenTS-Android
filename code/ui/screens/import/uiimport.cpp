/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "ui/screens/import/uiimport.h"

#include "ui/rml/rmlview.h"

#include <utility>


UIImportPresenterClass::UIImportPresenterClass(void)
{
	Heading = "Import the game files";
	Body = "OpenTS needs the files from a Tiberian Sun installation. Pick the folder that holds "
		"them and the game copies what it needs into its own storage.";
	ChooseLabel = "Choose folder";
	CancelLabel = "Quit";
}


void UIImportPresenterClass::Execute(UIIntent const & intent)
{
	if (intent.Name == "choose") {
		if (!CanChoose) {
			return;
		}
		if (OnChoose) {
			OnChoose();
		} else {
			Result = UI_RESULT_ACCEPTED;
		}
	} else if (intent.Name == "cancel") {
		if (!CanCancel) {
			return;
		}
		if (OnCancel) {
			OnCancel();
		} else {
			Result = UI_RESULT_CANCELLED;
		}
	}
}


void UIImportPresenterClass::Refresh(void)
{
}


void UIImportPresenterClass::Set_Path(std::string const & path)
{
	Path = "Game files: " + path;
}


namespace
{

class UIImportViewClass : public UIRmlViewClass
{
	public:
		explicit UIImportViewClass(UIImportPresenterClass & presenter) :
			UIRmlViewClass(presenter, "import.rml", "import"),
			Data(presenter)
		{
		}

		virtual void Sync(void) override
		{
			Model.DirtyVariable("heading");
			Model.DirtyVariable("body");
			Model.DirtyVariable("step");
			Model.DirtyVariable("path");
			Model.DirtyVariable("chooselabel");
			Model.DirtyVariable("cancellabel");
			Model.DirtyVariable("canchoose");
			Model.DirtyVariable("cancancel");
		}

	protected:
		virtual bool Bind(Rml::DataModelConstructor & model) override
		{
			return(model.Bind("heading", &Data.Heading)
				&& model.Bind("body", &Data.Body)
				&& model.Bind("step", &Data.Step)
				&& model.Bind("path", &Data.Path)
				&& model.Bind("chooselabel", &Data.ChooseLabel)
				&& model.Bind("cancellabel", &Data.CancelLabel)
				&& model.Bind("canchoose", &Data.CanChoose)
				&& model.Bind("cancancel", &Data.CanCancel));
		}

	private:
		UIImportPresenterClass & Data;
};

}


std::unique_ptr<UIViewClass> UI_Import_View(UIImportPresenterClass & presenter)
{
	return(std::make_unique<UIImportViewClass>(presenter));
}
