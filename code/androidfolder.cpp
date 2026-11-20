/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "androidfolder.h"

#if defined(__ANDROID__)
#include <jni.h>
#include <SDL3/SDL_system.h>

#include <atomic>
#include <mutex>
#include <string>


namespace
{
	std::mutex _FolderLock;
	std::string _PickedFolder;
	bool _FolderAnswered = false;

	std::atomic<std::uint64_t> _CopyDone{ 0 };
	std::atomic<std::uint64_t> _CopyTotal{ 0 };
	bool _CopyFinished = false;
	bool _CopyOk = false;
	std::string _CopyMessage;
}


/// <summary>
/// Asks the activity to open the system folder picker. Returns false when the activity cannot
/// be reached or does not carry the picker.
/// </summary>
bool Android_Pick_Game_Folder(void)
{
	JNIEnv * env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());

	if (env == nullptr || activity == nullptr) {
		return(false);
	}

	bool started = false;
	jclass activityclass = env->GetObjectClass(activity);
	if (activityclass != nullptr) {
		jmethodID picker = env->GetMethodID(activityclass, "pickGameFolder", "()V");
		if (picker != nullptr) {
			env->CallVoidMethod(activity, picker);
			started = true;
		}
		env->DeleteLocalRef(activityclass);
	}

	env->DeleteLocalRef(activity);
	return(started);
}


/// <summary>
/// Reports the folder the picker answered with, once. An empty path means the player cancelled.
/// </summary>
bool Android_Game_Folder_Answered(std::string & path)
{
	std::lock_guard<std::mutex> guard(_FolderLock);
	if (!_FolderAnswered) {
		return(false);
	}

	path = _PickedFolder;
	_FolderAnswered = false;
	return(true);
}


void Android_Forget_Game_Folder(void)
{
	std::lock_guard<std::mutex> guard(_FolderLock);
	_PickedFolder.clear();
	_FolderAnswered = false;
	_CopyDone.store(0);
	_CopyTotal.store(0);
	_CopyFinished = false;
	_CopyOk = false;
	_CopyMessage.clear();
}


/// <summary>
/// Starts copying the chosen Storage Access Framework tree into target_dir on a worker thread.
/// </summary>
bool Android_Start_Game_Folder_Copy(std::string const & tree_uri, std::string const & target_dir)
{
	if (tree_uri.empty() || target_dir.empty()) {
		return(false);
	}

	JNIEnv * env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());

	if (env == nullptr || activity == nullptr) {
		return(false);
	}

	{
		std::lock_guard<std::mutex> guard(_FolderLock);
		_CopyDone.store(0);
		_CopyTotal.store(0);
		_CopyFinished = false;
		_CopyOk = false;
		_CopyMessage.clear();
	}

	bool started = false;
	jclass activityclass = env->GetObjectClass(activity);
	if (activityclass != nullptr) {
		jmethodID method = env->GetMethodID(activityclass, "copyGameFolder", "(Ljava/lang/String;Ljava/lang/String;)V");
		if (method != nullptr) {
			jstring juri = env->NewStringUTF(tree_uri.c_str());
			jstring jtarget = env->NewStringUTF(target_dir.c_str());
			if (juri != nullptr && jtarget != nullptr) {
				env->CallVoidMethod(activity, method, juri, jtarget);
				started = true;
			}
			if (juri != nullptr) {
				env->DeleteLocalRef(juri);
			}
			if (jtarget != nullptr) {
				env->DeleteLocalRef(jtarget);
			}
		}
		env->DeleteLocalRef(activityclass);
	}

	env->DeleteLocalRef(activity);
	return(started);
}


bool Android_Cancel_Game_Folder_Copy(void)
{
	JNIEnv * env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());

	if (env == nullptr || activity == nullptr) {
		return(false);
	}

	bool cancelled = false;
	jclass activityclass = env->GetObjectClass(activity);
	if (activityclass != nullptr) {
		jmethodID method = env->GetMethodID(activityclass, "cancelGameFolderCopy", "()Z");
		if (method != nullptr) {
			cancelled = (env->CallBooleanMethod(activity, method) == JNI_TRUE);
		}
		env->DeleteLocalRef(activityclass);
	}

	env->DeleteLocalRef(activity);
	return(cancelled);
}


void Android_Game_Folder_Copy_Progress(std::uint64_t & done, std::uint64_t & total)
{
	done = _CopyDone.load();
	total = _CopyTotal.load();
}


bool Android_Game_Folder_Copy_Result(bool & ok, std::string & message)
{
	std::lock_guard<std::mutex> guard(_FolderLock);
	if (!_CopyFinished) {
		return(false);
	}

	ok = _CopyOk;
	message = _CopyMessage;
	_CopyFinished = false;
	return(true);
}


extern "C" JNIEXPORT void JNICALL Java_net_opents_android_OpenTSActivity_nativeFolderPicked(JNIEnv * env, jclass, jstring uri)
{
	std::lock_guard<std::mutex> guard(_FolderLock);
	_PickedFolder.clear();

	if (uri != nullptr) {
		char const * text = env->GetStringUTFChars(uri, nullptr);
		if (text != nullptr) {
			_PickedFolder = text;
			env->ReleaseStringUTFChars(uri, text);
		}
	}

	_FolderAnswered = true;
}


extern "C" JNIEXPORT void JNICALL Java_net_opents_android_OpenTSActivity_nativeCopyProgress(JNIEnv *, jclass, jlong done, jlong total)
{
	_CopyDone.store(done > 0 ? static_cast<std::uint64_t>(done) : 0);
	_CopyTotal.store(total > 0 ? static_cast<std::uint64_t>(total) : 0);
}


extern "C" JNIEXPORT void JNICALL Java_net_opents_android_OpenTSActivity_nativeCopyFinished(JNIEnv * env, jclass, jboolean ok, jstring message)
{
	std::lock_guard<std::mutex> guard(_FolderLock);
	_CopyOk = (ok == JNI_TRUE);
	_CopyMessage.clear();

	if (message != nullptr) {
		char const * text = env->GetStringUTFChars(message, nullptr);
		if (text != nullptr) {
			_CopyMessage = text;
			env->ReleaseStringUTFChars(message, text);
		}
	}

	_CopyFinished = true;
}

#else

bool Android_Pick_Game_Folder(void)
{
	return(false);
}

bool Android_Game_Folder_Answered(std::string & path)
{
	(void)path;
	return(false);
}

void Android_Forget_Game_Folder(void)
{
}

bool Android_Start_Game_Folder_Copy(std::string const & tree_uri, std::string const & target_dir)
{
	(void)tree_uri;
	(void)target_dir;
	return(false);
}

bool Android_Cancel_Game_Folder_Copy(void)
{
	return(false);
}

void Android_Game_Folder_Copy_Progress(std::uint64_t & done, std::uint64_t & total)
{
	done = 0;
	total = 0;
}

bool Android_Game_Folder_Copy_Result(bool & ok, std::string & message)
{
	(void)ok;
	(void)message;
	return(false);
}

#endif
