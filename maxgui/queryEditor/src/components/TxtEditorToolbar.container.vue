<template>
    <div
        class="txt-editor-toolbar mxs-color-helper border-bottom-table-border d-flex align-center"
        :style="{ height: `${height}px` }"
    >
        <!-- Run/Stop buttons-->
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop mxs-color-helper text-navigation py-1 px-4"
            :disabled="getLoadingQueryResultBySessionId(session.id)"
        >
            <template v-slot:activator="{ on }">
                <!-- disable button Prevent parallel querying of the same connection -->
                <v-btn
                    class="toolbar-square-btn"
                    :class="[getLoadingQueryResultBySessionId(session.id) ? 'stop-btn' : 'run-btn']"
                    text
                    color="accent-dark"
                    :disabled="
                        getLoadingQueryResultBySessionId(session.id)
                            ? false
                            : getIsRunBtnDisabledBySessionId(session.id)
                    "
                    v-on="on"
                    @click="
                        () =>
                            getLoadingQueryResultBySessionId(session.id)
                                ? stopQuery()
                                : handleRun(selected_query_txt ? 'selected' : 'all')
                    "
                >
                    <v-icon size="16">
                        {{
                            `$vuetify.icons.${
                                getLoadingQueryResultBySessionId(session.id) ? 'stopped' : 'running'
                            }`
                        }}
                    </v-icon>
                </v-btn>
            </template>
            <span style="white-space: pre;" class="d-inline-block text-center">
                {{
                    selected_query_txt
                        ? `${$t('runStatements', {
                              quantity: $t('selected'),
                          })}\nCmd/Ctrl + Enter`
                        : `${$t('runStatements', {
                              quantity: $t('all'),
                          })}\nCmd/Ctrl + Shift + Enter`
                }}
            </span>
        </v-tooltip>
        <!-- Visualize button-->
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop mxs-color-helper text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn
                    class="toolbar-square-btn visualize-btn"
                    :depressed="show_vis_sidebar"
                    :text="!show_vis_sidebar"
                    :color="show_vis_sidebar ? 'primary' : 'accent-dark'"
                    :disabled="getIsVisBtnDisabledBySessionId(session.id)"
                    v-on="on"
                    @click="
                        SET_SHOW_VIS_SIDEBAR({ payload: !show_vis_sidebar, id: getActiveSessionId })
                    "
                >
                    <v-icon size="16"> $vuetify.icons.reports </v-icon>
                </v-btn>
            </template>
            <span class="text-capitalize">
                {{
                    $t('visualizedConfig', {
                        action: show_vis_sidebar ? $t('hide') : $t('show'),
                    })
                }}
            </span>
        </v-tooltip>
        <!-- Create snippet button-->
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop mxs-color-helper text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn
                    class="create-snippet-btn toolbar-square-btn"
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
        <load-sql-ctr ref="loadSqlCtr" />
        <v-spacer />
        <!-- QUERY_ROW_LIMIT dropdown input-->
        <v-form v-model="isRowLimitValid" class="fill-height d-flex align-center mr-3">
            <row-limit-ctr
                :style="{ maxWidth: '190px' }"
                :height="26"
                hide-details="auto"
                :prefix="$t('rowLimit')"
                @change="SET_QUERY_ROW_LIMIT($event)"
            >
                <template v-slot:prepend-inner>
                    <label class="field__label mxs-color-helper text-small-text">
                        {{ $t('rowLimit') }}
                    </label>
                </template>
            </row-limit-ctr>
        </v-form>

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
                    <readonly-sql-editor
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
                        class="field__label mxs-color-helper text-small-text label-required text-capitalize"
                    >
                        {{ $t('prefix') }}
                    </label>
                    <v-text-field
                        v-model="snippet.name"
                        type="text"
                        :rules="rules.snippetName"
                        class="vuetify-input--override error--text__bottom mb-2"
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
                    dense
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
 * Change Date: 2026-08-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapMutations, mapState, mapGetters, mapActions } from 'vuex'
