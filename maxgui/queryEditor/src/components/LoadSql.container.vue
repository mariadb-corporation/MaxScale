<template>
    <div>
        <confirm-dialog
            v-model="confDlg.isOpened"
            :title="confDlg.title"
            :type="confDlg.type"
            minBodyWidth="768px"
            :closeImmediate="true"
            :lazyValidation="false"
            :onSave="confDlg.onSave"
            cancelText="dontSave"
            saveText="save"
            @on-cancel="confDlg.dontSave"
        >
            <template v-slot:form-body>
                <p v-html="confDlg.confirmMsg" />
            </template>
        </confirm-dialog>
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop mxs-color-helper text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn
                    text
                    class="load-sql-btn toolbar-square-btn"
                    type="file"
                    v-on="on"
                    @click="handleFileOpen"
                >
                    <v-icon size="18" color="accent-dark">
                        {{ supportFs ? 'mdi-file-outline' : 'mdi-file-upload-outline' }}
                    </v-icon>
                    <input ref="uploader" class="d-none" type="file" @input="onFileLoadChanged" />
                </v-btn>
            </template>
            <span style="white-space: pre;" class="d-inline-block text-center">
                {{ `${$t('openScript')}\nCmd/Ctrl + O` }}
            </span>
        </v-tooltip>
        <v-tooltip
            v-if="hasFullSupport"
            top
            transition="slide-y-transition"
            content-class="shadow-drop mxs-color-helper text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn
                    text
                    class="save-sql-btn toolbar-square-btn"
                    type="file"
                    :disabled="!getIsFileUnsaved || !hasFileHandle"
                    v-on="on"
                    @click="saveFile"
                >
                    <v-icon size="20" color="accent-dark">
                        mdi-content-save-outline
                    </v-icon>
                </v-btn>
            </template>
            <span style="white-space: pre;" class="d-inline-block text-center">
                {{ `${$t('saveScript')}\nCmd/Ctrl + S` }}
            </span>
        </v-tooltip>
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop mxs-color-helper text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn
                    text
                    class="save-sql-btn toolbar-square-btn"
                    type="file"
                    :disabled="!getIsFileUnsaved"
                    v-on="on"
                    @click="hasFullSupport ? saveFileAs() : saveFileLegacy()"
                >
                    <v-icon size="20" color="accent-dark">
                        mdi-content-save-edit-outline
                    </v-icon>
                </v-btn>
            </template>
            <span style="white-space: pre;" class="d-inline-block text-center">
                {{ `${$t('saveScriptAs')}\nCmd/Ctrl + Shift + S` }}
            </span>
        </v-tooltip>
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapGetters, mapMutations } from 'vuex'
import { fileOpen, supported } from 'browser-fs-access'

