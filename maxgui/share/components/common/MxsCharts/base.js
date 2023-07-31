/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mixins } from 'vue-chartjs'

export default {
    mixins: [mixins.reactiveProp],
    props: { opts: { type: Object } },
    computed: {
        baseOpts() {
            return {
                plugins: {
                    streaming: false,
                },
                scales: {
                    xAxes: [{ gridLines: { drawBorder: true } }],
                    yAxes: [{ gridLines: { drawBorder: false } }],
                },
            }
        },
        options() {
            return this.$helpers.lodash.merge(this.baseOpts, this.opts)
        },
    },
    watch: {
        opts: {
            deep: true,
            handler(v, oV) {
                if (!this.$helpers.lodash.isEqual(v, oV))
                    this.$data._chart.update({ preservation: true })
            },
        },
    },
    mounted() {
        this.renderChart(this.chartData, this.options)
    },
}
