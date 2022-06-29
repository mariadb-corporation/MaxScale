<template>
    <div>
        <confirm-dialog
            v-model="confDlg.isOpened"
            :title="confDlg.title"
            :type="confDlg.type"
            :item="confDlg.item"
            minBodyWidth="768px"
            :closeImmediate="true"
            :lazyValidation="false"
            :onSave="confDlg.onSave"
        >
            <template v-slot:form-body>
                <p
                    v-html="
                        $t(`confirmations.${confDlg.type}`, {
                            targetId: $typy(confDlg, 'item.id').safeString,
                            fileNameToBeOpened: confDlg.fileNameToBeOpened,
                        })
                    "
                />
            </template>
            <template v-slot:actions="{ cancel, save }">
                <v-spacer />
                <v-btn
                    small
                    height="36"
                    color="primary"
                    class="cancel font-weight-medium px-7 text-capitalize"
                    rounded
                    outlined
                    depressed
                    @click="cancel"
                >
                    {{ $t('cancel') }}
                </v-btn>
                <v-btn
                    small
                    height="36"
                    color="primary"
                    class="cancel font-weight-medium px-7 text-capitalize"
                    rounded
                    outlined
                    depressed
                    @click="confDlg.dontSave"
                >
                    {{ $t('dontSave') }}
                </v-btn>
                <v-btn
                    small
                    height="36"
                    color="primary"
                    class="save font-weight-medium px-7 text-capitalize"
                    rounded
                    outlined
                    depressed
                    @click="save"
                >
                    {{ $t('save') }}
                </v-btn>
            </template>
        </confirm-dialog>
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop color text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn
                    text
                    class="load-sql-btn session-toolbar-square-btn"
                    type="file"
                    v-on="on"
                    @click="supportFs ? openFile() : openFileLegacy()"
                >
                    <v-icon size="18" color="accent-dark">
                        {{ supportFs ? 'mdi-file-outline' : 'mdi-file-upload-outline' }}
                    </v-icon>
                    <input ref="uploader" class="d-none" type="file" @input="onFileLoadChanged" />
                </v-btn>
            </template>
            <span>{{ $t('openScript') }}</span>
        </v-tooltip>
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop color text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn
                    text
                    class="save-sql-btn session-toolbar-square-btn"
                    type="file"
                    :disabled="!getIsFileUnsaved"
                    v-on="on"
                    @click="supportFs ? saveFile() : saveFileLegacy()"
                >
                    <v-icon size="20" color="accent-dark">
                        {{
                            supportFs ? 'mdi-content-save-outline' : 'mdi-content-save-edit-outline'
                        }}
                    </v-icon>
                </v-btn>
            </template>
            <span>{{ $t('saveScript') }}</span>
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
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapGetters, mapMutations } from 'vuex'
import { fileOpen, supported } from 'browser-fs-access'

export default {
    name: 'load-sql',
    data() {
        return {
            confDlg: {
                isOpened: false,
                title: this.$t('openScript'),
                item: null,
                type: 'openScript',
                onSave: () => null,
                dontSave: () => null,
                fileNameToBeOpened: '',
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
    },

    methods: {
        ...mapMutations({
            SET_QUERY_TXT: 'editor/SET_QUERY_TXT',
            UPDATE_SESSION: 'querySession/UPDATE_SESSION',
            SET_SNACK_BAR_MESSAGE: 'SET_SNACK_BAR_MESSAGE',
            SET_BLOB_FILE: 'editor/SET_BLOB_FILE',
        }),
        /**
         * Legacy support for reading uploaded file
         * @param {FileSystemFileHandle} fileHandle File handle
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
         * @param {FileSystemFileHandle} fileHandle File handle.
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
                item: { id: this.getActiveSession.name },
                fileNameToBeOpened: blob.handle.name,
                onSave: async () => await this.onSave(blob),
                dontSave: async () => await this.dontSave(blob),
            }
        },

        // legacy upload file changed support
        async onFileLoadChanged(e) {
            const blob = { handle: e.target.files[0] }
            await this.handleLoadScript(blob)
        },
        async openFileLegacy() {
            await this.$refs.uploader.click()
        },
        async openFile() {
            const blob = await fileOpen({ description: 'Text files' })
            await this.handleLoadScript(blob)
        },

        /**
         * @param {Blob} blob - blob
         */
        async handleLoadScript(blob) {
            if (this.getIsFileUnsaved) this.openConfDlg(blob)
            else await this.loadScriptToActiveSession(blob)
        },
        /**
         * @param {Blob} blob - blob
         */
        async loadScriptToActiveSession(blob) {
            const blobTxt = await this.getFileTxt(blob.handle)
            this.SET_QUERY_TXT({ payload: blobTxt, id: this.getActiveSessionId })
            const sessionIdx = this.query_sessions.findIndex(s => s.id === this.getActiveSessionId)
            this.UPDATE_SESSION({
                idx: sessionIdx,
                session: {
                    ...this.$help.lodash.cloneDeep(this.getActiveSession),
                    name: blob.handle.name,
                },
            })
            if (!this.supportFs)
                /**
                 * clear the uploader file input so that if the user upload the same file,
                 * onFileLoadChanged event handler can be triggered again to show the dialog
                 */
                this.$refs.uploader.value = ''
            // once script is loaded, store file_handle to the session
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
            await this.loadScriptToActiveSession(blob)
            this.confDlg.isOpened = false
        },
        /**
         * @param {Blob} blob - blob
         */
        async onSave(blob) {
            if (this.supportFs) await this.saveFile()
            // load new blob
            await this.loadScriptToActiveSession(blob)
        },

        /*
         * Download the file to user's local device
         */
        async saveFileLegacy() {
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
                this.$logger('LoadSql-saveFile').error(e)
            }
        },

        /**
         * Writes the contents to disk.
         * @param {FileSystemFileHandle} param.fileHandle File handle to write to.
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
         * @param {FileSystemFileHandle} fileHandle File handle to check.
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
    },
}
</script>
