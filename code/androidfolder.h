/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <string>


// Opens the platform folder picker when the build has one.
bool Android_Pick_Game_Folder(void);

// True once the picker answered, with the chosen tree URI in path. Cancelling clears path.
bool Android_Game_Folder_Answered(std::string & path);

// Forgets an earlier answer so the next request starts clean.
void Android_Forget_Game_Folder(void);

// Starts copying the chosen Storage Access Framework tree into target_dir.
bool Android_Start_Game_Folder_Copy(std::string const & tree_uri, std::string const & target_dir);

// Asks the active copy to stop and clean up its partial files.
bool Android_Cancel_Game_Folder_Copy(void);

// Reads the latest byte counts from the active copy (total is 0 when unknown).
void Android_Game_Folder_Copy_Progress(std::uint64_t & done, std::uint64_t & total);

// True once the copy finishes, setting ok and the result path or failure message.
bool Android_Game_Folder_Copy_Result(bool & ok, std::string & message);

