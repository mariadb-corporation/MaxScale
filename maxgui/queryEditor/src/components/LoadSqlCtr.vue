<template>
    <div>
        <mxs-conf-dlg
            v-model="confDlg.isOpened"
            minBodyWidth="768px"
            :title="confDlg.title"
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
        </mxs-conf-dlg>
        <mxs-tooltip-btn
            btnClass="toolbar-square-btn load-sql-btn"
            text
            type="file"
            @click="handleFileOpen"
        >
            <template v-slot:btn-content>
                <v-icon size="18" color="accent-dark">
                    {{ supportFs ? 'mdi-file-outline' : 'mdi-file-upload-outline' }}
                </v-icon>
                <input ref="uploader" class="d-none" type="file" @input="onFileLoadChanged" />
            </template>
            {{ $mxs_t('openScript') }}
            <br />
            Cmd/Ctrl + O
        </mxs-tooltip-btn>
        <mxs-tooltip-btn
            v-if="hasFullSupport"
            btnClass="toolbar-square-btn save-sql-btn"
            text
            :disabled="isSaveFileDisabled"
            @click="saveFile"
        >
            <template v-slot:btn-content>
                <v-icon size="20" color="accent-dark">mdi-content-save-outline</v-icon>
            </template>
            {{ $mxs_t('saveScript') }}
            <br />
            Cmd/Ctrl + S
        </mxs-tooltip-btn>
        <mxs-tooltip-btn
            btnClass="toolbar-square-btn save-sql-btn"
            text
            :disabled="isSaveFileAsDisabled"
            @click="handleSaveFileAs"
        >
            <template v-slot:btn-content>
                <v-icon size="20" color="accent-dark">mdi-content-save-edit-outline</v-icon>
            </template>
            {{ $mxs_t('saveScriptAs') }}
            <br />
            Cmd/Ctrl + Shift + S
        </mxs-tooltip-btn>
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-09-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapGetters, mapMutations } from 'vuex'
import { fileOpen, supported } from 'browser-fs-access'
import { EventBus } from './EventBus'

export default {
    name: 'load-sql-ctr',
    props: { session: { type: Object, required: true } },
    data() {
        return {
            confDlg: {
                isOpened: false,
                title: this.$mxs_t('openScript'),
                confirmMsg: '',
                onSave: () => null,
                dontSave: () => null,
            },
        }
    },
    computed: {
        ...mapState({
            active_wke_id: state => state.wke.active_wke_id,
        }),
        ...mapGetters({
            getIsFileUnsavedBySessionId: 'editor/getIsFileUnsavedBySessionId',
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
        blobFile() {
            return this.session.blob_file
        },
        queryTxt() {
            return this.session.query_txt
        },
        fileHandleName() {
            return this.$typy(this.blobFile, 'file_handle.name').safeString
        },
        hasFileHandle() {
            return Boolean(this.fileHandleName)
        },
        isFileUnsaved() {
            return this.getIsFileUnsavedBySessionId(this.session.id)
        },
        isSaveFileDisabled() {
            return !this.isFileUnsaved || !this.hasFileHandle
        },
        isSaveFileAsDisabled() {
            return !this.isFileUnsaved
        },
        eventBus() {
            return EventBus
        },
    },
    activated() {
        this.eventBus.$on('shortkey', this.shortKeyHandler)
    },
    deactivated() {
        this.eventBus.$off('shortkey')
    },
    methods: {
        ...mapMutations({
            SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE',
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
            return this.getFileTextLegacy(fileHandle)
        },

        /**
         * @param {Blob} blob - blob
         */
        openConfDlg(blob) {
            this.confDlg = {
                ...this.confDlg,
                isOpened: true,
                title: this.$mxs_t('openScript'),
                confirmMsg: this.$mxs_t('confirmations.openScript', {
                    targetId: this.session.name,
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
            if (this.isFileUnsaved) this.openConfDlg(blob)
            else await this.loadFileToActiveSession(blob)
        },
        /**
         * @param {Blob} blob - blob
         */
        async loadFileToActiveSession(blob) {
            const blobTxt = await this.getFileTxt(blob.handle)
            this.UPDATE_SESSION({ ...this.session, name: blob.handle.name })
            this.SET_QUERY_TXT({ payload: blobTxt, id: this.session.id })
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
                id: this.session.id,
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
            if (this.hasFullSupport && this.hasFileHandle) await this.saveFile()
            else await this.handleSaveFileAs()
        },
        /*
         * Download the file to user's disk
         */
        saveFileLegacy() {
            let a = document.createElement('a')
            /** legacy download file name
             *  If there is no file_handle, use the current session tab name
             */
            const fileName = `${this.fileHandleName}` || `${this.session.name}.sql`

            a.href = `data:application/text;charset=utf-8;, ${encodeURIComponent(this.queryTxt)}`
            a.download = fileName
            document.body.appendChild(a)
            a.click()
            document.body.removeChild(a)
            // update blob_file
            this.SET_BLOB_FILE({
                payload: {
                    ...this.blobFile,
                    txt: this.queryTxt,
                },
                id: this.session.id,
            })
        },
        /*
         *Save new content for the opened file to user's local device
         */
        async saveFile() {
            try {
                const hasPriv = await this.verifyWritePriv(this.blobFile.file_handle)
                if (hasPriv) {
                    await this.writeFile({
                        fileHandle: this.blobFile.file_handle,
                        contents: this.queryTxt,
                    })
                    // update blob_file
                    this.SET_BLOB_FILE({
                        payload: {
                            file_handle: this.blobFile.file_handle,
                            txt: this.queryTxt,
                        },
                        id: this.session.id,
                    })
                }
            } catch (e) {
                this.$logger('load-sql-ctr-saveFile').error(e)
            }
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
        async saveFileAs() {
            let fileHandle = await this.getNewFileHandle(
                `${this.session.name}${this.hasFileHandle ? '' : '.sql'}`
            )
            try {
                await this.writeFile({ fileHandle, contents: this.queryTxt })
                // update blob_file
                this.SET_BLOB_FILE({
                    payload: { file_handle: fileHandle, txt: this.queryTxt },
                    id: this.session.id,
                })
                // update session tab name
                this.UPDATE_SESSION({ ...this.session, name: fileHandle.name })
            } catch (ex) {
                this.$logger('load-sql-ctr-saveFileAs').error('Unable to write file')
            }
        },
        async handleSaveFileAs() {
            this.hasFullSupport ? await this.saveFileAs() : this.saveFileLegacy()
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

        shortKeyHandler(key) {
            switch (key) {
                case 'ctrl-o':
                case 'mac-cmd-o':
                    this.handleFileOpen()
                    break
                case 'ctrl-s':
                case 'mac-cmd-s': {
                    if (!this.isSaveFileDisabled && this.hasFullSupport) this.saveFile()
                    break
                }
                case 'ctrl-shift-s':
                case 'mac-cmd-shift-s': {
                    if (!this.isSaveFileAsDisabled) this.handleSaveFileAs()
                    break
                }
            }
        },
    },
}
</script>
