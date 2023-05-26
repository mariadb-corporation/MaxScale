<template>
    <div
        class="txt-editor-toolbar mxs-color-helper border-bottom-table-border d-flex align-center"
        :style="{ height: `${height}px` }"
    >
        <mxs-tooltip-btn
            :btnClass="['toolbar-square-btn', isExecuting ? 'stop-btn' : 'run-btn']"
            text
            color="accent-dark"
            :disabled="isExecuting ? hasKillFlag : isRunBtnDisabled"
            :loading="hasKillFlag"
            @click="
                () =>
                    isExecuting ? stopQuery() : handleRun(selected_query_txt ? 'selected' : 'all')
            "
        >
            <template v-slot:btn-content>
                <v-icon size="16">
                    {{ `$vuetify.icons.mxs_${isExecuting ? 'stopped' : 'running'}` }}
                </v-icon>
            </template>
            <template v-if="isExecuting">
                {{ $mxs_t('stopStatements') }}
                <br />
                {{ OS_KEY }} + SHIFT + C
            </template>
            <template v-else>
                {{
                    $mxs_t('runStatements', {
                        quantity: selected_query_txt ? $mxs_t('selected') : $mxs_t('all'),
                    })
                }}
                <br />
                {{ OS_KEY }} {{ selected_query_txt ? '' : '+ SHIFT' }} + ENTER
            </template>
        </mxs-tooltip-btn>
        <mxs-tooltip-btn
            btnClass="visualize-btn toolbar-square-btn"
            :depressed="show_vis_sidebar"
            :text="!show_vis_sidebar"
            :color="show_vis_sidebar ? 'primary' : 'accent-dark'"
            :disabled="isVisBtnDisabled"
            @click="SET_SHOW_VIS_SIDEBAR({ payload: !show_vis_sidebar, id: session.id })"
        >
            <template v-slot:btn-content>
                <v-icon size="16"> $vuetify.icons.mxs_reports </v-icon>
            </template>
            {{
                $mxs_t('visualizedConfig', {
                    action: show_vis_sidebar ? $mxs_t('hide') : $mxs_t('show'),
                })
            }}
        </mxs-tooltip-btn>
        <mxs-tooltip-btn
            btnClass="create-snippet-btn toolbar-square-btn"
            text
            color="accent-dark"
            :disabled="!query_txt"
            @click="openSnippetDlg"
        >
            <template v-slot:btn-content>
                <v-icon size="19"> mdi-star-plus-outline </v-icon>
            </template>
            {{ $mxs_t('createQuerySnippet') }}
            <br />
            {{ OS_KEY }} + D
        </mxs-tooltip-btn>
        <file-btns-ctr :session="session" />
        <v-spacer />
        <mxs-tooltip-btn
            v-if="tab_moves_focus"
            btnClass="disable-tab-move-focus-mode-btn mr-1 text-capitalize"
            text
            color="accent-dark"
            x-small
            @click="$emit('disable-tab-move-focus')"
        >
            <template v-slot:btn-content>
                {{ $mxs_t('tabMovesFocus') }}
            </template>
            {{ $mxs_t('disableAccessibilityMode') }}
            <br />
            {{ OS_KEY }} {{ $helpers.isMAC() ? '+ SHIFT' : '' }} + M
        </mxs-tooltip-btn>
        <!-- QUERY_ROW_LIMIT dropdown input-->
        <v-form v-model="isRowLimitValid" class="fill-height d-flex align-center mr-3">
            <row-limit-ctr
                :style="{ maxWidth: '190px' }"
                :height="26"
                hide-details="auto"
                :prefix="$mxs_t('rowLimit')"
                @change="SET_QUERY_ROW_LIMIT($event)"
            >
                <template v-slot:prepend-inner>
                    <label class="field__label mxs-color-helper text-small-text">
                        {{ $mxs_t('rowLimit') }}
                    </label>
                </template>
            </row-limit-ctr>
        </v-form>
        <slot name="txt-editor-toolbar-right-slot" />
        <mxs-conf-dlg
            v-model="confDlg.isOpened"
            :title="confDlg.title"
            :saveText="confDlg.type"
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
                        {{ $mxs_t('prefix') }}
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
                    :label="$mxs_t('dontAskMeAgain')"
                    color="primary"
                    hide-details
                    dense
                />
                <v-spacer />
            </template>
        </mxs-conf-dlg>
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/**
 * Emits
 * @disable-tab-move-focus : void
 */
