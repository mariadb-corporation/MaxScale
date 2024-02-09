<template>
    <bar-chart
        ref="wrapper"
        v-bind="{ ...$attrs }"
        :style="{ width: '100%' }"
        :chartOptions="chartOptions"
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
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { Bar } from 'vue-chartjs/legacy'
import base from '@share/components/common/MxsCharts/base.js'

export default {
    components: { 'bar-chart': Bar },
    mixins: [base],
    inheritAttrs: false,
    computed: {
        chartOptions() {
            return this.baseOpts
        },
    },
}
</script>
