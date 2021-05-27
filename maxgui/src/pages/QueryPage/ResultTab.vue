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
                    nudge-left="16"
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
                    v-model="activeResSet"
                    show-arrows
                    hide-slider
                    :height="20"
                    class="tab-navigation--btn-style tab-navigation--btn-style--custom-max-width"
                >
                    <v-tab
                        v-for="(resSet, name) in resultData"
                        :key="name"
                        :href="`#${name}`"
                        class="tab-btn px-3 text-uppercase"
                        :class="{ 'tab-btn--err-tab': getErrTabName() === name }"
                        active-class="tab-btn--active font-weight-medium"
                    >
                        {{ name }}
                    </v-tab>
                </v-tabs>
                <v-spacer />
                <duration-timer
                    :startTime="query_request_sent_time"
                    :executionTime="executionTime"
                />
            </template>
        </div>
        <template v-if="!showGuide">
            <v-skeleton-loader
                v-if="loading_query_result"
                :loading="loading_query_result"
                type="table: table-thead, table-tbody"
                :height="dynDim.height - headerHeight"
            />
            <template v-else>
                <template v-for="(resSet, name) in resultData">
                    <v-slide-x-transition :key="name">
                        <keep-alive>
                            <template v-if="activeResSet === name">
                                <result-data-table
                                    v-if="$typy(resSet, 'data').isDefined"
                                    :height="dynDim.height - headerHeight"
                                    :width="dynDim.width"
                                    :headers="resSet.fields"
                                    :rows="resSet.data"
                                />
                                <div
                                    v-else
                                    :style="{ height: `${dynDim.height - headerHeight}px` }"
                                >
                                    <template v-for="(v, key) in resSet">
                                        <div :key="key">
                                            <b>{{ key }}:</b>
                                            <span class="d-inline-block ml-4">{{ v }}</span>
                                        </div>
                                    </template>
                                </div>
                            </template>
                        </keep-alive>
                    </v-slide-x-transition>
                </template>
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
import DurationTimer from './DurationTimer'
export default {
    name: 'result-tab',
    components: {
        ResultDataTable,
        DurationTimer,
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
            activeResSet: '',
            runSeconds: 0,
        }
    },
    computed: {
        ...mapState({
            query_result: state => state.query.query_result,
            loading_query_result: state => state.query.loading_query_result,
            active_conn_state: state => state.query.active_conn_state,
            query_request_sent_time: state => state.query.query_request_sent_time,
        }),
        showGuide() {
            return this.isMounted || !this.active_conn_state
        },
        queryTxt() {
            return this.$typy(this.query_result, 'attributes.sql').safeObject
        },
        executionTime() {
            if (this.loading_query_result) return -1
            const seconds = this.$typy(this.query_result, 'attributes.execution_time').safeObject
            return parseFloat(seconds.toFixed(3))
        },
        resultData() {
            if (this.$typy(this.query_result, 'attributes.results').isDefined) {
                let resultData = {}
                let resSetCount = 0
                let resCount = 0
                for (const res of this.query_result.attributes.results) {
                    if (this.$typy(res, 'data').isDefined) {
                        ++resSetCount
                        resultData[`Result set ${resSetCount}`] = res
                    } else if (this.$typy(res, 'errno').isDefined) {
                        resultData[`Error`] = res
                    } else {
                        ++resCount
                        resultData[`Result ${resCount}`] = res
                    }
                }
                return resultData
            } else return {}
        },
    },
    watch: {
        loading_query_result(v) {
            // After user clicks Run to send query, set isMounted to false to show skeleton-loader
            if (v && this.isMounted) this.isMounted = false
        },
        resultData: {
            deep: true,
            handler() {
                if (this.getErrTabName()) this.activeResSet = this.getErrTabName()
            },
        },
    },

    mounted() {
        this.setHeaderHeight()
    },
    methods: {
        /**
         * This function checks for result set having syntax error or error message
         * @returns {String} Return resultData key tab name. e.g. Result_set_0
         */
        getErrTabName() {
            for (const key in this.resultData) {
                if (this.$typy(this.resultData[key], 'errno').isDefined) return key
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
    max-width: calc(100% - 250px);
}
</style>
