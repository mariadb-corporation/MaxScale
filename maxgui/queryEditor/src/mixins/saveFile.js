/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters, mapMutations } from 'vuex'
export default {
    computed: {
        ...mapGetters({
            hasFileSystemRWAccess: 'editor/hasFileSystemRWAccess',
            getSessFileHandle: 'editor/getSessFileHandle',
            getSessFileHandleName: 'editor/getSessFileHandleName',
            checkSessFileHandleValidity: 'editor/checkSessFileHandleValidity',
        }),
    },
    methods: {
        ...mapMutations({
            UPDATE_SESSION: 'querySession/UPDATE_SESSION',
            SET_BLOB_FILE: 'editor/SET_BLOB_FILE',
        }),
        /**
         * @private
         * Verify the user has granted permission to read and write to the file, if
         * permission hasn't been granted, request permission.
         * @param {<FileSystemFileHandle>} fileHandle File handle to check.
         * @return {boolean} True if the user has granted read/write permission.
         */
        async verifyWritePriv(fileHandle) {
            const opts = {}
            opts.writable = true
            opts.mode = 'readwrite' // For Chrome 86 and later...
            // Check if we already have permission, if so, return true.
            if ((await fileHandle.queryPermission(opts)) === 'granted') return true
            // Request permission to the file, if the user grants permission, return true.
            if ((await fileHandle.requestPermission(opts)) === 'granted') return true
            // The user didn't grant permission, return false.
            return false
        },
        /**
         * @private
         * Writes the contents to disk.
         * @param {<FileSystemFileHandle>} param.fileHandle File handle to write to.
         * @param {string} param.contents Contents to write.
         */
        async writeFile({ fileHandle, contents }) {
            // Support for Chrome 82 and earlier.
            if (fileHandle.createWriter) {
                // Create a writer (request permission if necessary).
                const writer = await fileHandle.createWriter()
                // Write the full length of the contents
                await writer.write(0, contents)
                // Close the file and write the contents to disk
                await writer.close()
                return
            }
            // For Chrome 83 and later.
            // Create a FileSystemWritableFileStream to write to.
            const writable = await fileHandle.createWritable()
            // Write the contents of the file to the stream.
            await writable.write(contents)
            // Close the file and write the contents to disk.
            await writable.close()
        },
        /**
         * @private
         * Create a handle to a new text file on the local file system.
         * @param {string} suggestedName - suggestedName for the file
         * @returns {Promise<FileSystemFileHandle>} Handle to the new file.
         */
        async getNewFileHandle(suggestedName) {
            try {
                // For Chrome 86 and later...
                if ('showSaveFilePicker' in window)
                    return await window.showSaveFilePicker({ suggestedName })
                // For Chrome 85 and earlier...
                return await window.chooseFileSystemEntries({ suggestedName, type: 'save-file' })
            } catch (ex) {
                if (!ex.name === 'AbortError')
                    this.$logger('mixins-saveFile-getNewFileHandle').error(
                        'An error occurred trying to open the file.'
                    )
            }
        },
        /**
         * Download the file to user's disk
         * @param {Object} session - session object
         */
        saveFileLegacy(session) {
            const { id: sessionId, query_txt, name: sessionName } = session
            let a = document.createElement('a')
            // If there is no file_handle, use the current session tab name
            const fileName = this.getSessFileHandleName(session) || `${sessionName}.sql`
            a.href = `data:application/text;charset=utf-8,${encodeURIComponent(query_txt)}`
            a.download = fileName
            document.body.appendChild(a)
            a.click()
            document.body.removeChild(a)
            // update blob_file
            this.SET_BLOB_FILE({
                payload: { ...session.blob_file, txt: query_txt },
                id: sessionId,
            })
        },
        /**
         * @private
         * @param {Object} session - session object
         */
        async saveFileAs(session) {
            let fileHandleName = session.name
            if (!this.checkSessFileHandleValidity(session)) fileHandleName += '.sql'
            const fileHandle = await this.getNewFileHandle(fileHandleName)
            try {
                await this.writeFile({ fileHandle, contents: session.query_txt })
                this.UPDATE_SESSION({ ...session, name: fileHandle.name }) // update tab name
                // update blob_file
                this.SET_BLOB_FILE({
                    payload: { file_handle: fileHandle, txt: session.query_txt },
                    id: session.id,
                })
            } catch (ex) {
                this.$logger('mixins-saveFile-saveFileAs').error('Unable to write file')
            }
        },

        // public methods
        /**
         * @public
         * @param {Object} session - session object
         */
        async handleSaveFileAs(session) {
            if (this.hasFileSystemRWAccess) await this.saveFileAs(session)
            else this.saveFileLegacy(session)
        },
        /**
         * @public
         * Save new content for the opened file to user's local device
         * @param {Object} session - session object
         */
        async saveFileToDisk(session) {
            try {
                const fileHandle = this.getSessFileHandle(session)
                const hasPriv = await this.verifyWritePriv(fileHandle)
                if (hasPriv) {
                    await this.writeFile({ fileHandle, contents: session.query_txt })
                    // update blob_file
                    this.SET_BLOB_FILE({
                        payload: { file_handle: fileHandle, txt: session.query_txt },
                        id: session.id,
                    })
                }
            } catch (e) {
                this.$logger('mixins-saveFile-saveFileToDisk').error(e)
            }
        },
        /**
         * @public
         * @param {Object} session - session object
         */
        async handleSaveFile(session) {
            if (this.hasFileSystemRWAccess && this.checkSessFileHandleValidity(session))
                await this.saveFileToDisk(session)
            else await this.handleSaveFileAs(session)
        },
    },
}
