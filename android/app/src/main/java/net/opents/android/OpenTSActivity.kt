package net.opents.android

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.DocumentsContract
import android.util.Log
import org.libsdl.app.SDLActivity
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.nio.file.FileVisitResult
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.StandardCopyOption
import java.nio.file.attribute.BasicFileAttributes
import java.util.concurrent.atomic.AtomicBoolean

/**
 * The activity the launcher starts. SDL owns the window, the input and the main thread; this
 * class names the libraries to load and unpacks the engine's UI documents before it starts.
 */
class OpenTSActivity : SDLActivity() {
    @Volatile
    private var copyCancelled = false
    private val copyActive = AtomicBoolean(false)

    /**
     * Opens the system folder picker so the player can point at a game installation. The
     * engine calls this through JNI and receives the choice in [nativeFolderPicked].
     */
    fun pickGameFolder() {
        try {
            val intent = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
            intent.addFlags(
                Intent.FLAG_GRANT_READ_URI_PERMISSION or
                    Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
            )
            startActivityForResult(intent, FOLDER_REQUEST)
        } catch (error: Exception) {
            Log.e(TAG, "no folder picker on this device", error)
            nativeFolderPicked(null)
        }
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        if (requestCode != FOLDER_REQUEST) {
            super.onActivityResult(requestCode, resultCode, data)
            return
        }
        val uri = if (resultCode == Activity.RESULT_OK) data?.data else null
        nativeFolderPicked(uri?.toString())
    }

    /**
     * Copies a Storage Access Framework directory tree into [targetPath] on a background
     * thread, reporting progress in [nativeCopyProgress] and the outcome in
     * [nativeCopyFinished].
     */
    fun copyGameFolder(treeUri: String, targetPath: String) {
        if (!copyActive.compareAndSet(false, true)) {
            return
        }
        copyCancelled = false
        val worker = Thread {
            var ok = false
            var message = ""
            var staging: File? = null
            try {
                val parsed = Uri.parse(treeUri)
                val target = File(targetPath)
                val parent = target.parentFile ?: filesDir
                if (!parent.isDirectory && !parent.mkdirs()) {
                    throw IOException("cannot create ${parent.absolutePath}")
                }
                val free = parent.usableSpace
                if (free < REQUIRED_BYTES) {
                    throw IOException(
                        "not enough free space: ${free shr 20} MB free, ${REQUIRED_BYTES shr 20} MB needed"
                    )
                }
                val missing = missingRetailArchives(parsed)
                if (missing.isNotEmpty()) {
                    throw IOException("that folder has no ${missing.joinToString(" or ")}")
                }
                val temp = Files.createTempDirectory(parent.toPath(), "ts-import-").toFile()
                staging = temp
                val totalBytes = measureTree(parsed)
                nativeCopyProgress(0L, totalBytes)
                var copiedBytes = 0L
                var reportedBytes = 0L
                val rootId = DocumentsContract.getTreeDocumentId(parsed)
                copyChildren(parsed, rootId, temp) { delta ->
                    copiedBytes += delta
                    if (copiedBytes - reportedBytes >= PROGRESS_STEP_BYTES) {
                        reportedBytes = copiedBytes
                        nativeCopyProgress(copiedBytes, totalBytes)
                    }
                }
                checkCancelled()
                nativeCopyProgress(copiedBytes, totalBytes)
                if (target.exists()) {
                    deleteTree(target.toPath())
                }
                Files.move(
                    temp.toPath(),
                    target.toPath(),
                    StandardCopyOption.REPLACE_EXISTING
                )
                staging = null
                ok = true
                message = target.absolutePath
            } catch (_: CopyCancelled) {
                ok = false
                message = "cancelled"
            } catch (error: Throwable) {
                Log.e(TAG, "game folder import failed", error)
                ok = false
                message = error.message ?: error.toString()
            } finally {
                staging?.let {
                    try {
                        deleteTree(it.toPath())
                    } catch (_: IOException) {
                    }
                }
                copyActive.set(false)
                nativeCopyFinished(ok, message)
            }
        }
        try {
            worker.start()
        } catch (error: Throwable) {
            copyActive.set(false)
            nativeCopyFinished(false, error.message ?: error.toString())
        }
    }

    fun cancelGameFolderCopy(): Boolean {
        if (!copyActive.get()) {
            return false
        }
        copyCancelled = true
        return true
    }

    private class CopyCancelled : IOException("Import cancelled")

    @Throws(CopyCancelled::class)
    private fun checkCancelled() {
        if (copyCancelled) {
            throw CopyCancelled()
        }
    }