import { mapMutations, mapState, mapGetters, mapActions } from 'vuex'
import RowLimitCtr from './RowLimitCtr.vue'
import SqlEditor from './SqlEditor'
import FileBtnsCtr from './FileBtnsCtr.vue'
import { EventBus } from './EventBus'

export default {
    name: 'txt-editor-toolbar-ctr',
    components: {
        RowLimitCtr,
        'readonly-sql-editor': SqlEditor,
        FileBtnsCtr,
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
                title: this.$mxs_t('confirmations.runQuery'),
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
            OS_KEY: state => state.queryEditorConfig.config.OS_KEY,
            SQL_QUERY_MODES: state => state.queryEditorConfig.config.SQL_QUERY_MODES,
            query_confirm_flag: state => state.queryPersisted.query_confirm_flag,
            query_snippets: state => state.queryPersisted.query_snippets,
            is_max_rows_valid: state => state.queryResult.is_max_rows_valid,
            query_txt: state => state.editor.query_txt,
            active_sql_conn: state => state.queryConn.active_sql_conn,
            show_vis_sidebar: state => state.queryResult.show_vis_sidebar,
            selected_query_txt: state => state.editor.selected_query_txt,
            tab_moves_focus: state => state.queryPersisted.tab_moves_focus,
        }),
        ...mapGetters({
            getLoadingQueryResultBySessionId: 'queryResult/getLoadingQueryResultBySessionId',
            getHasKillFlagMapBySessionId: 'queryResult/getHasKillFlagMapBySessionId',
            getIsRunBtnDisabledBySessionId: 'queryResult/getIsRunBtnDisabledBySessionId',
            getIsVisBtnDisabledBySessionId: 'queryResult/getIsVisBtnDisabledBySessionId',
        }),
        eventBus() {
            return EventBus
        },
        isRowLimitValid: {
            get() {
                return this.is_max_rows_valid
            },
            set(v) {
                if (v) this.SET_IS_MAX_ROWS_VALID(v)
            },
        },
        isExecuting() {
            return this.getLoadingQueryResultBySessionId(this.session.id)
        },
        hasKillFlag() {
            return this.getHasKillFlagMapBySessionId(this.session.id)
        },
        isRunBtnDisabled() {
            return this.getIsRunBtnDisabledBySessionId(this.session.id)
        },
        isVisBtnDisabled() {
            return this.getIsVisBtnDisabledBySessionId(this.session.id)
        },
    },
    activated() {
        this.eventBus.$on('shortkey', this.shortKeyHandler)
    },
    deactivated() {
        this.eventBus.$off('shortkey')
    },
    methods: {
        ...mapActions({
            fetchQueryResult: 'queryResult/fetchQueryResult',
            pushToQuerySnippets: 'queryPersisted/pushToQuerySnippets',
            stopQuery: 'queryResult/stopQuery',
        }),
        ...mapMutations({
            SET_QUERY_ROW_LIMIT: 'queryPersisted/SET_QUERY_ROW_LIMIT',
            SET_CURR_QUERY_MODE: 'queryResult/SET_CURR_QUERY_MODE',
            SET_QUERY_CONFIRM_FLAG: 'queryPersisted/SET_QUERY_CONFIRM_FLAG',
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
            if (!this.isRunBtnDisabled)
                if (!this.query_confirm_flag) await this.onRun(mode)
                else if (this.shouldOpenDialog(mode)) {
                    this.activeRunMode = mode
                    this.dontShowConfirm = false // clear checkbox state
                    this.confDlg = {
                        ...this.confDlg,
                        isOpened: true,
                        title: this.$mxs_t('confirmations.runQuery'),
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
                id: this.session.id,
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
                    title: this.$mxs_t('confirmations.createSnippet'),
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
            if (!v) return this.$mxs_t('errors.requiredInput', { inputName: this.$mxs_t('prefix') })
            else if (names.includes(v))
                return this.$mxs_t('errors.duplicatedValue', { inputValue: v })
            return true
        },
        shortKeyHandler(key) {
            switch (key) {
                case 'ctrl-d':
                case 'mac-cmd-d':
                    this.openSnippetDlg()
                    break
                case 'ctrl-enter':
                case 'mac-cmd-enter':
                    this.handleRun('selected')
                    break
                case 'ctrl-shift-enter':
                case 'mac-cmd-shift-enter':
                    this.handleRun('all')
                    break
                case 'ctrl-shift-c':
                case 'mac-cmd-shift-c':
                    if (this.isExecuting) this.stopQuery()
            }
        },
    },
}
</script>
<style lang="scss">
.stop-btn.v-btn--loading .v-progress-circular {
    height: 16px !important;
    width: 16px !important;
}
</style>
