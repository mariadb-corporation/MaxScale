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
            <v-tab v-for="spec of DDL_ALTER_SPECS" :key="spec" color="primary" :href="`#${spec}`">
                <span> {{ $mxs_t(spec.toLowerCase()) }}</span>
            </v-tab>
        </v-tabs>
        <div class="px-3 py-2">
            <v-slide-x-transition>
                <keep-alive>
                    <alter-cols-opts
                        v-if="activeColSpec === DDL_ALTER_SPECS.COLUMNS"
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
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapGetters } from 'vuex'
import QueryTab from '@queryEditorSrc/store/orm/models/QueryTab'
import Editor from '@queryEditorSrc/store/orm/models/Editor'
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
            DDL_ALTER_SPECS: state => state.queryEditorConfig.config.DDL_ALTER_SPECS,
            charset_collation_map: state => state.editors.charset_collation_map,
            engines: state => state.editors.engines,
            def_db_charset_map: state => state.editors.def_db_charset_map,
        }),
        ...mapGetters({
            getCurrDdlAlterSpec: 'editors/getCurrDdlAlterSpec',
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
                return this.getCurrDdlAlterSpec
            },
            set(value) {
                Editor.update({
                    where: QueryTab.getters('getActiveQueryTabId'),
                    data: { curr_ddl_alter_spec: value },
                })
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
        setHeaderHeight() {
            if (!this.$refs.header) return
            this.headerHeight = this.$refs.header.clientHeight
        },
    },
}
</script>