    private fun missingRetailArchives(treeUri: Uri): List<String> {
        val rootId = DocumentsContract.getTreeDocumentId(treeUri)
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, rootId)
        val present = mutableSetOf<String>()
        contentResolver.query(
            children,
            arrayOf(DocumentsContract.Document.COLUMN_DISPLAY_NAME),
            null,
            null,
            null
        )?.use { cursor ->
            while (cursor.moveToNext()) {
                cursor.getString(0)?.lowercase()?.let(present::add)
            }
        }
        return REQUIRED_ARCHIVES.filterNot(present::contains)
    }

    private fun measureTree(treeUri: Uri): Long {
        return try {
            val rootId = DocumentsContract.getTreeDocumentId(treeUri)
            measureChildren(treeUri, rootId)
        } catch (_: Throwable) {
            0L
        }
    }

    private fun measureChildren(treeUri: Uri, documentId: String): Long {
        checkCancelled()
        var total = 0L
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, documentId)
        val columns = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE
        )
        contentResolver.query(children, columns, null, null, null)?.use { cursor ->
            while (cursor.moveToNext()) {
                checkCancelled()
                val id = cursor.getString(0) ?: continue
                val mime = cursor.getString(1)
                if (mime == DocumentsContract.Document.MIME_TYPE_DIR) {
                    total += measureChildren(treeUri, id)
                } else if (!cursor.isNull(2)) {
                    val size = cursor.getLong(2)
                    if (size > 0L) {
                        total += size
                    }
                }
            }
        }
        return total
    }

    private fun copyChildren(
        treeUri: Uri,
        documentId: String,
        dir: File,
        onBytes: (Long) -> Unit
    ) {
        checkCancelled()
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, documentId)
        val columns = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE
        )
        contentResolver.query(children, columns, null, null, null)?.use { cursor ->
            while (cursor.moveToNext()) {
                checkCancelled()
                val id = cursor.getString(0) ?: continue
                val name = cursor.getString(1) ?: continue
                val mime = cursor.getString(2)
                val out = safeChild(dir, name)
                if (mime == DocumentsContract.Document.MIME_TYPE_DIR) {
                    if (!out.isDirectory && !out.mkdir()) {
                        throw IOException("cannot create $name")
                    }
                    copyChildren(treeUri, id, out, onBytes)
                } else {
                    val source = DocumentsContract.buildDocumentUriUsingTree(treeUri, id)
                    contentResolver.openInputStream(source)?.use { input ->
                        FileOutputStream(out).use { output ->
                            val buffer = ByteArray(1 shl 16)
                            while (true) {
                                checkCancelled()
                                val count = input.read(buffer)
                                if (count < 0) {
                                    break
                                }
                                output.write(buffer, 0, count)
                                onBytes(count.toLong())
                            }
                        }
                    } ?: throw IOException("cannot read $name")
                }
            }
        } ?: throw IOException("cannot list $documentId")
    }

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
        assets.open(asset).use { input ->
            FileOutputStream(file).use { output ->
                input.copyTo(output, bufferSize = 16 * 1024)
            }
        }
    }

    private companion object {
        private const val TAG = "OpenTS"
        private const val FOLDER_REQUEST = 0x0F01
        private const val REQUIRED_BYTES = 500L shl 20
        private const val PROGRESS_STEP_BYTES = 8L shl 20
        private val REQUIRED_ARCHIVES = listOf("tibsun.mix")

        @Throws(IOException::class)
        private fun safeChild(directory: File, name: String): File {
            if (name.isEmpty() || name == "." || name == ".." ||
                name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 || name.indexOf('\u0000') >= 0 ||
                File(name).isAbsolute
            ) {
                throw IOException("invalid file name in the chosen folder")
            }
            val child = File(directory, name).canonicalFile
            if (directory.canonicalFile != child.parentFile) {
                throw IOException("file escapes the import folder")
            }
            return child
        }

        @Throws(IOException::class)
        private fun deleteTree(root: Path) {
            Files.walkFileTree(
                root,
                object : SimpleFileVisitor<Path>() {
                    override fun visitFile(
                        file: Path,
                        attributes: BasicFileAttributes
                    ): FileVisitResult {
                        Files.delete(file)
                        return FileVisitResult.CONTINUE
                    }

                    override fun postVisitDirectory(
                        directory: Path,
                        error: IOException?
                    ): FileVisitResult {
                        if (error != null) {
                            throw error
                        }
                        Files.delete(directory)
                        return FileVisitResult.CONTINUE
                    }
                }
            )
        }

        /** Hands the chosen folder back to the engine, or null when the player cancelled. */
        @JvmStatic external fun nativeFolderPicked(uri: String?)

        /** Reports bytes copied so far and the total byte count when known (0 when unknown). */
        @JvmStatic external fun nativeCopyProgress(done: Long, total: Long)

        /** Reports the outcome of the background copy. */
        @JvmStatic external fun nativeCopyFinished(ok: Boolean, message: String?)
    }
}
