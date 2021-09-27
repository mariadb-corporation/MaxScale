<template>
    <div>
        <!-- TODO: Replace with table functions -->
        <div>
            TABLE functions
        </div>

        <v-tabs v-model="activeColSpec" :height="24" class="tab-navigation-wrapper">
            <v-tab color="primary" :href="`#${SQL_DDL_ALTER_SPECS.COLUMNS}`">
                <span> {{ $t('columns') }} </span>
            </v-tab>
            <v-tab color="primary" :href="`#${SQL_DDL_ALTER_SPECS.TRIGGERS}`">
                <span>{{ $t('triggers') }} </span>
            </v-tab>
        </v-tabs>
        <v-slide-x-transition>
            <keep-alive>
                <div v-if="activeColSpec === SQL_DDL_ALTER_SPECS.COLUMNS">
                    <!-- TODO: Replace with column functions -->
                    Alter column functions here
                </div>
                <div v-else-if="activeColSpec === SQL_DDL_ALTER_SPECS.TRIGGERS">
                    <!-- TODO: Replace with triggers functions -->
                    Alter triggers functions here
                </div>
            </keep-alive>
        </v-slide-x-transition>
    </div>
</template>

<script>
import { mapMutations, mapState } from 'vuex'
export default {
    name: 'ddl-editor',
    computed: {
        ...mapState({
            SQL_DDL_ALTER_SPECS: state => state.app_config.SQL_DDL_ALTER_SPECS,
            curr_ddl_alter_spec: state => state.query.curr_ddl_alter_spec,
        }),
        activeColSpec: {
            get() {
                return this.curr_ddl_alter_spec
            },
            set(value) {
                this.SET_CURR_DDL_COL_SPEC(value)
            },
        },
    },
    methods: {
        ...mapMutations({ SET_CURR_DDL_COL_SPEC: 'query/SET_CURR_DDL_COL_SPEC' }),
    },
}
</script>

<style lang="scss" scoped></style>
