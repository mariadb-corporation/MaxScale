<template>
    <div class="chart-container fill-height pa-2">
        <!-- TODO: Add more charts-->
        <div class="chart-container fill-height pa-2">
            <line-chart
                v-if="selectedChart === 'Line'"
                class="line-chart-container"
                :style="{
                    height: `${chartHeight}px`,
                    overflow: 'auto',
                    position: 'relative',
                }"
                :chartData="{
                    labels: [],
                    datasets: [],
                }"
            />
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

export default {
    name: 'chart-container',
    props: {
        selectedChart: { type: String, default: '' },
        chartHeight: { type: Number, default: 0 },
    },
}
</script>
