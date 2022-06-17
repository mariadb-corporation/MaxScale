<template>
    <div class="session-toolbar d-flex align-center">
        <div
            v-for="session in query_sessions"
            v-show="session.id === getActiveSessionId && isTxtEditor"
            :key="`${session.id}`"
        >
            <session-btns
                :session="session"
                @on-stop-query="stopQuery"
                @on-run="handleRun(selected_query_txt ? 'selected' : 'all')"
                @on-visualize="
                    SET_SHOW_VIS_SIDEBAR({
                        payload: !show_vis_sidebar,
                        id: getActiveSessionId,
                    })
                "
            />
        </div>
        <portal-target v-if="isDDLEditor" name="alter-table-btns" class="fill-height" />
        <template v-if="isTxtEditor">
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        class="create-snippet-btn session-toolbar-square-btn"
                        text
                        color="accent-dark"
                        :disabled="!query_txt"
                        v-on="on"
                        @click="openSnippetDlg"
                    >
                        <v-icon size="19"> mdi-star-plus-outline </v-icon>
                    </v-btn>
                </template>
                <span style="white-space: pre;" class="d-inline-block text-center">
                    {{ `${$t('createQuerySnippet')}\nCmd/Ctrl + D` }}
                </span>
            </v-tooltip>
            <load-sql ref="loadSql" />
            <v-spacer />
            <v-form v-model="isMaxRowsValid" class="fill-height d-flex align-center mr-3">
                <max-rows-input
                    :style="{ maxWidth: '180px' }"
                    :height="26"
                    hide-details="auto"
                    :hasFieldsetBorder="false"
                    @change="SET_QUERY_MAX_ROW($event)"
                >
                    <template v-slot:prepend-inner>
                        <label class="field__label color text-small-text">
                            {{ $t('maxRows') }}
                        </label>
                    </template>
                </max-rows-input>
            </v-form>
        </template>
        <confirm-dialog
            v-model="confDlg.isOpened"
            :title="confDlg.title"
            :type="confDlg.type"
            minBodyWidth="768px"
            :closeImmediate="true"
            :lazyValidation="false"
            :onSave="confDlg.onSave"
        >
            <template v-slot:body-prepend>
                <div class="mb-4 readonly-sql-code-wrapper pa-2">
                    <readonly-query-editor
                        :value="confDlg.sqlTxt"
                        class="readonly-editor fill-height"
                        readOnly
                        :options="{
                            fontSize: 10,
                            contextmenu: false,
                        }"
                    />
                </div>
                <template v-if="confDlg.isCreatingSnippet">
                    <label
                        class="field__label color text-small-text label-required text-capitalize"
                    >
                        {{ $t('prefix') }}
                    </label>
                    <v-text-field
                        v-model="snippet.name"
                        type="text"
                        :rules="rules.snippetName"
                        class="std error--text__bottom mb-2"
                        dense
                        :height="36"
                        hide-details="auto"
                        outlined
                        required
                    />
                </template>
            </template>
            <template v-if="!confDlg.isCreatingSnippet" v-slot:action-prepend>
                <v-checkbox
                    v-model="dontShowConfirm"
                    class="pa-0 ma-0"
                    :label="$t('dontAskMeAgain')"
                    color="primary"
                    hide-details
                />
                <v-spacer />
            </template>
        </confirm-dialog>
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-04-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapMutations, mapState, mapGetters, mapActions } from 'vuex'
import MaxRowsInput from './MaxRowsInput.vue'
import SessionBtns from './SessionBtns'
import QueryEditor from '@/components/QueryEditor'
import LoadSql from './LoadSql'

