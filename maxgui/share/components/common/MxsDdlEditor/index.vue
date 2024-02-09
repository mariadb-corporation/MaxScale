<template>
    <v-form ref="form" v-model="isFormValid" lazy-validation>
        <div
            class="d-flex align-center mxs-color-helper border-bottom-table-border"
            :style="{ height: `${toolbarHeight}px` }"
        >
            <revert-btn v-if="!isCreating" :disabled="!hasChanged" @click="onRevert" />
            <apply-btn v-if="showApplyBtn" :disabled="!hasChanged" @click="onApply" />
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
        <v-tabs v-model="activeSpecTab" :height="24" class="v-tabs--mariadb">
            <v-tab v-for="spec of DDL_EDITOR_SPECS" :key="spec" color="primary" :href="`#${spec}`">
                <span> {{ $mxs_t(spec) }}</span>
            </v-tab>
        </v-tabs>
        <div class="px-3 py-2" :style="{ height: `${tabDim.height + 16}px` }">
            <v-slide-x-transition>
                <keep-alive>
                    <col-definitions
                        v-if="activeSpecTab === DDL_EDITOR_SPECS.COLUMNS"
                        v-model="defs"
                        :charsetCollationMap="charset_collation_map"
                        :initialData="initialDefinitions"
                        :dim="tabDim"
                        :defTblCharset="$typy(tblOpts, 'charset').safeString"
                        :defTblCollation="$typy(tblOpts, 'collation').safeString"
                        :colKeyCategoryMap="colKeyCategoryMap"
                    />
                    <template v-else-if="activeSpecTab === DDL_EDITOR_SPECS.FK">
                        <fk-definitions
                            v-if="$typy(tblOpts, 'engine').safeString === FK_SUPPORTED_ENGINE"
                            v-model="keyCategoryMap"
                            :lookupTables="lookupTables"
                            :newLookupTables.sync="newLookupTables"
                            :allLookupTables="allLookupTables"
                            :allTableColMap="allTableColMap"
                            :refTargets="refTargets"
                            :tablesColNameMap="tablesColNameMap"
                            :tableId="stagingData.id"
                            :dim="tabDim"
                            :connData="connData"
                            :charsetCollationMap="charset_collation_map"
                        />
                        <span v-else class="d-inline-block grayed-out-info pt-3">
                            {{ $mxs_t('info.fkEngineSupport') }}
                        </span>
                    </template>
                    <index-definitions
                        v-else-if="activeSpecTab === DDL_EDITOR_SPECS.INDEXES"
                        v-model="keyCategoryMap"
                        :tableColNameMap="$typy(tablesColNameMap[stagingData.id]).safeObjectOrEmpty"
                        :dim="tabDim"
                        :tableColMap="$typy(allTableColMap[stagingData.id]).safeObjectOrEmpty"
                    />
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
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapMutations } from 'vuex'
import TableOpts from '@share/components/common/MxsDdlEditor/TableOpts.vue'
import ColDefinitions from '@share/components/common/MxsDdlEditor/ColDefinitions.vue'
import FkDefinitions from '@share/components/common/MxsDdlEditor/FkDefinitions.vue'
import IndexDefinitions from '@share/components/common/MxsDdlEditor/IndexDefinitions.vue'
import RevertBtn from '@share/components/common/MxsDdlEditor/RevertBtn.vue'
import ApplyBtn from '@share/components/common/MxsDdlEditor/ApplyBtn.vue'
import TableScriptBuilder from '@wsSrc/utils/TableScriptBuilder.js'
import erdHelper from '@wsSrc/utils/erdHelper'
import { EventBus } from '@wkeComps/EventBus'
import { DDL_EDITOR_SPECS, FK_SUPPORTED_ENGINE } from '@wsSrc/constants'

