<template>
    <v-form ref="form" v-model="isFormValid" lazy-validation>
        <div
            class="d-flex align-center mxs-color-helper border-bottom-table-border"
            :style="{ height: `${toolbarHeight}px` }"
        >
            <revert-btn :disabled="!hasChanged || isCreating" @click="onRevert" />
            <apply-btn :disabled="!hasChanged" @click="onApply" />
            <slot name="toolbar-append" :formRef="$refs.form" />
        </div>
        <div ref="header">
            <table-opts
                v-model="tblOpts"
                :engines="engines"
                :charsetCollationMap="charset_collation_map"
                :defDbCharset="
                    $typy(def_db_charset_map, `${$typy(tblOpts, 'schema').safeString}`).safeString
                "
                :isCreating="isCreating"
                :schemas="schemas"
                @after-expand="setHeaderHeight"
                @after-collapse="setHeaderHeight"
            />
        </div>
        <v-tabs v-model="activeSpec" :height="24" class="v-tabs--mariadb">
            <v-tab v-for="spec of DDL_EDITOR_SPECS" :key="spec" color="primary" :href="`#${spec}`">
                <span> {{ $mxs_t(spec) }}</span>
            </v-tab>
        </v-tabs>
        <div class="px-3 py-2">
            <v-slide-x-transition>
                <keep-alive>
                    <col-definitions
                        v-if="activeSpec === DDL_EDITOR_SPECS.COLUMNS"
                        v-model="definitions"
                        :charsetCollationMap="charset_collation_map"
                        :initialData="initialDefinitions"
                        :dim="tabDim"
                        :defTblCharset="$typy(tblOpts, 'charset').safeString"
                        :defTblCollation="$typy(tblOpts, 'collation').safeString"
                        :colKeyTypeMap="colKeyTypeMap"
                    />
                    <template v-else-if="activeSpec === DDL_EDITOR_SPECS.FK">
                        <fk-definitions
                            v-if="$typy(tblOpts, 'engine').safeString === FK_SUPPORTED_ENGINE"
                            :key="stagingData.id"
                            v-model="fks"
                            :initialData="initialFks"
                            :lookupTables="lookupTables"
                            :newLookupTables.sync="newLookupTables"
                            :allLookupTables="allLookupTables"
                            :refTargets="refTargets"
                            :tablesColNameMap="tablesColNameMap"
                            :tableId="stagingData.id"
                            :dim="tabDim"
                            :connData="connData"
                            :charsetCollationMap="charset_collation_map"
                        />
                        <span v-else class="grayed-out-info mxs-color-helper text-small-text">
                            {{ $mxs_t('info.fkEngineSupport') }}
                        </span>
                    </template>
                </keep-alive>
            </v-slide-x-transition>
        </div>
    </v-form>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import { mapState, mapMutations } from 'vuex'
import TableOpts from '@wsSrc/components/common/MxsDdlEditor/TableOpts.vue'
import ColDefinitions from '@wsSrc/components/common/MxsDdlEditor/ColDefinitions.vue'
import FkDefinitions from '@wsSrc/components/common/MxsDdlEditor/FkDefinitions.vue'
import RevertBtn from '@wsSrc/components/common/MxsDdlEditor/RevertBtn.vue'
import ApplyBtn from '@wsSrc/components/common/MxsDdlEditor/ApplyBtn.vue'
import TableScriptBuilder from '@wsSrc/utils/TableScriptBuilder.js'
import queryHelper from '@wsSrc/store/queryHelper'
import { EventBus } from '@wkeComps/EventBus'

