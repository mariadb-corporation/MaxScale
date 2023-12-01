/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryTab from '@wsModels/QueryTab'
import TxtEditor from '@wsModels/TxtEditor'
import { mapGetters, mapActions } from 'vuex'

export default {
    computed: {
        ...mapGetters({
            hasFileSystemRWAccess: 'fileSysAccess/hasFileSystemRWAccess',
            getFileHandle: 'fileSysAccess/getFileHandle',
            getFileHandleName: 'fileSysAccess/getFileHandleName',
            getIsFileHandleValid: 'fileSysAccess/getIsFileHandleValid',
        }),
    },
    methods: {
        ...mapActions({ updateFileHandleDataMap: 'fileSysAccess/updateFileHandleDataMap' }),
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
        async saveFileLegacy(queryTab) {
            const { id: queryTabId, name: queryTabName } = queryTab
            const editor = TxtEditor.find(queryTabId) || {}
            let a = document.createElement('a')
            // If there is no file_handle, use the current queryTab name
            const fileName = this.getFileHandleName(queryTab.id) || `${queryTabName}.sql`
            a.href = `data:application/text;charset=utf-8,${encodeURIComponent(editor.query_txt)}`
            a.download = fileName
            document.body.appendChild(a)
            a.click()
            document.body.removeChild(a)
            await this.updateFileHandleDataMap({
                id: editor.id,
                data: { txt: editor.query_txt },
            })
        },
        /**
         * @private
         * @param {Object} queryTab - queryTab object
         */
        async saveFileAs(queryTab) {
            let fileHandleName = queryTab.name
            if (!this.getIsFileHandleValid(queryTab.id)) fileHandleName += '.sql'
            const fileHandle = await this.getNewFileHandle(fileHandleName)
            try {
                const { query_txt } = TxtEditor.find(queryTab.id) || {}
                await this.writeFile({ fileHandle, contents: query_txt })
                QueryTab.update({ where: queryTab.id, data: { name: fileHandle.name } })
                await this.updateFileHandleDataMap({
                    id: queryTab.id,
                    data: { file_handle: fileHandle, txt: query_txt },
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
            else await this.saveFileLegacy(queryTab)
        },
        /**
         * @public
         * Save new content for the opened file to user's local device
         * @param {Object} queryTab - queryTab object
         */
        async saveFileToDisk(queryTab) {
            try {
                const fileHandle = this.getFileHandle(queryTab.id)
                const hasPriv = await this.verifyWritePriv(fileHandle)
                if (hasPriv) {
                    const { query_txt } = TxtEditor.find(queryTab.id) || {}
                    await this.writeFile({ fileHandle, contents: query_txt })
                    await this.updateFileHandleDataMap({
                        id: queryTab.id,
                        data: { file_handle: fileHandle, txt: query_txt },
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
            if (this.hasFileSystemRWAccess && this.getIsFileHandleValid(queryTab.id))
                await this.saveFileToDisk(queryTab)
            else await this.handleSaveFileAs(queryTab)
        },
    },
}
