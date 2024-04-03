<template>
    <div
        class="txt-editor-toolbar mxs-color-helper border-bottom-table-border d-flex align-center"
        :style="{ height: `${height}px` }"
    >
        <mxs-tooltip-btn
            :btnClass="['toolbar-square-btn', isExecuting ? 'stop-btn' : 'run-btn']"
            text
            color="primary"
            :disabled="isExecuting ? hasKillFlag : isRunBtnDisabled"
            :loading="hasKillFlag"
            @click="
                () =>
                    isExecuting
                        ? stopUserQuery()
                        : handleRun(selected_query_txt ? 'selected' : 'all')
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
            :depressed="isVisSidebarShown"
            :text="!isVisSidebarShown"
            :color="isVisSidebarShown ? 'primary' : 'primary'"
            :disabled="isVisBtnDisabled"
            @click="toggleVisSidebar"
        >
            <template v-slot:btn-content>
                <v-icon size="16"> $vuetify.icons.mxs_reports </v-icon>
            </template>
            {{
                $mxs_t('visualizedConfig', {
                    action: isVisSidebarShown ? $mxs_t('hide') : $mxs_t('show'),
                })
            }}
        </mxs-tooltip-btn>
        <mxs-tooltip-btn
            btnClass="create-snippet-btn toolbar-square-btn"
            text
            color="primary"
            :disabled="!queryTxt"
            @click="openSnippetDlg"
        >
            <template v-slot:btn-content>
                <v-icon size="19"> mdi-star-plus-outline </v-icon>
            </template>
            {{ $mxs_t('createQuerySnippet') }}
            <br />
            {{ OS_KEY }} + D
        </mxs-tooltip-btn>
        <file-btns-ctr :queryTab="queryTab" />
        <v-spacer />
        <mxs-tooltip-btn
            v-if="tab_moves_focus"
            btnClass="disable-tab-move-focus-mode-btn mr-1 text-capitalize"
            text
            color="primary"
            x-small
            @click="$emit('disable-tab-move-focus')"
        >
            <template v-slot:btn-content>
                {{ $mxs_t('tabMovesFocus') }}
            </template>
            {{ $mxs_t('disableAccessibilityMode') }}
            <br />
            {{ OS_KEY }} {{ IS_MAC_OS ? '+ SHIFT' : '' }} + M
        </mxs-tooltip-btn>
        <!-- QUERY_ROW_LIMIT dropdown input-->
        <v-form v-model="isRowLimitValid" class="fill-height d-flex align-center mr-3">
            <row-limit-ctr
                :style="{ maxWidth: '190px' }"
                :height="26"
                hide-details="auto"
                :prefix="$mxs_t('rowLimit')"
                @change="SET_QUERY_ROW_LIMIT($event)"
            />
        </v-form>
        <mxs-dlg
            v-model="confDlg.isOpened"
            :title="confDlg.title"
            :saveText="confDlg.type"
            minBodyWidth="768px"
            :closeImmediate="true"
            :lazyValidation="false"
            :onSave="confDlg.onSave"
        >
            <template v-slot:form-body>
                <div class="mb-4 readonly-sql-code-wrapper pa-2">
                    <mxs-sql-editor
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
        </mxs-dlg>
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/**
 * Emits
 * @disable-tab-move-focus : void
 */
import { mapMutations, mapState, mapActions } from 'vuex'
import TxtEditor from '@wsModels/TxtEditor'
import QueryResult from '@wsModels/QueryResult'
import RowLimitCtr from '@wkeComps/QueryEditor/RowLimitCtr.vue'
import FileBtnsCtr from '@wkeComps/QueryEditor/FileBtnsCtr.vue'
import { EventBus } from '@wkeComps/EventBus'
import { QUERY_MODES, OS_KEY, IS_MAC_OS } from '@wsSrc/constants'
import { splitQuery } from '@wsSrc/utils/queryUtils'

