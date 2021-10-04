<template>
    <v-card
        v-if="getLoadingTblCreationInfo || !initialTblOptsData"
        class="fill-height color border-top-table-border border-right-table-border border-bottom-table-border"
        :loading="Boolean(getLoadingTblCreationInfo && !initialTblOptsData)"
    />
    <div
        v-else
        class="relative fill-height color border-top-table-border border-right-table-border border-bottom-table-border"
    >
        <div class="pt-2 pl-3 pr-2 d-flex align-center justify-space-between">
            <span class="text-body-2 color text-navigation font-weight-bold text-uppercase">
                {{ $t('alterTbl') }}
            </span>
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn icon small v-on="on" @click="closeDDLEditor">
                        <v-icon size="12" color="navigation"> $vuetify.icons.close</v-icon>
                    </v-btn>
                </template>
                <span>{{ $t('closeDDLEditor') }}</span>
            </v-tooltip>
        </div>
        <portal to="wke-toolbar-right">
            <alter-table-toolbar
                :disableRevert="!hasChanged"
                :disableApply="!hasValidChanges"
                @on-revert="revertChanges"
                @on-apply="applyChanges"
            />
        </portal>
        <v-form v-model="isFormValid">
            <alter-table-opts v-model="tableOptsData" />
            <v-tabs v-model="activeColSpec" :height="24" class="tab-navigation-wrapper">
                <v-tab color="primary" :href="`#${SQL_DDL_ALTER_SPECS.COLUMNS}`">
                    <span> {{ $t('columns') }} </span>
                </v-tab>
                <v-tab color="primary" :href="`#${SQL_DDL_ALTER_SPECS.TRIGGERS}`">
                    <span>{{ $t('triggers') }} </span>
                </v-tab>
                <v-tabs-items v-model="activeColSpec">
                    <v-tab-item
                        v-for="spec in SQL_DDL_ALTER_SPECS"
                        :id="spec"
                        :key="spec"
                        class="pt-2"
                    >
                        <div v-if="activeColSpec === spec" class="px-4 py-2">
                            <!-- TODO: Replace with columns/triggers input specs -->
                            {{ spec }} input specs here
                        </div>
                    </v-tab-item>
                </v-tabs-items>
            </v-tabs>
        </v-form>

        <confirm-dialog
            ref="confirmAlterDialog"
            :title="isErrDialogShown ? $t('errors.alterFailed') : $t('confirmations.alterTable')"
            :smallInfo="isErrDialogShown ? '' : $t('info.alterTableInfo')"
            type="execute"
            :onSave="confirmAlter"
            minBodyWidth="768px"
            :hasSavingErr="isAlterFailed"
            :onCancel="clearAlterResult"
            :onClose="clearAlterResult"
        >
            <template v-slot:body-prepend>
                <table v-if="isErrDialogShown" class="alter-err-tbl pa-4">
                    <tr>
                        <td><b>sql</b></td>
                        <td>
                            {{ alterSql }}
                        </td>
                    </tr>
                    <tr v-for="(v, key) in alterResult" :key="key">
                        <td>
                            <b>{{ key }}</b>
                        </td>
                        <td>
                            {{ v }}
                        </td>
                    </tr>
                </table>

                <div
                    v-else
                    class="mb-4 pt-2 pl-2 color border-all-table-border"
                    style="height:250px"
                >
                    <query-editor
                        v-if="sql"
                        v-model="sql"
                        :class="`fill-height`"
                        :cmplList="getDbCmplList"
                        :options="{
                            fontSize: 10,
                            contextmenu: false,
                        }"
                    />
                </div>
            </template>
        </confirm-dialog>
    </div>
</template>

