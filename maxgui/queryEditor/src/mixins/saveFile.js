/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters, mapMutations } from 'vuex'
import QueryTab from '@queryEditorSrc/store/orm/models/QueryTab'
export default {
    computed: {
        ...mapGetters({
            hasFileSystemRWAccess: 'editor/hasFileSystemRWAccess',
            getQueryTabFileHandle: 'editor/getQueryTabFileHandle',
            getQueryTabFileHandleName: 'editor/getQueryTabFileHandleName',
            checkQueryTabFileHandleValidity: 'editor/checkQueryTabFileHandleValidity',
        }),
    },
    methods: {
        ...mapMutations({
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
                    this.$logger.error('An error occurred trying to open the file.')
            }
        },
        /**
         * Download the file to user's disk
         * @param {Object} queryTab - queryTab object
         */
        saveFileLegacy(queryTab) {
            const { id: queryTabId, query_txt, name: queryTabName } = queryTab
            let a = document.createElement('a')
            // If there is no file_handle, use the current queryTab name
            const fileName = this.getQueryTabFileHandleName(queryTab) || `${queryTabName}.sql`
            a.href = `data:application/text;charset=utf-8,${encodeURIComponent(query_txt)}`
            a.download = fileName
            document.body.appendChild(a)
            a.click()
            document.body.removeChild(a)
            // update blob_file
            this.SET_BLOB_FILE({
                payload: { ...queryTab.blob_file, txt: query_txt },
                id: queryTabId,
            })
        },
        /**
         * @private
         * @param {Object} queryTab - queryTab object
         */
        async saveFileAs(queryTab) {
            let fileHandleName = queryTab.name
            if (!this.checkQueryTabFileHandleValidity(queryTab)) fileHandleName += '.sql'
            const fileHandle = await this.getNewFileHandle(fileHandleName)
            try {
                await this.writeFile({ fileHandle, contents: queryTab.query_txt })
                QueryTab.update({ where: queryTab.id, data: { name: fileHandle.name } })
                // update blob_file
                this.SET_BLOB_FILE({
                    payload: { file_handle: fileHandle, txt: queryTab.query_txt },
                    id: queryTab.id,
                })
            } catch (ex) {
                this.$logger.error('Unable to write file')
            }
        },

        // public methods
        /**
         * @public
         * @param {Object} queryTab - queryTab object
         */
        async handleSaveFileAs(queryTab) {
            if (this.hasFileSystemRWAccess) await this.saveFileAs(queryTab)
            else this.saveFileLegacy(queryTab)
        },
        /**
         * @public
         * Save new content for the opened file to user's local device
         * @param {Object} queryTab - queryTab object
         */
        async saveFileToDisk(queryTab) {
            try {
                const fileHandle = this.getQueryTabFileHandle(queryTab)
                const hasPriv = await this.verifyWritePriv(fileHandle)
                if (hasPriv) {
                    await this.writeFile({ fileHandle, contents: queryTab.query_txt })
                    // update blob_file
                    this.SET_BLOB_FILE({
                        payload: { file_handle: fileHandle, txt: queryTab.query_txt },
                        id: queryTab.id,
                    })
                }
            } catch (e) {
                this.$logger.error(e)
            }
        },
        /**
         * @public
         * @param {Object} queryTab - queryTab object
         */
        async handleSaveFile(queryTab) {
            if (this.hasFileSystemRWAccess && this.checkQueryTabFileHandleValidity(queryTab))
                await this.saveFileToDisk(queryTab)
            else await this.handleSaveFileAs(queryTab)
        },
    },
}
