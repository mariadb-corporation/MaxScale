<template>
    <v-sheet class="d-flex mb-2">
        <template v-for="(value, name) in getTopOverviewInfo">
            <outlined-overview-card :key="name" cardWrapper="mt-0" cardClass="px-10">
                <template v-slot:card-body>
                    <span class="caption text-uppercase font-weight-bold color text-deep-ocean">
                        {{ name.replace('_', ' ') }}
                    </span>

                    <span class="text-no-wrap body-2">
                        {{ value }}
                    </span>
                </template>
            </outlined-overview-card>
        </template>
    </v-sheet>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    name: 'overview-header',

    props: {
        currentMonitor: { type: Object, required: true },
    },

    computed: {
        /**
         * @return {Object} return overviewInfo object
         */
        getTopOverviewInfo: function() {
            let self = this
            let currentMonitor = self.$help.lodash.cloneDeep(self.currentMonitor)
            let overviewInfo = {}
            if (!self.$help.lodash.isEmpty(currentMonitor)) {
                // Set fallback undefined value if properties doesnt exist
                const {
                    attributes: {
                        monitor_diagnostics: { master, master_gtid_domain_id, state, primary } = {},
                    } = {},
                } = currentMonitor

                overviewInfo = {
                    master: master,
                    master_gtid_domain_id: master_gtid_domain_id,
                    state: state,
                    primary: primary,
                }

                Object.keys(overviewInfo).forEach(
                    key => (overviewInfo[key] = self.$help.handleValue(overviewInfo[key]))
                )
            }
            return overviewInfo
        },
    },
}
</script>
