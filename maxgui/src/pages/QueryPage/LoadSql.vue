<template>
    <div>
        <confirm-dialog
            v-model="confDlg.isOpened"
            :title="confDlg.title"
            :type="confDlg.type"
            :item="confDlg.item"
            saveText="override"
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
                    {{ $t('override') }}
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
                    {{ $t('loadInANewTab') }}
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
                    @click="handleFileImport"
                >
                    <v-icon size="18" color="accent-dark">
                        mdi-file-upload-outline
                    </v-icon>
                    <input ref="uploader" class="d-none" type="file" @input="onFileChanged" />
                </v-btn>
            </template>
            <span>{{ $t('loadScript') }}</span>
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
export default {
    name: 'load-sql',
    data() {
        return {
            isSelecting: false,
            confDlg: {
                isOpened: false,
                title: this.$t('loadScript'),
                item: null,
                type: 'loadScript',
                onSave: () => null,
            },
            reader: null,
            file: null,
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
    },
    methods: {
        ...mapActions({ handleAddNewSession: 'querySession/handleAddNewSession' }),
        ...mapMutations({
            SET_QUERY_TXT: 'editor/SET_QUERY_TXT',
            UPDATE_SESSION: 'querySession/UPDATE_SESSION',
            SET_SNACK_BAR_MESSAGE: 'SET_SNACK_BAR_MESSAGE',
        }),
        handleFileImport() {
            this.isSelecting = true
            window.addEventListener('focus', () => (this.isSelecting = false), { once: true })
            this.$refs.uploader.click()
        },
        onFileChanged(e) {
            this.reader = new FileReader()
            this.file = e.target.files[0]
            this.reader.readAsText(this.file)
            this.reader.onload = () => {
                this.confDlg = {
                    ...this.confDlg,
                    isOpened: true,
                    item: { id: this.getActiveSession.name },
                    onSave: this.loadScriptToActiveSession,
                }
            }
            this.reader.onerror = () =>
                this.SET_SNACK_BAR_MESSAGE({ text: [this.reader.error], type: 'error' })
        },
        loadScriptToActiveSession() {
            this.SET_QUERY_TXT({ payload: this.reader.result, id: this.getActiveSessionId })
            const sessionIdx = this.query_sessions.findIndex(s => s.id === this.getActiveSessionId)
            this.UPDATE_SESSION({
                idx: sessionIdx,
                session: {
                    ...this.$help.lodash.cloneDeep(this.getActiveSession),
                    name: this.file.name,
                },
            })
            /**
             * clear the uploader file input so that if the user upload the same file,
             * onFileChanged event handler can be triggered again to show the dialog
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
    },
}
</script>
