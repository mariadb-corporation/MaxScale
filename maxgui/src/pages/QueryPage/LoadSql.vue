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
            <template v-slot:actions="{ cancel, save, close }">
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
                    class="save font-weight-medium px-7 text-capitalize"
                    rounded
                    outlined
                    depressed
                    @click="save"
                >
                    {{ $t(supportFs ? 'saveAndOpen' : 'load') }}
                </v-btn>
                <v-btn
                    small
                    height="36"
                    color="primary"
                    class="cancel font-weight-medium px-7 text-capitalize"
                    rounded
                    outlined
                    depressed
                    @click="handleLoadInANewTab(close)"
                >
                    {{ $t(supportFs ? 'openInANewTab' : 'loadInANewTab') }}
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
            <span>{{ supportFs ? $t('openScript') : $t('loadScript') }}</span>
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
import { mapState, mapGetters, mapMutations, mapActions } from 'vuex'
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
            },
            fileHandle: null,
            fileTxt: null,
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
        }),
        supportFs() {
            return supported
        },
    },

    methods: {
        ...mapActions({ handleAddNewSession: 'querySession/handleAddNewSession' }),
        ...mapMutations({
            SET_QUERY_TXT: 'editor/SET_QUERY_TXT',
            UPDATE_SESSION: 'querySession/UPDATE_SESSION',
            SET_SNACK_BAR_MESSAGE: 'SET_SNACK_BAR_MESSAGE',
        }),
        openConfDlg() {
            this.confDlg = {
                ...this.confDlg,
                isOpened: true,
                title: this.$t(this.supportFs ? 'openScript' : 'loadScript'),
                type: this.supportFs ? 'openScript' : 'loadScript',
                item: { id: this.getActiveSession.name },
                onSave: this.loadScriptToActiveSession,
            }
        },
        loadScriptToActiveSession() {
            this.SET_QUERY_TXT({ payload: this.fileTxt, id: this.getActiveSessionId })
            const sessionIdx = this.query_sessions.findIndex(s => s.id === this.getActiveSessionId)
            this.UPDATE_SESSION({
                idx: sessionIdx,
                session: {
                    ...this.$help.lodash.cloneDeep(this.getActiveSession),
                    name: this.fileHandle.name,
                },
            })
            if (!this.supportFs)
                /**
                 * clear the uploader file input so that if the user upload the same file,
                 * onFileLoadChanged event handler can be triggered again to show the dialog
                 */
                this.$refs.uploader.value = ''
        },
        /**
         * @param {Function} close - close dialog function
         */
        async handleLoadInANewTab(close) {
            await this.handleAddNewSession(this.active_wke_id)
            this.loadScriptToActiveSession()
            close()
        },
        async handleFileOpen() {
            if (!this.supportFs) this.handleFileUpload()
            const blob = await fileOpen({ description: 'Text files' })
            this.fileHandle = blob.handle
            // get file contents
            const file = await this.fileHandle.getFile()
            this.fileTxt = await file.text()
            /* TODO: detect unsaved changes, if it's saved or if there is no file opened yet,
             * call loadScriptToActiveSession
             */
            this.openConfDlg()
        },
        // legacy upload support
        handleFileUpload() {
            this.isSelecting = true
            window.addEventListener('focus', () => (this.isSelecting = false), { once: true })
            this.$refs.uploader.click()
        },
        // legacy upload file changed support
        onFileLoadChanged(e) {
            const reader = new FileReader()
            this.fileHandle = e.target.files[0]
            reader.readAsText(this.fileHandle)
            reader.onload = () => {
                this.fileTxt = reader.result
                this.openConfDlg()
            }
            reader.onerror = () =>
                this.SET_SNACK_BAR_MESSAGE({ text: [reader.error], type: 'error' })
        },
    },
}
</script>
