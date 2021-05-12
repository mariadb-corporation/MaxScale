<template>
    <div class="fill-height">
        <div v-if="showGuide" ref="header" class="pb-2 result-header">
            <span>
                Click Run button to see query results
            </span>
        </div>
        <div v-else class="result-table-wrapper">
            <div ref="header" class="pb-2 result-header-nav d-flex align-center">
                <v-menu
                    offset-y
                    top
                    transition="slide-y-transition"
                    :close-on-content-click="false"
                    content-class="shadow-drop color text-navigation "
                    open-on-hover
                >
                    <template v-slot:activator="{ on }">
                        <span class="d-inline-block pointer color text-links " v-on="on">
                            Query text
                        </span>
                    </template>
                    <v-sheet class="text-body-2 py-2 px-4 color bg-background text-navigation">
                        {{ queryTxt }}
                    </v-sheet>
                </v-menu>
                <!-- TODO: add arrow prev & next to navigate instead of showing horizontal scrollbar -->
                <v-btn-toggle
                    v-model="activeResultSet"
                    class="ml-4 resultset-btn-container"
                    mandatory
                >
                    <v-btn
                        v-for="(resSet, name) in resultSets"
                        :key="name"
                        :value="name"
                        x-small
                        text
                        color="primary"
                    >
                        {{ name }}
                    </v-btn>
                </v-btn-toggle>
            </div>
            <v-skeleton-loader
                v-if="loading_query_result"
                :loading="loading_query_result"
                type="table: table-thead, table-tbody"
                :max-height="`${dynDim.height - headerHeight}px`"
            />
            <!-- TODO: show syntax error or affected_rows fields if there is no return rows for the query -->
            <template v-else>
                <div v-for="(resSet, name) in resultSets" :key="name">
                    <v-slide-x-transition>
                        <result-data-table
                            v-if="activeResultSet === name"
                            :height="dynDim.height - headerHeight"
                            :width="dynDim.width"
                            :headers="resSet.fields"
                            :rows="resSet.data"
                        />
                    </v-slide-x-transition>
                </div>
            </template>
        </div>
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
/* eslint-disable vue/no-unused-components */
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
    },
    mounted() {
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
<style lang="scss" scoped>
.resultset-btn-container {
    max-width: calc(100% - 90px);
    overflow-x: auto;
}
</style>
