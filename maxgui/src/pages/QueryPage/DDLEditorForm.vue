<template>
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
                <v-tab-item v-for="spec in SQL_DDL_ALTER_SPECS" :id="spec" :key="spec" class="pt-2">
                    <div v-if="activeColSpec === spec" class="px-4 py-2">
                        <!-- TODO: Replace with columns/triggers input specs -->
                        {{ spec }} input specs here
                    </div>
                </v-tab-item>
            </v-tabs-items>
        </v-tabs>
    </v-form>
</template>

<script>
import { mapMutations, mapState } from 'vuex'
import AlterTableOpts from './AlterTableOpts.vue'
export default {
    name: 'ddl-editor-form',
    components: {
        'alter-table-opts': AlterTableOpts,
    },
    model: {
        prop: 'formData',
        event: 'input',
    },
    props: {
        formData: { type: Object, required: true },
    },
    data() {
        return {
            isFormValid: true,
        }
    },
    computed: {
        ...mapState({
            SQL_DDL_ALTER_SPECS: state => state.app_config.SQL_DDL_ALTER_SPECS,
            curr_ddl_alter_spec: state => state.query.curr_ddl_alter_spec,
        }),
        tableOptsData: {
            get() {
                return this.$typy(this.formData, 'table_opts_data').safeObjectOrEmpty
            },
            set(v) {
                this.$emit('input', { ...this.formData, table_opts_data: v })
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
    },
    watch: {
        isFormValid(v) {
            this.$emit('is-form-valid', v)
        },
    },
    methods: {
        ...mapMutations({
            SET_CURR_DDL_COL_SPEC: 'query/SET_CURR_DDL_COL_SPEC',
        }),
    },
}
</script>
