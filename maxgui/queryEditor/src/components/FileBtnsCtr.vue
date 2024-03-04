<template>
    <div>
        <mxs-tooltip-btn
            btnClass="toolbar-square-btn load-sql-btn"
            text
            type="file"
            @click="handleFileOpen"
        >
            <template v-slot:btn-content>
                <v-icon size="18" color="accent-dark">
                    {{
                        hasFileSystemReadOnlyAccess ? 'mdi-file-outline' : 'mdi-file-upload-outline'
                    }}
                </v-icon>
                <input ref="uploader" class="d-none" type="file" @input="onFileLoadChanged" />
            </template>
            {{ $mxs_t('openScript') }}
            <br />
            {{ OS_KEY }} + O
        </mxs-tooltip-btn>
        <mxs-tooltip-btn
            v-if="hasFileSystemRWAccess"
            btnClass="toolbar-square-btn save-sql-btn"
            text
            :disabled="isSaveFileDisabled"
            @click="saveFileToDisk(session)"
        >
            <template v-slot:btn-content>
                <v-icon size="20" color="accent-dark">mdi-content-save-outline</v-icon>
            </template>
            {{ $mxs_t('saveScript') }}
            <br />
            {{ OS_KEY }} + S
        </mxs-tooltip-btn>
        <mxs-tooltip-btn
            btnClass="toolbar-square-btn save-sql-btn"
            text
            :disabled="isSaveFileAsDisabled"
            @click="handleSaveFileAs(session)"
        >
            <template v-slot:btn-content>
                <v-icon size="20" color="accent-dark">mdi-content-save-edit-outline</v-icon>
            </template>
            {{ $mxs_t('saveScriptAs') }}
            <br />
            {{ OS_KEY }} + SHIFT + S
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
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapGetters, mapMutations } from 'vuex'
import { fileOpen } from 'browser-fs-access'
import { EventBus } from './EventBus'
import saveFile from '@queryEditorSrc/mixins/saveFile'
export default {
    name: 'file-btns-ctr',
    mixins: [saveFile],
    props: { session: { type: Object, required: true } },
    computed: {
        ...mapState({
            file_dlg_data: state => state.editor.file_dlg_data,
            OS_KEY: state => state.queryEditorConfig.config.OS_KEY,
        }),
        ...mapGetters({
            getIsFileUnsavedBySessionId: 'editor/getIsFileUnsavedBySessionId',
            hasFileSystemReadOnlyAccess: 'editor/hasFileSystemReadOnlyAccess',
            hasFileSystemRWAccess: 'editor/hasFileSystemRWAccess',
            checkSessFileHandleValidity: 'editor/checkSessFileHandleValidity',
        }),
        isFileUnsaved() {
            return this.getIsFileUnsavedBySessionId(this.session.id)
        },
        isSaveFileDisabled() {
            return !this.isFileUnsaved || !this.checkSessFileHandleValidity(this.session)
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
            SET_FILE_DLG_DATA: 'editor/SET_FILE_DLG_DATA',
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
            if (this.hasFileSystemReadOnlyAccess) {
                // get file contents
                const file = await fileHandle.getFile()
                return await file.text()
            }
            return this.getFileTextLegacy(fileHandle)
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
            if (this.hasFileSystemReadOnlyAccess) this.openFile()
            else this.openFileLegacy()
        },
        /**
         * @param {Blob} blob - blob
         */
        async handleLoadFile(blob) {
            if (this.isFileUnsaved) {
                this.SET_FILE_DLG_DATA({
                    is_opened: true,
                    title: this.$mxs_t('openScript'),
                    confirm_msg: this.$mxs_t('confirmations.openScript', {
                        targetId: this.session.name,
                        fileNameToBeOpened: blob.handle.name,
                    }),
                    on_save: async () => {
                        await this.handleSaveFile(this.session)
                        await this.loadFileToActiveSession(blob)
                    },
                    dont_save: async () => {
                        await this.loadFileToActiveSession(blob)
                        this.SET_FILE_DLG_DATA({ ...this.file_dlg_data, is_opened: false })
                    },
                })
            } else await this.loadFileToActiveSession(blob)
        },
        /**
         * @param {Blob} blob - blob
         */
        async loadFileToActiveSession(blob) {
            const blobTxt = await this.getFileTxt(blob.handle)
            this.UPDATE_SESSION({ ...this.session, name: blob.handle.name })
            this.SET_QUERY_TXT({ payload: blobTxt, id: this.session.id })
            if (!this.hasFileSystemReadOnlyAccess)
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

        shortKeyHandler(key) {
            switch (key) {
                case 'ctrl-o':
                case 'mac-cmd-o':
                    this.handleFileOpen()
                    break
                case 'ctrl-s':
                case 'mac-cmd-s': {
                    if (!this.isSaveFileDisabled && this.hasFileSystemRWAccess)
                        this.saveFileToDisk(this.session)
                    break
                }
                case 'ctrl-shift-s':
                case 'mac-cmd-shift-s': {
                    if (!this.isSaveFileAsDisabled) this.handleSaveFileAs(this.session)
                    break
                }
            }
        },
    },
}
</script>
