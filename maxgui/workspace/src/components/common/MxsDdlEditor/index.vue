<template>
    <v-form v-model="isFormValid">
        <div
            class="d-flex align-center mxs-color-helper border-bottom-table-border"
            :style="{ height: `${toolbarHeight}px` }"
        >
            <revert-btn :disabled="!hasChanged || isCreating" @click="onRevert" />
            <apply-btn :disabled="!hasValidChanges" @click="onApply" />
            <slot name="toolbar-append" :isFormValid="isFormValid" />
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
                    />
                    <fk-definitions
                        v-else-if="activeSpec === DDL_EDITOR_SPECS.FK"
                        v-model="fks"
                        :stagingCols="definitions.cols"
                        :initialData="initialFks"
                        :lookupTables="lookupTables"
                        :stagingColNameMap="stagingColNameMap"
                        :dim="tabDim"
                        :connData="connData"
                        :charsetCollationMap="charset_collation_map"
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
        }
    },
    computed: {
        ...mapState({
            DDL_EDITOR_SPECS: state => state.mxsWorkspace.config.DDL_EDITOR_SPECS,
            CREATE_TBL_TOKENS: state => state.mxsWorkspace.config.CREATE_TBL_TOKENS,
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
        hasValidChanges() {
            return this.isFormValid && this.hasChanged
        },
        stagingColNameMap() {
            return queryHelper.createColNameMap(this.$typy(this.definitions, 'cols').safeArray)
        },
    },
    watch: {
        isFormValid(v) {
            this.$emit('is-form-valid', v)
        },
    },
    mounted() {
        this.activeSpec = this.DDL_EDITOR_SPECS.COLUMNS
        this.setHeaderHeight()
    },
    methods: {
        ...mapMutations({ SET_EXEC_SQL_DLG: 'mxsWorkspace/SET_EXEC_SQL_DLG' }),
        setHeaderHeight() {
            if (!this.$refs.header) return
            this.headerHeight = this.$refs.header.clientHeight
        },
        onRevert() {
            this.stagingData = this.$helpers.lodash.cloneDeep(this.initialData)
        },
        onApply() {
            const builder = new TableScriptBuilder({
                initialData: this.initialData,
                stagingData: this.stagingData,
                stagingColNameMap: this.stagingColNameMap,
                isCreateTable: this.isCreating,
            })
            this.SET_EXEC_SQL_DLG({
                ...this.exec_sql_dlg,
                is_opened: true,
                sql: builder.build(),
                on_exec: this.onExecute,
                on_after_cancel: () =>
                    this.SET_EXEC_SQL_DLG({ ...this.exec_sql_dlg, result: null }),
            })
        },
    },
}
</script>
