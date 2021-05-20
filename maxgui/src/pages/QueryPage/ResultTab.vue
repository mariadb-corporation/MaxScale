<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 result-header d-flex align-center">
            <span v-if="showGuide" v-html="$t('resultTabGuide')" />
            <template v-else>
                <v-menu
                    offset-y
                    top
                    transition="slide-y-transition"
                    :close-on-content-click="false"
                    content-class="shadow-drop color text-navigation "
                    open-on-hover
                >
                    <template v-slot:activator="{ on }">
                        <span class="mr-4 pointer color text-links " v-on="on">
                            {{ $t('queryTxt') }}
                        </span>
                    </template>
                    <v-sheet class="text-body-2 py-2 px-4 color bg-background text-navigation">
                        {{ queryTxt }}
                    </v-sheet>
                </v-menu>

                <v-tabs
                    v-model="activeResultSet"
                    show-arrows
                    hide-slider
                    :height="20"
                    class="tab-navigation--btn-style tab-navigation--btn-style--custom-max-width"
                >
                    <v-tab
                        v-for="(resSet, name) in resultSets"
                        :key="name"
                        :href="`#${name}`"
                        class="tab-btn px-3 text-uppercase"
                        :class="{ 'tab-btn--err-tab': getErrTabName() === name }"
                        active-class="tab-btn--active font-weight-medium"
                    >
                        {{ name }}
                    </v-tab>
                </v-tabs>
            </template>
        </div>
        <template v-if="!showGuide">
            <v-skeleton-loader
                v-if="loading_query_result"
                :loading="loading_query_result"
                type="table: table-thead, table-tbody"
                :height="dynDim.height - headerHeight"
            />
            <template v-for="(resSet, name) in resultSets" v-else>
                <v-slide-x-transition :key="name">
                    <div v-if="activeResultSet === name">
                        <result-data-table
                            v-if="$typy(resSet, 'data').isDefined"
                            :height="dynDim.height - headerHeight"
                            :width="dynDim.width"
                            :headers="resSet.fields"
                            :rows="resSet.data"
                        />
                        <div v-else :style="{ height: `${dynDim.height - headerHeight}px` }">
                            <template v-for="(v, key) in resSet">
                                <div :key="key">
                                    <b>{{ key }}:</b>
                                    <span class="d-inline-block ml-4">{{ v }}</span>
                                </div>
                            </template>
                        </div>
                    </div>
                </v-slide-x-transition>
            </template>
        </template>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import ResultDataTable from './ResultDataTable'
export default {
    name: 'result-tab',
    components: {
        ResultDataTable,
    },
    props: {
        dynDim: {
            type: Object,
            validator(obj) {
                return 'width' in obj && 'height' in obj
            },
            required: true,
        },
    },
    data() {
        return {
            headerHeight: 0,
            isMounted: true,
            activeResultSet: '',
        }
    },
    computed: {
        ...mapState({
            query_result: state => state.query.query_result,
            loading_query_result: state => state.query.loading_query_result,
            active_conn_state: state => state.query.active_conn_state,
        }),
        showGuide() {
            return this.isMounted || !this.active_conn_state
        },
        queryTxt() {
            return this.$typy(this.query_result, 'attributes.sql').safeObject
        },
        resultSets() {
            if (this.$typy(this.query_result, 'attributes.results').isDefined) {
                let resultSets = {}
                for (const [i, resSet] of this.query_result.attributes.results.entries()) {
                    resultSets[`Result_set_${i + 1}`] = resSet
                }
                return resultSets
            } else return {}
        },
    },
    watch: {
        loading_query_result(v) {
            // After user clicks Run to send query, set isMounted to false to show skeleton-loader
            if (v && this.isMounted) this.isMounted = false
        },
        resultSets: {
            deep: true,
            handler() {
                if (this.getErrTabName()) this.activeResultSet = this.getErrTabName()
            },
        },
    },

    mounted() {
        this.setHeaderHeight()
    },
    methods: {
        /**
         * This function checks for result set having syntax error or error message
         * @returns {String} Return resultSets key tab name. e.g. Result_set_0
         */
        getErrTabName() {
            for (const key in this.resultSets) {
                if (this.$typy(this.resultSets[key], 'errno').isDefined) return key
            }
        },
        setHeaderHeight() {
            if (!this.$refs.header) return
            this.headerHeight = this.$refs.header.clientHeight
        },
    },
}
</script>

<style lang="scss" scoped>
.tab-navigation--btn-style--custom-max-width {
    max-width: calc(100% - 90px);
}
</style>
