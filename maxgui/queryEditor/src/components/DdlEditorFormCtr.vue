<template>
    <v-form v-model="isFormValid">
        <div ref="header">
            <alter-table-opts
                v-model="tableOptsData"
                :engines="engines"
                :charsetCollationMap="charset_collation_map"
                :defDbCharset="
                    $typy(def_db_charset_map, `${$typy(tableOptsData, 'dbName').safeString}`)
                        .safeString
                "
            />
        </div>
        <v-tabs
            v-if="!isEmptyFormData && activated"
            v-model="activeColSpec"
            :height="24"
            class="v-tabs--mariadb-style"
        >
            <v-tab
                v-for="spec of SQL_DDL_ALTER_SPECS"
                :key="spec"
                color="primary"
                :href="`#${spec}`"
            >
                <span> {{ $mxs_t(spec.toLowerCase()) }}</span>
            </v-tab>
        </v-tabs>
        <div class="px-3 py-2">
            <v-slide-x-transition>
                <keep-alive>
                    <alter-cols-opts
                        v-if="activeColSpec === SQL_DDL_ALTER_SPECS.COLUMNS"
                        v-model="colsOptsData"
                        :charsetCollationMap="charset_collation_map"
                        :initialData="$typy(initialData, 'cols_opts_data').safeObjectOrEmpty"
                        :height="tabDim.height"
                        :boundingWidth="tabDim.width"
                        :defTblCharset="$typy(tableOptsData, 'table_charset').safeString"
                        :defTblCollation="$typy(tableOptsData, 'table_collation').safeString"
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
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState, mapGetters } from 'vuex'
import AlterTableOpts from './AlterTableOpts.vue'
import AlterColsOpts from './AlterColsOpts.vue'

export default {
    name: 'ddl-editor-form-ctr',
    components: {
        AlterTableOpts,
        AlterColsOpts,
    },
    model: {
        prop: 'formData',
        event: 'input',
    },
    props: {
        formData: { type: Object, required: true },
        initialData: { type: Object, required: true },
        dim: { type: Object, required: true },
    },
    data() {
        return {
            isFormValid: true,
            headerHeight: 0,
            activated: false,
        }
    },
    computed: {
        ...mapState({
            SQL_DDL_ALTER_SPECS: state => state.queryEditorConfig.config.SQL_DDL_ALTER_SPECS,
            curr_ddl_alter_spec: state => state.editor.curr_ddl_alter_spec,
            charset_collation_map: state => state.editor.charset_collation_map,
            engines: state => state.editor.engines,
            def_db_charset_map: state => state.editor.def_db_charset_map,
        }),
        ...mapGetters({
            getActiveSessionId: 'querySession/getActiveSessionId',
        }),
        tableOptsData: {
            get() {
                return this.$typy(this.formData, 'table_opts_data').safeObjectOrEmpty
            },
            set(v) {
                this.$emit('input', { ...this.formData, table_opts_data: v })
            },
        },
        colsOptsData: {
            get() {
                return this.$typy(this.formData, 'cols_opts_data').safeObjectOrEmpty
            },
            set(v) {
                this.$emit('input', { ...this.formData, cols_opts_data: v })
            },
        },
        activeColSpec: {
            get() {
                return this.curr_ddl_alter_spec
            },
            set(value) {
                this.SET_CURR_DDL_ALTER_SPEC({ payload: value, id: this.getActiveSessionId })
            },
        },
        tabDim() {
            return {
                width: this.dim.width - 24, // v-tab-item class px-3
                // v-tab-item class py-2: 16 && v-tabs-bar: 24
                height: this.dim.height - this.headerHeight - 24 - 16,
            }
        },
        isEmptyFormData() {
            return this.$typy(this.formData).isEmptyObject
        },
    },
    watch: {
        isFormValid(v) {
            this.$emit('is-form-valid', v)
        },
        isEmptyFormData(v) {
            if (!v) this.setHeaderHeight()
        },
    },
    activated() {
        this.activated = true
    },
    deactivated() {
        this.activated = false
    },
    methods: {
        ...mapMutations({
            SET_CURR_DDL_ALTER_SPEC: 'editor/SET_CURR_DDL_ALTER_SPEC',
        }),
        setHeaderHeight() {
            if (!this.$refs.header) return
            this.headerHeight = this.$refs.header.clientHeight
        },
    },
}
</script>
