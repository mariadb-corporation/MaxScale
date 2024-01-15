/*
 * Copyright (c) 2023 MariaDB plc
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

export default {
    props: { opts: { type: Object, default: () => {} } },
    computed: {
        baseOpts() {
            return this.$helpers.lodash.merge(
                {
                    responsive: true,
                    maintainAspectRatio: false,
                    clip: false,
                    interaction: { mode: 'index', intersect: false },
                    plugins: {
                        legend: { display: false },
                        tooltip: {
                            mode: 'index',
                            enabled: false,
                            intersect: false,
                            position: 'mxsCursor',
                        },
                        /**
                         * chartjs-plugin-annotation is registered globally so annotation key must be defined,
                         * otherwise chartjs will have "Maximum call stack size exceeded" error
                         */
                        annotation: {},
                    },
                },
                this.opts
            )
        },
        chartInstance() {
            return this.$typy(this.$refs, 'wrapper.getCurrentChart').safeFunction()
        },
    },
}
