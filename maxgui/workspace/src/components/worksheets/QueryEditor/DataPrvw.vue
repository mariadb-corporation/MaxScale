<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 result-header d-flex align-center">
            <template v-if="nodeQualifiedName">
                <div class="d-flex align-center mr-4">
                    <b class="mr-1">Table:</b>
                    <mxs-truncate-str
                        :tooltipItem="{ txt: nodeQualifiedName, nudgeLeft: 16 }"
                        :maxWidth="260"
                    />
                </div>
                <data-prvw-nav-ctr
                    :queryTabId="queryTabId"
                    :queryMode="queryMode"
                    :isLoading="isLoading"
                    :resultData="resultData"
                    :nodeQualifiedName="nodeQualifiedName"
                />
                <v-spacer />
                <duration-timer
                    :startTime="requestSentTime"
                    :executionTime="execTime"
                    :totalDuration="totalDuration"
                />
            </template>
            <span v-else v-html="$mxs_t('prvwTabGuide')" />
        </div>
        <v-skeleton-loader
            v-if="isLoading"
            :loading="isLoading"
            type="table: table-thead, table-tbody"
            :height="dim.height - headerHeight"
        />
        <template v-else>
            <keep-alive>
                <result-data-table
                    v-if="$typy(resultData, 'fields').safeArray.length"
                    :key="queryMode"
                    :height="dim.height - headerHeight"
                    :width="dim.width"
                    :headers="$typy(resultData, 'fields').safeArray.map(field => ({ text: field }))"
                    :data="$typy(resultData, 'data').safeArray"
                    :metadata="$typy(resultData, 'metadata').safeArray"
                    showGroupBy
                    v-on="$listeners"
                />
                <template v-else>
                    <div v-for="(v, key) in resultData" :key="key">
                        <b>{{ key }}:</b>
                        <span class="d-inline-block ml-4">{{ v }}</span>
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
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ResultDataTable from '@wkeComps/QueryEditor/ResultDataTable'
import DurationTimer from '@wkeComps/QueryEditor/DurationTimer'
import DataPrvwNavCtr from '@wkeComps/QueryEditor/DataPrvwNavCtr.vue'

export default {
    name: 'data-prvw',
    components: {
        ResultDataTable,
        DurationTimer,
        DataPrvwNavCtr,
    },
    props: {
        dim: {
            type: Object,
            validator(obj) {
                return 'width' in obj && 'height' in obj
            },
            required: true,
        },
        queryMode: { type: String, required: true },
        queryTabId: { type: String, required: true },
        queryTabTmp: { type: Object, required: true },
        isLoading: { type: Boolean, required: true },
        data: { type: Object, required: true },
        requestSentTime: { type: Number, required: true },
        execTime: { type: Number, required: true },
        totalDuration: { type: Number, required: true },
    },
    data() {
        return {
            headerHeight: 0,
        }
    },
    computed: {
        resultData() {
            return this.$typy(this.data, 'data.attributes.results[0]').safeObjectOrEmpty
        },
        nodeQualifiedName() {
            return this.$typy(this.queryTabTmp, 'previewing_node.qualified_name').safeString
        },
    },
    watch: {
        isLoading(v) {
            if (!v) this.setHeaderHeight()
        },
    },
    methods: {
        setHeaderHeight() {
            if (this.$refs.header) this.headerHeight = this.$refs.header.clientHeight
        },
    },
}
</script>
