<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 result-header d-flex align-center d-flex flex-row">
            <template v-if="!isLoading">
                <i18n
                    v-if="!queryTxt"
                    path="mxs.resultTabGuide"
                    class="d-flex align-center"
                    tag="div"
                >
                    <template v-slot:shortcut>
                        &nbsp;<b>{{ OS_KEY }} + SHIFT + ENTER</b>&nbsp;
                    </template>
                    <template v-slot:icon>
                        &nbsp;
                        <v-icon color="accent-dark" size="16">$vuetify.icons.mxs_running</v-icon>
                        &nbsp;
                    </template>
                </i18n>
                <v-menu
                    v-else
                    offset-y
                    top
                    transition="slide-y-transition"
                    :close-on-content-click="false"
                    content-class="shadow-drop mxs-color-helper text-navigation "
                    open-on-hover
                    nudge-left="16"
                >
                    <template v-slot:activator="{ on }">
                        <span class="mr-4 pointer mxs-color-helper text-anchor " v-on="on">
                            {{ $mxs_t('queryTxt') }}
                        </span>
                    </template>
                    <v-sheet class="text-body-2 py-2 px-4 mxs-color-helper text-navigation">
                        {{ queryTxt }}
                    </v-sheet>
                </v-menu>
            </template>
            <!-- TODO: Get dynamic width instead of hard-code value (200) -->
            <v-tabs
                v-model="activeResSet"
                show-arrows
                hide-slider
                :height="20"
                class="v-tabs--mxs-workspace-style v-tabs--custom-small-pagination-btn"
                :style="{ maxWidth: `calc(100% - ${200}px)` }"
                center-active
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
                    v-if="requestSentTime"
                    :startTime="requestSentTime"
                    :executionTime="execTime"
                    :totalDuration="totalDuration"
                />
            </keep-alive>

            <v-tooltip
                v-if="
                    $typy(resultData[activeResSet], 'data').isDefined &&
                        !resultData[activeResSet].complete
                "
                top
                transition="slide-y-transition"
            >
                <template v-slot:activator="{ on }">
                    <div class="ml-4 d-flex align-center" v-on="on">
                        <v-icon size="16" color="error" class="mr-2">
                            $vuetify.icons.mxs_alertWarning
                        </v-icon>
                        {{ $mxs_t('incomplete') }}
                    </div>
                </template>
                {{ $mxs_t('info.queryIncomplete') }}
            </v-tooltip>
        </div>
        <v-skeleton-loader
            v-if="isLoading"
            :loading="isLoading"
            type="table: table-thead, table-tbody"
            :height="dynDim.height - headerHeight"
        />
        <template v-else>
            <keep-alive v-for="(resSet, name) in resultData" :key="name">
                <template v-if="activeResSet === name">
                    <result-data-table
                        v-if="$typy(resSet, 'data').isDefined"
                        :height="dynDim.height - headerHeight"
                        :width="dynDim.width"
                        :headers="resSet.fields.map(field => ({ text: field }))"
                        :rows="resSet.data"
                        showGroupBy
                        v-on="$listeners"
                    />
                    <div v-else :style="{ height: `${dynDim.height - headerHeight}px` }">
                        <div v-for="(v, key) in resSet" :key="key">
                            <b>{{ key }}:</b>
                            <span class="d-inline-block ml-4">{{ v }}</span>
                        </div>
                    </div>
                </template>
            </keep-alive>
        </template>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ResultDataTable from '@wkeComps/QueryEditor/ResultDataTable'
import DurationTimer from '@wkeComps/QueryEditor/DurationTimer'
import { mapState } from 'vuex'
export default {
    name: 'results-tab',
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
        isLoading: { type: Boolean, required: true },
        data: { type: Object, required: true },
        requestSentTime: { type: Number, required: true },
        execTime: { type: Number, required: true },
        totalDuration: { type: Number, required: true },
    },
    data() {
        return {
            headerHeight: 0,
            activeResSet: '',
            runSeconds: 0,
        }
    },
    computed: {
        ...mapState({
            OS_KEY: state => state.mxsWorkspace.config.OS_KEY,
        }),
        queryTxt() {
            return this.$typy(this.data, 'data.attributes.sql').safeObject
        },
        resultData() {
            if (this.$typy(this.data, 'data.attributes.results').isDefined) {
                let resultData = {}
                let resSetCount = 0
                let resCount = 0
                for (const res of this.data.data.attributes.results) {
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
        this.watch_resultData()
    },
    deactivated() {
        this.$typy(this.unwatch_resultData).safeFunction()
    },
    methods: {
        watch_resultData() {
            this.unwatch_resultData = this.$watch('resultData', () => {
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
