<template>
    <v-form v-model="isFormValid">
        <div ref="header">
            <alter-table-opts
                v-model="tableOptsData"
                :initialData="$typy(initialData, 'table_opts_data').safeObjectOrEmpty"
            />
        </div>
        <v-tabs
            v-if="!isEmptyFormData && activated"
            v-model="activeColSpec"
            :height="24"
            class="tab-navigation-wrapper"
        >
            <v-tab
                v-for="spec of SQL_DDL_ALTER_SPECS"
                :key="spec"
                color="primary"
                :href="`#${spec}`"
            >
                <span> {{ $t(spec.toLowerCase()) }}</span>
            </v-tab>
        </v-tabs>
        <div class="px-3 py-2">
            <v-slide-x-transition>
                <keep-alive>
                    <alter-cols-opts
                        v-if="activeColSpec === SQL_DDL_ALTER_SPECS.COLUMNS"
                        v-model="colsOptsData"
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
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState, mapGetters } from 'vuex'
import AlterTableOpts from './AlterTableOpts.vue'
import AlterColsOpts from './AlterColsOpts.vue'

export default {
    name: 'ddl-editor-form',
    components: {
        'alter-table-opts': AlterTableOpts,
        'alter-cols-opts': AlterColsOpts,
    },
    model: {
        prop: 'formData',
        event: 'input',
    },
    props: {
        formData: { type: Object, required: true },
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
            SQL_DDL_ALTER_SPECS: state => state.app_config.SQL_DDL_ALTER_SPECS,
            curr_ddl_alter_spec: state => state.query.curr_ddl_alter_spec,
        }),
        ...mapGetters({
            getTblCreationInfo: 'query/getTblCreationInfo',
        }),
        initialData() {
            return this.$typy(this.getTblCreationInfo, 'data').safeObjectOrEmpty
        },
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
                this.SET_CURR_DDL_COL_SPEC(value)
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
            SET_CURR_DDL_COL_SPEC: 'query/SET_CURR_DDL_COL_SPEC',
        }),
        setHeaderHeight() {
            if (!this.$refs.header) return
            this.headerHeight = this.$refs.header.clientHeight
        },
    },
}
</script>