export default {
    name: 'txt-editor-toolbar-ctr',
    components: {
        RowLimitCtr,
        FileBtnsCtr,
    },
    props: {
        height: { type: Number, required: true },
        queryTab: { type: Object, required: true },
        queryTabTmp: { type: Object, required: true },
        queryTabConn: { type: Object, required: true },
        queryTxt: { type: String, required: true },
        isVisSidebarShown: { type: Boolean, required: true },
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
            query_confirm_flag: state => state.prefAndStorage.query_confirm_flag,
            query_snippets: state => state.prefAndStorage.query_snippets,
            is_max_rows_valid: state => state.editorsMem.is_max_rows_valid,
            selected_query_txt: state => state.editorsMem.selected_query_txt,
            tab_moves_focus: state => state.prefAndStorage.tab_moves_focus,
            max_statements: state => state.prefAndStorage.max_statements,
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
            return this.$typy(this.queryTabTmp, 'query_results.is_loading').safeBoolean
        },
        hasKillFlag() {
            return this.$typy(this.queryTabTmp, 'has_kill_flag').safeBoolean
        },
        isQueryTabConnBusy() {
            return this.$typy(this.queryTabConn, 'is_busy').safeBoolean
        },
        isRunBtnDisabled() {
            return (
                !this.queryTxt ||
                !this.queryTabConn.id ||
                this.isQueryTabConnBusy ||
                this.isExecuting
            )
        },
        isVisBtnDisabled() {
            return !this.queryTabConn.id || (this.isQueryTabConnBusy && this.isExecuting)
        },
        sqlTxt() {
            return this.activeRunMode === 'selected' ? this.selected_query_txt : this.queryTxt
        },
    },
    created() {
        this.IS_MAC_OS = IS_MAC_OS
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
        ...mapActions({ pushToQuerySnippets: 'prefAndStorage/pushToQuerySnippets' }),
        ...mapMutations({
            SET_QUERY_ROW_LIMIT: 'prefAndStorage/SET_QUERY_ROW_LIMIT',
            SET_QUERY_CONFIRM_FLAG: 'prefAndStorage/SET_QUERY_CONFIRM_FLAG',
            SET_IS_MAX_ROWS_VALID: 'editorsMem/SET_IS_MAX_ROWS_VALID',
            SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE',
        }),
        toggleVisSidebar() {
            TxtEditor.update({
                where: this.queryTab.id,
                data(obj) {
                    obj.is_vis_sidebar_shown = !obj.is_vis_sidebar_shown
                },
            })
        },
        /**
         * Only open dialog when its corresponding query text exists
         */
        shouldOpenDialog(mode) {
            return (
                (mode === 'selected' && this.selected_query_txt) ||
                (mode === 'all' && this.queryTxt)
            )
        },
        async handleRun(mode) {
            if (!this.isRunBtnDisabled)
                if (splitQuery(this.sqlTxt).length > this.max_statements)
                    this.SET_SNACK_BAR_MESSAGE({
                        text: [this.$mxs_t('errors.maxStatements', [this.max_statements])],
                        type: 'error',
                    })
                else {
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
                            sqlTxt: this.sqlTxt,
                            onSave: this.confirmRunning,
                        }
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
            QueryResult.update({
                where: this.queryTab.id,
                data: {
                    query_mode: QUERY_MODES.QUERY_VIEW,
                },
            })
            switch (mode) {
                case 'all':
                    if (this.queryTxt) await QueryResult.dispatch('fetchUserQuery', this.queryTxt)
                    break
                case 'selected':
                    if (this.selected_query_txt)
                        await QueryResult.dispatch('fetchUserQuery', this.selected_query_txt)
                    break
            }
        },
        openSnippetDlg() {
            if (this.queryTxt) {
                this.snippet.date = new Date().valueOf()
                this.snippet.name = ''
                this.confDlg = {
                    ...this.confDlg,
                    isOpened: true,
                    title: this.$mxs_t('confirmations.createSnippet'),
                    type: 'create',
                    isCreatingSnippet: true,
                    sqlTxt: this.selected_query_txt ? this.selected_query_txt : this.queryTxt,
                    onSave: this.addSnippet,
                }
            }
        },
        addSnippet() {
            let payload = {
                sql: this.queryTxt,
                ...this.snippet,
            }
            if (this.selected_query_txt) payload.sql = this.selected_query_txt
            this.pushToQuerySnippets(payload)
        },
        validateSnippetName(v) {
            const names = this.query_snippets.map(q => q.name)
            if (!v) return this.$mxs_t('errors.requiredInput', { inputName: this.$mxs_t('prefix') })
            else if (names.includes(v)) return this.$mxs_t('errors.duplicatedValue')
            return true
        },
        async stopUserQuery() {
            await QueryResult.dispatch('stopUserQuery')
        },
        async shortKeyHandler(key) {
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
                    if (this.isExecuting) await this.stopUserQuery()
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