export default {
    name: 'load-sql-ctr',
    data() {
        return {
            confDlg: {
                isOpened: false,
                title: this.$t('openScript'),
                type: 'openScript',
                confirmMsg: '',
                onSave: () => null,
                dontSave: () => null,
            },
        }
    },
    computed: {
        ...mapState({
            query_sessions: state => state.querySession.query_sessions,
            active_wke_id: state => state.wke.active_wke_id,
            blob_file: state => state.editor.blob_file,
            query_txt: state => state.editor.query_txt,
        }),
        ...mapGetters({
            getActiveSessionId: 'querySession/getActiveSessionId',
            getActiveSession: 'querySession/getActiveSession',
            getIsFileUnsaved: 'editor/getIsFileUnsaved',
        }),
        supportFs() {
            return supported
        },
        isInSecureCtx() {
            return window.location.protocol.includes('https')
        },
        hasFullSupport() {
            return this.supportFs && this.isInSecureCtx
        },
        hasFileHandle() {
            return Boolean(this.$typy(this.blob_file, 'file_handle.name').safeString)
        },
    },
    methods: {
        ...mapMutations({
            SET_SNACK_BAR_MESSAGE: 'appNotifier/SET_SNACK_BAR_MESSAGE',
            SET_QUERY_TXT: 'editor/SET_QUERY_TXT',
            UPDATE_SESSION: 'querySession/UPDATE_SESSION',
            SET_BLOB_FILE: 'editor/SET_BLOB_FILE',
        }),
        /**
         * Legacy support for reading uploaded file
         * @param {<FileSystemFileHandle>} fileHandle File handle
         * @returns {String} returns file content
         */
        getFileTextLegacy(fileHandle) {
            const reader = new FileReader()
            return new Promise((resolve, reject) => {
                reader.onerror = () => {
                    reader.abort()
                    reject(new DOMException('Problem parsing input file.'))
                    this.SET_SNACK_BAR_MESSAGE({ text: [reader.error], type: 'error' })
                }
                reader.onload = () => {
                    resolve(reader.result)
                }
                reader.readAsText(fileHandle)
            })
        },
        /**
         * @param {<FileSystemFileHandle>} fileHandle File handle.
         * @returns {String} returns file content
         */
        async getFileTxt(fileHandle) {
            if (this.supportFs) {
                // get file contents
                const file = await fileHandle.getFile()
                return await file.text()
            }
            return await this.getFileTextLegacy(fileHandle)
        },

        /**
         * @param {Blob} blob - blob
         */
        openConfDlg(blob) {
            this.confDlg = {
                ...this.confDlg,
                isOpened: true,
                title: this.$t('openScript'),
                type: 'openScript',
                confirmMsg: this.$t('confirmations.openScript', {
                    targetId: this.getActiveSession.name,
                    fileNameToBeOpened: blob.handle.name,
                }),
                onSave: async () => await this.onSave(blob),
                dontSave: async () => await this.dontSave(blob),
            }
        },

        // legacy upload file changed support
        async onFileLoadChanged(e) {
            const blob = { handle: e.target.files[0] }
            await this.handleLoadFile(blob)
        },
        async openFileLegacy() {
            await this.$refs.uploader.click()
        },
        async openFile() {
            const blob = await fileOpen({ description: 'Text files' })
            await this.handleLoadFile(blob)
        },
        async handleFileOpen() {
            if (this.supportFs) this.openFile()
            else this.openFileLegacy()
        },
        /**
         * @param {Blob} blob - blob
         */
        async handleLoadFile(blob) {
            if (this.getIsFileUnsaved) this.openConfDlg(blob)
            else await this.loadFileToActiveSession(blob)
        },
        /**
         * @param {Blob} blob - blob
         */
        async loadFileToActiveSession(blob) {
            const blobTxt = await this.getFileTxt(blob.handle)
            this.SET_QUERY_TXT({ payload: blobTxt, id: this.getActiveSessionId })
            this.UPDATE_SESSION({
                idx: this.query_sessions.indexOf(this.getActiveSession),
                session: {
                    ...this.$helpers.lodash.cloneDeep(this.getActiveSession),
                    name: blob.handle.name,
                },
            })
            if (!this.supportFs)
                /**
                 * clear the uploader file input so that if the user upload the same file,
                 * onFileLoadChanged event handler can be triggered again to show the dialog
                 */
                this.$refs.uploader.value = ''
            // once file is loaded, store file_handle to the session
            this.SET_BLOB_FILE({
                payload: {
                    file_handle: blob.handle,
                    /* store its txt so that it can be retrieved
                     * because the permission to read the file is withdrawn
                     * when the browser is refreshed or closed
                     */
                    txt: blobTxt,
                },
                id: this.getActiveSessionId,
            })
        },

        /**
         * @param {Blob} blob - blob
         */
        async dontSave(blob) {
            await this.loadFileToActiveSession(blob)
            this.confDlg.isOpened = false
        },
        /**
         * @param {Blob} blob - blob
         */
        async onSave(blob) {
            await this.handleSaveFile()
            // load new blob
            await this.loadFileToActiveSession(blob)
        },
        async handleSaveFile() {
            if (!this.hasFullSupport) this.saveFileLegacy()
            else if (this.hasFileHandle) await this.saveFile()
            else await this.saveFileAs()
        },
        /*
         * Download the file to user's local device
         */
        saveFileLegacy() {
            let a = document.createElement('a')
            /** legacy download file name
             *  If there is no file_handle, use the current session tab name
             */
            const fileName =
                `${this.$typy(this.blob_file, 'file_handle.name').safeString}` ||
                `${this.getActiveSession.name}.sql`

            a.href = `data:application/text;charset=utf-8;, ${encodeURIComponent(this.query_txt)}`
            a.download = fileName
            document.body.appendChild(a)
            a.click()
            document.body.removeChild(a)
            // update blob_file
            this.SET_BLOB_FILE({
                payload: {
                    ...this.blob_file,
                    txt: this.query_txt,
                },
                id: this.getActiveSessionId,
            })
        },
        /*
         *Save new content for the opened file to user's local device
         */
        async saveFile() {
            try {
                const hasPriv = await this.verifyWritePriv(this.blob_file.file_handle)
                if (hasPriv) {
                    await this.writeFile({
                        fileHandle: this.blob_file.file_handle,
                        contents: this.query_txt,
                    })
                    // update blob_file
                    this.SET_BLOB_FILE({
                        payload: {
                            file_handle: this.blob_file.file_handle,
                            txt: this.query_txt,
                        },
                        id: this.getActiveSessionId,
                    })
                }
            } catch (e) {
                this.$logger('load-sql-ctr-saveFile').error(e)
            }
        },
        async saveFileAs() {
            let fileHandle = await this.getNewFileHandle(
                `${this.getActiveSession.name}${this.hasFileHandle ? '' : '.sql'}`
            )
            try {
                await this.writeFile({ fileHandle, contents: this.query_txt })
                // update blob_file
                this.SET_BLOB_FILE({
                    payload: { file_handle: fileHandle, txt: this.query_txt },
                    id: this.getActiveSessionId,
                })
                // update session tab name
                this.UPDATE_SESSION({
                    idx: this.query_sessions.indexOf(this.getActiveSession),
                    session: {
                        ...this.$helpers.lodash.cloneDeep(this.getActiveSession),
                        name: fileHandle.name,
                    },
                })
            } catch (ex) {
                this.$logger('load-sql-ctr-saveFileAs').error('Unable to write file')
            }
        },
        /**
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
         * Create a handle to a new text file on the local file system.
         * @param {string} suggestedName - suggestedName for the file
         * @returns {Promise<FileSystemFileHandle>} Handle to the new file.
         */
        getNewFileHandle(suggestedName) {
            try {
                // For Chrome 86 and later...
                if ('showSaveFilePicker' in window)
                    return window.showSaveFilePicker({
                        suggestedName,
                    })
                // For Chrome 85 and earlier...
                return window.chooseFileSystemEntries({
                    suggestedName,
                    type: 'save-file',
                })
            } catch (ex) {
                if (!ex.name === 'AbortError')
                    this.$logger('load-sql-ctr-getNewFileHandle').error(
                        'An error occurred trying to open the file.'
                    )
            }
        },
    },
}
</script>
