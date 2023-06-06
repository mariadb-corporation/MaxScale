<template>
    <v-form v-model="isFormValid">
        <div ref="header">
            <alter-table-opts
                v-model="tblOpts"
                :engines="engines"
                :charsetCollationMap="charset_collation_map"
                :defDbCharset="
                    $typy(def_db_charset_map, `${$typy(tblOpts, 'schema').safeString}`).safeString
                "
            />
        </div>
        <v-tabs v-model="activeSpec" :height="24" class="v-tabs--mariadb">
            <v-tab v-for="spec of DDL_ALTER_SPECS" :key="spec" color="primary" :href="`#${spec}`">
                <span> {{ $mxs_t(spec.toLowerCase()) }}</span>
            </v-tab>
        </v-tabs>
        <div class="px-3 py-2">
            <v-slide-x-transition>
                <alter-cols-opts
                    v-if="activeSpec === DDL_ALTER_SPECS.COLUMNS"
                    v-model="colDefinitions"
                    :charsetCollationMap="charset_collation_map"
                    :initialData="$typy(initialData, 'definitions').safeObjectOrEmpty"
                    :height="tabDim.height"
                    :boundingWidth="tabDim.width"
                    :defTblCharset="$typy(tblOpts, 'charset').safeString"
                    :defTblCollation="$typy(tblOpts, 'collation').safeString"
                />
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
import { mapState } from 'vuex'
import AlterTableOpts from '@wsSrc/components/common/MxsDdlEditor/AlterTableOpts.vue'
import AlterColsOpts from '@wsSrc/components/common/MxsDdlEditor/AlterColsOpts.vue'

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
            activeSpec: '',
        }
    },
    computed: {
        ...mapState({
            DDL_ALTER_SPECS: state => state.mxsWorkspace.config.DDL_ALTER_SPECS,
            charset_collation_map: state => state.editorsMem.charset_collation_map,
            engines: state => state.editorsMem.engines,
            def_db_charset_map: state => state.editorsMem.def_db_charset_map,
        }),
        tblOpts: {
            get() {
                return this.$typy(this.formData, 'options').safeObjectOrEmpty
            },
            set(v) {
                this.$emit('input', { ...this.formData, options: v })
            },
        },
        colDefinitions: {
            get() {
                return this.$typy(this.formData, 'definitions').safeObjectOrEmpty
            },
            set(v) {
                this.$emit('input', { ...this.formData, definitions: v })
            },
        },
        tabDim() {
            return {
                width: this.dim.width - 24, // v-tab-item class px-3
                // v-tab-item class py-2: 16 && v-tabs-bar: 24
                height: this.dim.height - this.headerHeight - 24 - 16,
            }
        },
    },
    watch: {
        isFormValid(v) {
            this.$emit('is-form-valid', v)
        },
    },
    mounted() {
        this.activeSpec = this.DDL_ALTER_SPECS.COLUMNS
        this.setHeaderHeight()
    },
    methods: {
        setHeaderHeight() {
            if (!this.$refs.header) return
            this.headerHeight = this.$refs.header.clientHeight
        },
    },
}
</script>