export default {
    name: 'mxs-ddl-editor',
    components: {
        RevertBtn,
        ApplyBtn,
        TableOpts,
        ColDefinitions,
        FkDefinitions,
    },
    props: {
        value: { type: Object, required: true },
        dim: { type: Object, required: true },
        initialData: { type: Object, required: true },
        isCreating: { type: Boolean, default: false }, // set true to output CREATE TABLE script
        schemas: { type: Array, default: () => [] }, // list of schema names
        onExecute: { type: Function, required: true },
        // parsed tables to be looked up in fk-definitions
        lookupTables: { type: Object, default: () => ({}) },
        connData: { type: Object, required: true },
    },
    data() {
        return {
            isFormValid: true,
            headerHeight: 0,
            activeSpec: '',
            newLookupTables: {},
        }
    },
    computed: {
        ...mapState({
            DDL_EDITOR_SPECS: state => state.mxsWorkspace.config.DDL_EDITOR_SPECS,
            CREATE_TBL_TOKENS: state => state.mxsWorkspace.config.CREATE_TBL_TOKENS,
            FK_SUPPORTED_ENGINE: state => state.mxsWorkspace.config.FK_SUPPORTED_ENGINE,
            charset_collation_map: state => state.editorsMem.charset_collation_map,
            engines: state => state.editorsMem.engines,
            def_db_charset_map: state => state.editorsMem.def_db_charset_map,
            exec_sql_dlg: state => state.mxsWorkspace.exec_sql_dlg,
        }),
        stagingData: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        tblOpts: {
            get() {
                return this.$typy(this.stagingData, 'options').safeObjectOrEmpty
            },
            set(v) {
                this.stagingData = this.$helpers.immutableUpdate(this.stagingData, {
                    options: { $set: v },
                })
            },
        },
        definitions: {
            get() {
                return this.$typy(this.stagingData, 'definitions').safeObjectOrEmpty
            },
            set(v) {
                this.stagingData = this.$helpers.immutableUpdate(this.stagingData, {
                    definitions: { $set: v },
                })
            },
        },
        initialDefinitions() {
            return this.$typy(this.initialData, 'definitions').safeObjectOrEmpty
        },
        fks: {
            get() {
                return this.$typy(this.definitions, `keys[${this.CREATE_TBL_TOKENS.foreignKey}]`)
                    .safeArray
            },
            set(v) {
                this.stagingData = this.$helpers.immutableUpdate(this.stagingData, {
                    definitions: {
                        keys: {
                            $merge: {
                                [this.CREATE_TBL_TOKENS.foreignKey]: v,
                            },
                        },
                    },
                })
            },
        },
        initialFks() {
            return this.$typy(this.initialDefinitions, `keys[${this.CREATE_TBL_TOKENS.foreignKey}]`)
                .safeArray
        },
        toolbarHeight() {
            return 28
        },
        editorDim() {
            return { ...this.dim, height: this.dim.height - this.toolbarHeight }
        },
        tabDim() {
            return {
                width: this.editorDim.width - 24, // v-tab-item class px-3
                // v-tab-item class py-2: 16 && v-tabs-bar: 24
                height: this.editorDim.height - this.headerHeight - 24 - 16,
            }
        },
        hasChanged() {
            return !this.$helpers.lodash.isEqual(this.initialData, this.stagingData)
        },
        allLookupTables() {
            return Object.values({ ...this.lookupTables, ...this.newLookupTables })
        },
        refTargets() {
            return queryHelper.genRefTargets(this.allLookupTables)
        },
        tablesColNameMap() {
            return queryHelper.createTablesColNameMap(this.allLookupTables)
        },
        colKeyTypeMap() {
            return queryHelper.genColKeyTypeMap(this.definitions.keys)
        },
        eventBus() {
            return EventBus
        },
    },
    watch: {
        isFormValid(v) {
            this.$emit('is-form-valid', v)
        },
    },
    created() {
        this.eventBus.$on('workspace-shortkey', this.shortKeyHandler)
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
    mounted() {
        this.activeSpec = this.DDL_EDITOR_SPECS.COLUMNS
        this.setHeaderHeight()
    },
    methods: {
        ...mapMutations({
            SET_EXEC_SQL_DLG: 'mxsWorkspace/SET_EXEC_SQL_DLG',
            SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE',
        }),
        setHeaderHeight() {
            if (!this.$refs.header) return
            this.headerHeight = this.$refs.header.clientHeight
        },
        onRevert() {
            this.stagingData = this.$helpers.lodash.cloneDeep(this.initialData)
        },
        onApply() {
            if (this.$refs.form.validate()) {
                const { lodash } = this.$helpers
                const refTargetMap = lodash.keyBy(this.refTargets, 'id')
                const builder = new TableScriptBuilder({
                    initialData: this.initialData,
                    stagingData: this.stagingData,
                    refTargetMap,
                    tablesColNameMap: this.tablesColNameMap,
                    options: { isCreating: this.isCreating },
                })
                this.SET_EXEC_SQL_DLG({
                    ...this.exec_sql_dlg,
                    is_opened: true,
                    sql: builder.build(),
                    on_exec: this.onExecute,
                    on_after_cancel: () =>
                        this.SET_EXEC_SQL_DLG({ ...this.exec_sql_dlg, result: null }),
                })
            } else
                this.SET_SNACK_BAR_MESSAGE({
                    text: [this.$mxs_t('errors.requiredInputs')],
                    type: 'error',
                })
        },
        shortKeyHandler(key) {
            if (this.hasChanged && (key === 'ctrl-enter' || key === 'mac-cmd-enter')) this.onApply()
        },
    },
}
</script>
