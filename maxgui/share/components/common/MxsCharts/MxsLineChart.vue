<template>
    <line-chart
        ref="wrapper"
        v-bind="{ ...$attrs }"
        :style="{ width: '100%' }"
        :chartOptions="chartOptions"
        :plugins="plugins"
        v-on="$listeners"
    />
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { Line } from 'vue-chartjs/legacy'
import vertCrossHair from '@share/components/common/MxsCharts/vertCrossHair.js'
import base from '@share/components/common/MxsCharts/base.js'

export default {
    components: { 'line-chart': Line },
    mixins: [base],
    inheritAttrs: false,
    props: {
        hasVertCrossHair: { type: Boolean, default: false },
    },
    computed: {
        chartOptions() {
            const options = {
                scales: { x: { beginAtZero: true }, y: { beginAtZero: true } },
            }
            return this.$helpers.lodash.merge(options, this.baseOpts)
        },
        plugins() {
            if (this.hasVertCrossHair)
                return [{ id: 'vert-cross-hair', afterDatasetsDraw: vertCrossHair }]
            return []
        },
    },
}
</script>
