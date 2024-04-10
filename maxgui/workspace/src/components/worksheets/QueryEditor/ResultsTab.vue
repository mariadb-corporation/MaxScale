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
                        <v-icon color="primary" size="16">$vuetify.icons.mxs_running</v-icon>
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
            <v-menu
                v-if="resultSetItems.length"
                v-model="isResultSetsMenuOpened"
                transition="slide-y-transition"
                offset-y
                left
                content-class="v-menu--mariadb v-menu--mariadb-full-border"
                close-on-content-click
            >
                <template v-slot:activator="{ on, attrs, value }">
                    <v-btn
                        x-small
                        class="text-capitalize font-weight-medium"
                        outlined
                        depressed
                        :color="`${isErrorTab ? 'error' : 'primary'}`"
                        v-bind="attrs"
                        v-on="on"
                    >
                        {{ activeResSet }}
                        <v-icon size="24" :class="[value ? 'rotate-up' : 'rotate-down']">
                            mdi-menu-down
                        </v-icon>
                    </v-btn>
                </template>
                <virtual-list
                    ref="resultSetItems"
                    v-model="activeResSet"
                    data-key="id"
                    :data-sources="resultSetItems"
                    :data-component="ResultSetItem"
                    class="result-sets-list"
                    item-class="result-set-item"
                    :extra-props="{
                        activeId: activeResSet,
                        errorTabId: errorTabId,
                        onClick: onClickResSetTab,
                    }"
                    :estimate-size="36"
                    :style="{ maxHeight: `300px` }"
                />
            </v-menu>
            <v-spacer />
            <duration-timer
                v-if="requestSentTime"
                :startTime="requestSentTime"
                :executionTime="execTime"
                :totalDuration="totalDuration"
            />
            <v-tooltip
                v-if="
                    $typy(resultData[activeResSet], 'headers').isDefined &&
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
            :height="resultTableHeight"
        />
        <template v-else>
            <keep-alive v-for="(resSet, name) in resultData" :key="name">
                <template v-if="activeResSet === name">
                    <result-data-table
                        v-if="$typy(resSet, 'headers').isDefined"
                        :height="resultTableHeight"
                        :width="dim.width"
                        :headers="resSet.headers"
                        :data="resSet.rows"
                        :metadata="resSet.metadata"
                        showGroupBys
                        v-on="$listeners"
                    />
                    <div v-else :style="{ height: `${resultTableHeight}px` }">
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ResultDataTable from '@wkeComps/QueryEditor/ResultDataTable'
import DurationTimer from '@wkeComps/QueryEditor/DurationTimer'
import VirtualList from 'vue-virtual-scroll-list'
import ResultSetItem from '@wkeComps/QueryEditor/ResultSetItem'
import { OS_KEY } from '@wsSrc/constants'

export default {
    name: 'results-tab',
    components: {
        ResultDataTable,
        DurationTimer,
        VirtualList,
    },
    props: {
        dim: {
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
            isResultSetsMenuOpened: false,
        }
    },
    computed: {
        ResultSetItem() {
            return ResultSetItem
        },
        queryTxt() {
            return this.$typy(this.data, 'data.attributes.sql').safeObject
        },
        resultSetItems() {
            return Object.keys(this.resultData).map(key => ({ id: key }))
        },
        resultData() {
            if (this.$typy(this.data, 'data.attributes.results').isDefined) {
                let resultData = {}
                let resSetCount = 0
                let resCount = 0
                for (const res of this.data.data.attributes.results) {
                    if (this.$typy(res, 'data').isDefined) {
                        ++resSetCount
                        resultData[`Result set ${resSetCount}`] = {
                            headers: this.$typy(res, 'fields').safeArray.map(field => ({
                                text: field,
                            })),
                            rows: this.$typy(res, 'data').safeArray,
                            complete: this.$typy(res, 'complete').safeBoolean,
                            metadata: this.$typy(res, 'metadata').safeArray,
                        }
                    } else if (this.$typy(res, 'errno').isDefined) {
                        resultData[this.errorTabId] = res
                    } else {
                        ++resCount
                        resultData[`Result ${resCount}`] = res
                    }
                }
                return resultData
            } else return {}
        },
        resultTableHeight() {
            return this.dim.height - this.headerHeight
        },
        errorTabId() {
            return 'Error'
        },
        isErrorTab() {
            return this.activeResSet === this.errorTabId
        },
    },
    watch: {
        isResultSetsMenuOpened(v) {
            if (v)
                this.$helpers.doubleRAF(() => {
                    if (this.isErrorTab) this.$refs.resultSetItems.scrollToBottom()
                })
        },
        resultSetItems: {
            deep: true,
            handler(v) {
                if (v.length) {
                    const errResSetIdx = v.findIndex(item => item.id === this.errorTabId)
                    this.activeResSet = errResSetIdx >= 0 ? v[errResSetIdx].id : v[0].id
                }
            },
        },
        isLoading(v) {
            if (!v) this.setHeaderHeight()
        },
    },
    created() {
        this.OS_KEY = OS_KEY
    },
    methods: {
        setHeaderHeight() {
            if (this.$refs.header) this.headerHeight = this.$refs.header.clientHeight
        },
        onClickResSetTab(item) {
            this.activeResSet = item.id
        },
    },
}
</script>
<style lang="scss" scoped>
.result-sets-list {
    overflow-y: auto;
    background: white;
}
</style>
