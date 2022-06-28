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
                    :loading="isSelecting"
                    v-on="on"
                    @click="handleFileOpen"
                >
                    <v-icon size="18" color="accent-dark">
                        {{ supportFs ? 'mdi-file-outline' : 'mdi-file-upload-outline' }}
                    </v-icon>
                    <input ref="uploader" class="d-none" type="file" @input="onFileLoadChanged" />
                </v-btn>
            </template>
            <span>{{ $t('openScript') }}</span>
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
            isSelecting: false,
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
            SET_FILE_HANDLE: 'editor/SET_FILE_HANDLE',
        }),
        // legacy support for reading uploaded file
        readUploadedFileAsText(fileHandle) {
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

        async getFileTxt(fileHandle) {
            if (this.supportFs) {
                // get file contents
                const file = await fileHandle.getFile()
                return await file.text()
            }
            return await this.readUploadedFileAsText(fileHandle)
        },

        async dontSave({ fileHandle }) {
            await this.loadScriptToActiveSession({ fileHandle })
            this.confDlg.isOpened = false
        },

        async onSave({ fileHandle }) {
            /* TODO: Handle saving file to user's local device if supportFs or downloading it
             * before calling loadScriptToActiveSession
             */
            await this.loadScriptToActiveSession({ fileHandle })
        },

        async loadScriptToActiveSession({ fileHandle }) {
            this.SET_QUERY_TXT({
                payload: await this.getFileTxt(fileHandle),
                id: this.getActiveSessionId,
            })
            const sessionIdx = this.query_sessions.findIndex(s => s.id === this.getActiveSessionId)
            this.UPDATE_SESSION({
                idx: sessionIdx,
                session: {
                    ...this.$help.lodash.cloneDeep(this.getActiveSession),
                    name: fileHandle.name,
                },
            })
            if (!this.supportFs)
                /**
                 * clear the uploader file input so that if the user upload the same file,
                 * onFileLoadChanged event handler can be triggered again to show the dialog
                 */
                this.$refs.uploader.value = ''
            // once script is loaded, store fileHandle to the session
            this.SET_FILE_HANDLE({
                payload: {
                    file: fileHandle,
                    /* store its txt so that it can be retrieved
                     * because the permission to read the file is withdrawn
                     * when the browser is refreshed or closed
                     */
                    txt: await this.getFileTxt(fileHandle),
                },
                id: this.getActiveSessionId,
            })
        },

        openConfDlg({ fileHandle }) {
            this.confDlg = {
                ...this.confDlg,
                isOpened: true,
                title: this.$t('openScript'),
                type: 'openScript',
                item: { id: this.getActiveSession.name },
                fileNameToBeOpened: fileHandle.name,
                onSave: async () => await this.onSave({ fileHandle }),
                dontSave: async () => await this.dontSave({ fileHandle }),
            }
        },

        async handleLoadScript(fileHandle) {
            if (this.getIsFileUnsaved) this.openConfDlg({ fileHandle })
            else await this.loadScriptToActiveSession({ fileHandle })
        },

        async handleFileOpen() {
            if (this.supportFs) {
                const blob = await fileOpen({ description: 'Text files' })
                const fileHandle = blob.handle
                await this.handleLoadScript(fileHandle)
            } else {
                this.isSelecting = true
                window.addEventListener('focus', () => (this.isSelecting = false), { once: true })
                this.$refs.uploader.click()
            }
        },

        // legacy upload file changed support
        async onFileLoadChanged(e) {
            await this.handleLoadScript(e.target.files[0])
        },
    },
}
</script>
