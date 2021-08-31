<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 result-header d-flex align-center">
            <span v-if="!queryTxt" v-html="$t('resultTabGuide')" />
            <v-menu
                v-else
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
            <keep-alive>
                <duration-timer
                    v-if="getQueryRequestSentTime"
                    :startTime="getQueryRequestSentTime"
                    :executionTime="getQueryExeTime"
                    :totalDuration="getQueryTotalDuration"
                />
            </keep-alive>

            <v-tooltip
                v-if="
                    $typy(resultData[activeResSet], 'data').isDefined &&
                        !resultData[activeResSet].complete
                "
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <div class="ml-4 d-flex align-center" v-on="on">
                        <v-icon size="16" color="error" class="mr-2">
                            $vuetify.icons.alertWarning
                        </v-icon>
                        {{ $t('incomplete') }}
                    </div>
                </template>
                <span> {{ $t('info.queryIncomplete') }}</span>
            </v-tooltip>
        </div>

        <template>
            <v-skeleton-loader
                v-if="getLoadingQueryResult"
                :loading="getLoadingQueryResult"
                type="table: table-thead, table-tbody"
                :height="dynDim.height - headerHeight"
            />
            <template v-else>
                <template v-for="(resSet, name) in resultData">
                    <keep-alive :key="name">
                        <template v-if="activeResSet === name">
                            <result-data-table
                                v-if="$typy(resSet, 'data').isDefined"
                                :height="dynDim.height - headerHeight"
                                :width="dynDim.width"
                                :headers="resSet.fields.map(field => ({ text: field }))"
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
                        </template>
                    </keep-alive>
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
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters, mapState } from 'vuex'
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
            isLoading: true,
            activeResSet: '',
            runSeconds: 0,
        }
    },
    computed: {
        ...mapState({
            active_wke_id: state => state.query.active_wke_id,
        }),
        ...mapGetters({
            getLoadingQueryResult: 'query/getLoadingQueryResult',
            getResults: 'query/getResults',
            getQueryRequestSentTime: 'query/getQueryRequestSentTime',
            getQueryExeTime: 'query/getQueryExeTime',
            getQueryTotalDuration: 'query/getQueryTotalDuration',
        }),
        queryTxt() {
            return this.$typy(this.getResults, 'attributes.sql').safeObject
        },
        resultData() {
            if (this.$typy(this.getResults, 'attributes.results').isDefined) {
                let resultData = {}
                let resSetCount = 0
                let resCount = 0
                for (const res of this.getResults.attributes.results) {
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
    activated() {
        this.setHeaderHeight()
        this.addResultDataWatcher()
    },
    deactivated() {
        this.unwatchResultData()
    },
    methods: {
        addResultDataWatcher() {
            this.unwatchResultData = this.$watch('resultData', () => {
                if (this.getErrTabName()) this.activeResSet = this.getErrTabName()
            })
        },
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
    max-width: calc(100% - 330px);
}
</style>
