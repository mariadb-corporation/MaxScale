<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { Scatter } from 'vue-chartjs'
import base from '@share/components/common/MxsCharts/base.js'
export default {
    extends: Scatter,
    mixins: [base],
    computed: {
        baseOpts() {
            return {
                scales: {
                    xAxes: [
                        {
                            type: 'linear',
                            position: 'bottom',
                            ticks: { beginAtZero: true },
                        },
                    ],
                    yAxes: [{ ticks: { beginAtZero: true } }],
                },
            }
        },
    },
}
</script>
