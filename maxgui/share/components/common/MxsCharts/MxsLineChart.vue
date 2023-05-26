<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { Line } from 'vue-chartjs'
import vertCrossHair from '@share/components/common/MxsCharts/vertCrossHair.js'
import base from '@share/components/common/MxsCharts/base.js'
export default {
    extends: Line,
    mixins: [base],
    props: {
        hasVertCrossHair: { type: Boolean, default: false },
    },
    computed: {
        baseOpts() {
            return {
                plugins: {
                    streaming: false,
                },
                scales: {
                    xAxes: [{ ticks: { beginAtZero: true } }],
                    yAxes: [{ ticks: { beginAtZero: true } }],
                },
            }
        },
    },
    mounted() {
        if (this.hasVertCrossHair)
            this.addPlugin({ id: 'vert-cross-hair', afterDatasetsDraw: vertCrossHair })
    },
}
</script>