export default {
    name: 'session-toolbar',
    components: {
        'max-rows-input': MaxRowsInput,
        'session-btns': SessionBtns,
        'readonly-query-editor': QueryEditor,
        'load-sql': LoadSql,
    },
    data() {
        return {
            dontShowConfirm: false,
            activeRunMode: 'all',
            confDlg: {
                isOpened: false,
                title: this.$t('confirmations.runQuery'),
                type: 'run',
                sqlTxt: '',
                isCreatingSnippet: false,
                onSave: () => null,
            },
            snippet: { date: '', name: '' },
            rules: {
                snippetName: [val => this.validateSnippetName(val)],
            },
        }
    },
    computed: {
        ...mapState({
            query_sessions: state => state.querySession.query_sessions,
            show_vis_sidebar: state => state.queryResult.show_vis_sidebar,
            query_confirm_flag: state => state.persisted.query_confirm_flag,
            query_txt: state => state.editor.query_txt,
            selected_query_txt: state => state.editor.selected_query_txt,
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            is_max_rows_valid: state => state.queryResult.is_max_rows_valid,
            SQL_EDITOR_MODES: state => state.app_config.SQL_EDITOR_MODES,
            query_favorite: state => state.persisted.query_favorite,
        }),
        ...mapGetters({
            getActiveSessionId: 'querySession/getActiveSessionId',
            getShouldDisableExecuteMap: 'queryResult/getShouldDisableExecuteMap',
            getCurrEditorMode: 'editor/getCurrEditorMode',
        }),
        isMaxRowsValid: {
            get() {
                return this.is_max_rows_valid
            },
            set(v) {
                if (v) this.SET_IS_MAX_ROWS_VALID(v)
            },
        },
        isTxtEditor() {
            return this.getCurrEditorMode === this.SQL_EDITOR_MODES.TXT_EDITOR
        },
        isDDLEditor() {
            return this.getCurrEditorMode === this.SQL_EDITOR_MODES.DDL_EDITOR
        },
    },
    methods: {
        ...mapActions({
            stopQuery: 'queryResult/stopQuery',
        }),
        ...mapActions({
            fetchQueryResult: 'queryResult/fetchQueryResult',
            pushToQuerySnippets: 'persisted/pushToQuerySnippets',
        }),
        ...mapMutations({
            SET_QUERY_MAX_ROW: 'persisted/SET_QUERY_MAX_ROW',
            SET_CURR_QUERY_MODE: 'queryResult/SET_CURR_QUERY_MODE',
            SET_QUERY_CONFIRM_FLAG: 'persisted/SET_QUERY_CONFIRM_FLAG',
            SET_SHOW_VIS_SIDEBAR: 'queryResult/SET_SHOW_VIS_SIDEBAR',
            SET_IS_MAX_ROWS_VALID: 'queryResult/SET_IS_MAX_ROWS_VALID',
        }),
        /**
         * Only open dialog when its corresponding query text exists
         */
        shouldOpenDialog(mode) {
            return (
                (mode === 'selected' && this.selected_query_txt) ||
                (mode === 'all' && this.query_txt)
            )
        },
        async handleRun(mode) {
            if (!this.getShouldDisableExecuteMap[this.getActiveSessionId])
                if (!this.query_confirm_flag) await this.onRun(mode)
                else if (this.shouldOpenDialog(mode)) {
                    this.activeRunMode = mode
                    this.dontShowConfirm = false // clear checkbox state
                    this.confDlg = {
                        ...this.confDlg,
                        isOpened: true,
                        title: this.$t('confirmations.runQuery'),
                        type: 'run',
                        isCreatingSnippet: false,
                        sqlTxt:
                            this.activeRunMode === 'selected'
                                ? this.selected_query_txt
                                : this.query_txt,
                        onSave: this.confirmRunning,
                    }
                }
        },
        async confirmRunning() {
            if (this.dontShowConfirm) this.SET_QUERY_CONFIRM_FLAG(0)
            await this.onRun(this.activeRunMode)
        },
        /**
         * @param {String} mode Mode to execute query: All or selected
         */
        async onRun(mode) {
            this.SET_CURR_QUERY_MODE({
                payload: this.SQL_QUERY_MODES.QUERY_VIEW,
                id: this.getActiveSessionId,
            })
            switch (mode) {
                case 'all':
                    if (this.query_txt) await this.fetchQueryResult(this.query_txt)
                    break
                case 'selected':
                    if (this.selected_query_txt)
                        await this.fetchQueryResult(this.selected_query_txt)
                    break
            }
        },
        openSnippetDlg() {
            if (this.query_txt) {
                this.snippet.date = new Date().valueOf()
                this.snippet.name = ''
                this.confDlg = {
                    ...this.confDlg,
                    isOpened: true,
                    title: this.$t('confirmations.createSnippet'),
                    type: 'create',
                    isCreatingSnippet: true,
                    sqlTxt: this.selected_query_txt ? this.selected_query_txt : this.query_txt,
                    onSave: this.addSnippet,
                }
            }
        },
        addSnippet() {
            let payload = {
                sql: this.query_txt,
                ...this.snippet,
            }
            if (this.selected_query_txt) payload.sql = this.selected_query_txt
            this.pushToQuerySnippets(payload)
        },
        validateSnippetName(v) {
            const names = this.query_favorite.map(q => q.name)
            if (!v) return this.$t('errors.requiredInput', { inputName: this.$t('prefix') })
            else if (names.includes(v)) return this.$t('errors.duplicatedValue', { inputValue: v })
            return true
        },
    },
}
</script>
<style lang="scss" scoped>
.session-toolbar {
    border-left: 1px solid $table-border;
    border-bottom: 1px solid $table-border;
    width: 100%;
    height: 28px;
}
</style>