export default {
    name: 'mxs-ddl-editor',
    components: {
        RevertBtn,
        ApplyBtn,
        TableOpts,
        ColDefinitions,
        FkDefinitions,
        IndexDefinitions,
    },
    props: {
        value: { type: Object, required: true },
        dim: { type: Object, required: true },
        initialData: { type: Object, required: true },
        isCreating: { type: Boolean, default: false }, // set true to output CREATE TABLE script
        schemas: { type: Array, default: () => [] }, // list of schema names
        onExecute: { type: Function, default: () => null },
        // parsed tables to be looked up in fk-definitions
        lookupTables: { type: Object, default: () => ({}) },
        /**
         * hinted referenced targets is a list of tables that are in
         * the same schema as the table being altered/created
         */
        hintedRefTargets: { type: Array, default: () => [] },
        connData: { type: Object, required: true },
        activeSpec: { type: String, required: true }, //sync
        showApplyBtn: { type: Boolean, default: true },
    },
    data() {
        return {
            isFormValid: true,
            headerHeight: 0,
            newLookupTables: {},
        }
    },
    computed: {
        ...mapState({
            charset_collation_map: state => state.editorsMem.charset_collation_map,
            engines: state => state.editorsMem.engines,
            def_db_charset_map: state => state.editorsMem.def_db_charset_map,
            exec_sql_dlg: state => state.mxsWorkspace.exec_sql_dlg,
        }),
        activeSpecTab: {
            get() {
                return this.activeSpec
            },
            set(v) {
                this.$emit('update:activeSpec', v)
            },
        },
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
        defs: {
            get() {
                return this.$typy(this.stagingData, 'defs').safeObjectOrEmpty
            },
            set(v) {
                this.stagingData = this.$helpers.immutableUpdate(this.stagingData, {
                    defs: { $set: v },
                })
            },
        },
        initialDefinitions() {
            return this.$typy(this.initialData, 'defs').safeObjectOrEmpty
        },
        keyCategoryMap: {
            get() {
                return this.$typy(this.defs, `key_category_map`).safeObjectOrEmpty
            },
            set(v) {
                this.stagingData = this.$helpers.immutableUpdate(this.stagingData, {
                    defs: { key_category_map: { $set: v } },
                })
            },
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
        knownTargets() {
            return erdHelper.genRefTargets(this.allLookupTables)
        },
        knownTargetMap() {
            // keyed by text as the genRefTargets method assign qualified name of the table to text field
            return this.$helpers.lodash.keyBy(this.knownTargets, 'text')
        },
        refTargets() {
            return [
                ...this.knownTargets,
                ...this.hintedRefTargets.filter(item => !this.knownTargetMap[item.text]),
            ]
        },
        tablesColNameMap() {
            return erdHelper.createTablesColNameMap(this.allLookupTables)
        },
        colKeyCategoryMap() {
            return erdHelper.genColKeyTypeMap(this.defs.key_category_map)
        },
        /**
         * @returns {Object.<string, object.<object>>}  e.g. { "tbl_123": {} }
         */
        allTableColMap() {
            return this.allLookupTables.reduce((res, tbl) => {
                res[tbl.id] = this.$typy(tbl, 'defs.col_map').safeObjectOrEmpty
                return res
            }, {})
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
        this.DDL_EDITOR_SPECS = DDL_EDITOR_SPECS
        this.FK_SUPPORTED_ENGINE = FK_SUPPORTED_ENGINE
        this.addKeyEvtListener()
    },
    beforeDestroy() {
        this.removeKeyEvtListener()
    },
    mounted() {
        this.setHeaderHeight()
    },
    methods: {
        ...mapMutations({
            SET_EXEC_SQL_DLG: 'mxsWorkspace/SET_EXEC_SQL_DLG',
            SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE',
        }),
        addKeyEvtListener() {
            if (this.showApplyBtn) this.eventBus.$on('workspace-shortkey', this.shortKeyHandler)
        },
        removeKeyEvtListener() {
            if (this.showApplyBtn) this.eventBus.$off('workspace-shortkey')
        },
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
                    after_cancel: () =>
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