<script>
import { mapActions, mapGetters, mapMutations, mapState } from 'vuex'
import AlterTableOpts from './AlterTableOpts.vue'
import AlterTableToolbar from './AlterTableToolbar.vue'
import QueryEditor from '@/components/QueryEditor'
export default {
    name: 'ddl-editor',
    components: {
        'alter-table-opts': AlterTableOpts,
        'alter-table-toolbar': AlterTableToolbar,
        'query-editor': QueryEditor,
    },
    data() {
        return {
            isFormValid: true,
            tableOptsData: {},
            sql: '',
            /**
             * Using isErrDialogShown instead of isAlterFailed because
             * when closing the dialog with error message, the action to clear
             * `isAlterFailed` is dispatched immediately but dialog is still in the transition
             * of closing. As a result, the query-editor is rendered while closing. This
             * state helps to keep error dialog content even when dialog is closed.
             *
             */
            isErrDialogShown: false,
        }
    },
    computed: {
        ...mapState({
            SQL_DDL_ALTER_SPECS: state => state.app_config.SQL_DDL_ALTER_SPECS,
            SQL_EDITOR_MODES: state => state.app_config.SQL_EDITOR_MODES,
            curr_ddl_alter_spec: state => state.query.curr_ddl_alter_spec,
            active_wke_id: state => state.query.active_wke_id,
        }),
        ...mapGetters({
            getLoadingTblCreationInfo: 'query/getLoadingTblCreationInfo',
            getTblCreationInfo: 'query/getTblCreationInfo',
            getDbCmplList: 'query/getDbCmplList',
            getAlteringTableResultMap: 'query/getAlteringTableResultMap',
        }),
        activeColSpec: {
            get() {
                return this.curr_ddl_alter_spec
            },
            set(value) {
                this.SET_CURR_DDL_COL_SPEC(value)
            },
        },
        /**
         * TODO: Add more inputs to initialData and newData
         * All inputs will be stored in an object,
         * for now only table option inputs (tableOptsData) here.
         */
        initialData() {
            return { ...this.initialTblOptsData }
        },
        newData() {
            return { ...this.tableOptsData }
        },
        initialTblOptsData() {
            return this.$typy(this.getTblCreationInfo, 'table_opts_data').safeObject
        },
        hasChanged() {
            return !this.$help.lodash.isEqual(this.initialData, this.newData)
        },
        hasValidChanges() {
            return this.isFormValid && this.hasChanged
        },
        notAlteredYet() {
            return Boolean(this.$typy(this.getAlteringTableResultMap).isEmptyObject)
        },
        alterResult() {
            return this.$typy(this.getAlteringTableResultMap, 'data.results[0]').safeObject
        },
        isAlterFailed() {
            if (this.notAlteredYet) return false
            return Boolean(this.$typy(this.alterResult, 'errno').safeObject)
        },
        alterSql() {
            return this.$typy(this.getAlteringTableResultMap, 'data.sql').safeString
        },
    },
    watch: {
        initialTblOptsData: {
            deep: true,
            handler(v) {
                this.tableOptsData = this.$help.lodash.cloneDeep(v)
            },
        },
    },
    methods: {
        ...mapMutations({
            SET_CURR_DDL_COL_SPEC: 'query/SET_CURR_DDL_COL_SPEC',
            SET_CURR_EDITOR_MODE: 'query/SET_CURR_EDITOR_MODE',
            UPDATE_TBL_CREATION_INFO_MAP: 'query/UPDATE_TBL_CREATION_INFO_MAP',
            UPDATE_ALTERING_TABLE_RESULT_MAP: 'query/UPDATE_ALTERING_TABLE_RESULT_MAP',
        }),
        ...mapActions({
            alterTable: 'query/alterTable',
        }),
        closeDDLEditor() {
            // Clear altered active node
            this.UPDATE_TBL_CREATION_INFO_MAP({
                id: this.active_wke_id,
                payload: {
                    altered_active_node: null,
                },
            })
            this.SET_CURR_EDITOR_MODE(this.SQL_EDITOR_MODES.TXT_EDITOR)
        },
        revertChanges() {
            this.tableOptsData = this.$help.lodash.cloneDeep(this.initialTblOptsData)
        },
        handleAddDelimiter({ sql, isLastKey }) {
            return `${sql}${isLastKey ? ';' : ', '}`
        },
        applyChanges() {
            const { escapeIdentifiers: escape, objectDiff } = this.$help
            const { dbName, table_name: initialTblName } = this.initialTblOptsData
            let sql = `ALTER TABLE ${escape(dbName)}.${escape(initialTblName)}\n`
            const diff = objectDiff({ base: this.initialTblOptsData, object: this.newData })
            const keys = Object.keys(diff)
            keys.forEach((key, i) => {
                switch (key) {
                    case 'table_name':
                        sql += `RENAME TO ${escape(dbName)}.${escape(diff[key])}`
                        break
                    case 'table_engine':
                        sql += `ENGINE = ${diff[key]}`
                        break
                    case 'table_charset':
                        sql += `CHARACTER SET = ${diff[key]}`
                        break
                    case 'table_collation':
                        sql += `COLLATE = ${diff[key]}`
                        break
                    case 'table_comment':
                        sql += `COMMENT = '${diff[key]}'`
                        break
                }
                sql = this.handleAddDelimiter({ sql, isLastKey: i === keys.length - 1 })
            })
            this.sql = sql
            // before opening dialog, manually clear isErrDialogShown so that query-editor can be shown
            this.isErrDialogShown = false
            this.$refs.confirmAlterDialog.open()
        },
        async confirmAlter() {
            await this.alterTable(this.sql)
            if (!this.isAlterFailed)
                this.UPDATE_TBL_CREATION_INFO_MAP({
                    id: this.active_wke_id,
                    payload: {
                        table_opts_data: this.tableOptsData,
                    },
                })
            else this.isErrDialogShown = true
        },
        clearAlterResult() {
            this.UPDATE_ALTERING_TABLE_RESULT_MAP({
                id: this.active_wke_id,
            })
        },
    },
}
</script>
<style lang="scss" scoped>
.alter-err-tbl {
    background-color: $reflection;
    td {
        color: $code-color;
        vertical-align: top;
        padding-bottom: 4px;
        &:first-of-type {
            padding-right: 16px;
        }
    }
}
</style>
