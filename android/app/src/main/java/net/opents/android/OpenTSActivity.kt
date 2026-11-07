package net.opents.android

import android.os.Bundle
import android.util.Log
import org.libsdl.app.SDLActivity
import java.io.File
import java.io.FileOutputStream
import java.io.IOException

/**
 * The activity the launcher starts. SDL owns the window, the input and the main thread; this
 * class names the libraries to load and unpacks the engine's UI documents before it starts.
 */
class OpenTSActivity : SDLActivity() {
    /**
     * SDL loads every library here before it calls SDL_main, and treats the last one as the
     * library that holds SDL_main. Naming the engine last keeps that default working.
     */
    override fun getLibraries(): Array<String> = arrayOf("SDL3", "OpenTS")

    override fun onCreate(savedInstanceState: Bundle?) {
        extractAssets("ui", File(filesDir, "ui"))
        super.onCreate(savedInstanceState)
    }

    /**
     * Copies one directory out of the APK, because the engine opens its documents by path and
     * cannot read them from the archive. A file that is already there is left alone.
     */
    private fun extractAssets(directory: String, target: File) {
        try {
            val names = assets.list(directory)
            if (names.isNullOrEmpty()) {
                return
            }
            if (!target.isDirectory && !target.mkdirs()) {
                Log.w(TAG, "could not create $target")
                return
            }
            for (name in names) {
                val asset = "$directory/$name"
                val file = File(target, name)
                if (!assets.list(asset).isNullOrEmpty()) {
                    extractAssets(asset, file)
                } else {
                    copyAsset(asset, file)
                }
            }
        } catch (error: IOException) {
            Log.e(TAG, "asset extraction failed for $directory", error)
        }
    }

    private fun copyAsset(asset: String, file: File) {
        if (file.isFile && file.length() > 0) {
            return
        }
        assets.open(asset).use { input ->
            FileOutputStream(file).use { output ->
                input.copyTo(output, bufferSize = 16 * 1024)
            }
        }
    }

    private companion object {
        private const val TAG = "OpenTS"
    }
}