import RowLimit from './RowLimit.container.vue'
import SqlEditor from './SqlEditor'
import LoadSql from './LoadSql.container.vue'

export default {
    name: 'txt-editor-toolbar-ctr',
    components: {
        'row-limit-ctr': RowLimit,
        'readonly-sql-editor': SqlEditor,
        'load-sql-ctr': LoadSql,
    },
    props: {
        session: { type: Object, required: true },
        height: { type: Number, required: true },
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
            SQL_QUERY_MODES: state => state.queryEditorConfig.config.SQL_QUERY_MODES,
            query_confirm_flag: state => state.queryPersisted.query_confirm_flag,
            query_snippets: state => state.queryPersisted.query_snippets,
            is_max_rows_valid: state => state.queryResult.is_max_rows_valid,
            query_txt: state => state.editor.query_txt,
            active_sql_conn: state => state.queryConn.active_sql_conn,
            show_vis_sidebar: state => state.queryResult.show_vis_sidebar,
            selected_query_txt: state => state.editor.selected_query_txt,
        }),
        ...mapGetters({
            getActiveSessionId: 'querySession/getActiveSessionId',
            getLoadingQueryResultBySessionId: 'queryResult/getLoadingQueryResultBySessionId',
            getIsStoppingQueryBySessionId: 'queryResult/getIsStoppingQueryBySessionId',
            getBgConn: 'queryConn/getBgConn',
            getIsRunBtnDisabledBySessionId: 'queryResult/getIsRunBtnDisabledBySessionId',
            getIsVisBtnDisabledBySessionId: 'queryResult/getIsVisBtnDisabledBySessionId',
        }),
        isQueryKilled() {
            return (
                !this.getLoadingQueryResultBySessionId(this.session.id) &&
                !this.getIsStoppingQueryBySessionId(this.session.id)
            )
        },
        isRowLimitValid: {
            get() {
                return this.is_max_rows_valid
            },
            set(v) {
                if (v) this.SET_IS_MAX_ROWS_VALID(v)
            },
        },
    },
    activated() {
        this.watch_isQueryKilled()
    },
    deactivated() {
        this.$typy(this.unwatch_isQueryKilled).safeFunction()
    },
    methods: {
        ...mapActions({
            fetchQueryResult: 'queryResult/fetchQueryResult',
            pushToQuerySnippets: 'queryPersisted/pushToQuerySnippets',
            stopQuery: 'queryResult/stopQuery',
            disconnectClone: 'queryConn/disconnectClone',
        }),
        ...mapMutations({
            SET_QUERY_ROW_LIMIT: 'queryPersisted/SET_QUERY_ROW_LIMIT',
            SET_CURR_QUERY_MODE: 'queryResult/SET_CURR_QUERY_MODE',
            SET_QUERY_CONFIRM_FLAG: 'queryPersisted/SET_QUERY_CONFIRM_FLAG',
            SET_SHOW_VIS_SIDEBAR: 'queryResult/SET_SHOW_VIS_SIDEBAR',
            SET_IS_MAX_ROWS_VALID: 'queryResult/SET_IS_MAX_ROWS_VALID',
        }),
        watch_isQueryKilled() {
            this.unwatch_isQueryKilled = this.$watch('isQueryKilled', async (v, oV) => {
                if (v !== oV && v) {
                    const bgConn = this.getBgConn({ session_id_fk: this.session.id })
                    if (bgConn.id) await this.disconnectClone({ id: bgConn.id })
                }
            })
        },
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
            if (!this.getIsRunBtnDisabledBySessionId[this.getActiveSessionId])
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
            const names = this.query_snippets.map(q => q.name)
            if (!v) return this.$t('errors.requiredInput', { inputName: this.$t('prefix') })
            else if (names.includes(v)) return this.$t('errors.duplicatedValue', { inputValue: v })
            return true
        },
    },
}
</script>
