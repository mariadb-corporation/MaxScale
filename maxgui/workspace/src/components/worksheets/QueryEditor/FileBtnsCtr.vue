<template>
    <div>
        <mxs-tooltip-btn
            btnClass="toolbar-square-btn load-sql-btn"
            text
            type="file"
            color="primary"
            @click="handleFileOpen"
        >
            <template v-slot:btn-content>
                <v-icon size="18">
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
            color="primary"
            @click="saveFileToDisk(queryTab)"
        >
            <template v-slot:btn-content>
                <v-icon size="20">mdi-content-save-outline</v-icon>
            </template>
            {{ $mxs_t('saveScript') }}
            <br />
            {{ OS_KEY }} + S
        </mxs-tooltip-btn>
        <mxs-tooltip-btn
            btnClass="toolbar-square-btn save-sql-btn"
            text
            :disabled="isSaveFileAsDisabled"
            color="primary"
            @click="handleSaveFileAs(queryTab)"
        >
            <template v-slot:btn-content>
                <v-icon size="20">mdi-content-save-edit-outline</v-icon>
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
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import { mapState, mapMutations, mapGetters, mapActions } from 'vuex'
import QueryTab from '@wsModels/QueryTab'
import TxtEditor from '@wsModels/TxtEditor'
import { fileOpen } from 'browser-fs-access'
import { EventBus } from '@wkeComps/EventBus'
import saveFile from '@wsSrc/mixins/saveFile'
import { OS_KEY } from '@wsSrc/constants'

export default {
    name: 'file-btns-ctr',
    mixins: [saveFile],
    props: { queryTab: { type: Object, required: true } },
    computed: {
        ...mapState({ confirm_dlg: state => state.mxsWorkspace.confirm_dlg }),
        ...mapGetters({
            hasFileSystemReadOnlyAccess: 'fileSysAccess/hasFileSystemReadOnlyAccess',
            hasFileSystemRWAccess: 'fileSysAccess/hasFileSystemRWAccess',
            getIsFileHandleValid: 'fileSysAccess/getIsFileHandleValid',
            getIsQueryTabUnsaved: 'fileSysAccess/getIsQueryTabUnsaved',
        }),
        isSaveFileDisabled() {
            return (
                !this.getIsQueryTabUnsaved(this.queryTab.id) ||
                !this.getIsFileHandleValid(this.queryTab.id)
            )
        },
        isSaveFileAsDisabled() {
            return !this.$typy(TxtEditor.find(this.queryTab.id), 'query_txt').safeString
        },
        eventBus() {
            return EventBus
        },
    },
    created() {
        this.OS_KEY = OS_KEY
    },
    activated() {
        this.eventBus.$on('workspace-shortkey', this.shortKeyHandler)
    },
    deactivated() {
        this.eventBus.$off('workspace-shortkey')
    },
    beforeDestroy() {
        this.eventBus.$off('workspace-shortkey')
    },
    methods: {
        ...mapActions({ updateFileHandleDataMap: 'fileSysAccess/updateFileHandleDataMap' }),
        ...mapMutations({
            SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE',
            SET_CONFIRM_DLG: 'mxsWorkspace/SET_CONFIRM_DLG',
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
            if (this.getIsQueryTabUnsaved(this.queryTab.id)) {
                this.SET_CONFIRM_DLG({
                    ...this.confirm_dlg,
                    is_opened: true,
                    title: this.$mxs_t('openScript'),
                    confirm_msg: this.$mxs_t('confirmations.openScript', {
                        targetId: this.queryTab.name,
                        fileNameToBeOpened: blob.handle.name,
                    }),
                    on_save: async () => {
                        await this.handleSaveFile(this.queryTab)
                        await this.loadFileToActiveQueryTab(blob)
                    },
                    after_cancel: async () => {
                        await this.loadFileToActiveQueryTab(blob)
                        this.SET_CONFIRM_DLG({ ...this.confirm_dlg, is_opened: false })
                    },
                })
            } else await this.loadFileToActiveQueryTab(blob)
        },
        /**
         * @param {Blob} blob - blob
         */
        async loadFileToActiveQueryTab(blob) {
            const blobTxt = await this.getFileTxt(blob.handle)
            QueryTab.update({ where: this.queryTab.id, data: { name: blob.handle.name } })
            if (!this.hasFileSystemReadOnlyAccess)
                /**
                 * clear the uploader file input so that if the user upload the same file,
                 * onFileLoadChanged event handler can be triggered again to show the dialog
                 */
                this.$refs.uploader.value = ''
            // once file is loaded, store it
            await this.updateFileHandleDataMap({
                id: this.queryTab.id,
                data: {
                    file_handle: blob.handle,
                    /* store its txt so it can be retrieved
                     * because the permission to read the file is withdrawn
                     * when the browser is refreshed or closed
                     */
                    txt: blobTxt,
                },
            })
            TxtEditor.update({ where: this.queryTab.id, data: { query_txt: blobTxt } })
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
                        this.saveFileToDisk(this.queryTab)
                    break
                }
                case 'ctrl-shift-s':
                case 'mac-cmd-shift-s': {
                    if (!this.isSaveFileAsDisabled) this.handleSaveFileAs(this.queryTab)
                    break
                }
            }
        },
    },
}
</script>
