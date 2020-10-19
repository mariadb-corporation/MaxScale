<template>
    <v-sheet class="d-flex mb-2">
        <outlined-overview-card
            v-for="(value, name) in getTopOverviewInfo"
            :key="name"
            wrapperClass="mt-0"
            cardClass="px-10"
        >
            <template v-slot:card-body>
                <span class="caption text-uppercase font-weight-bold color text-deep-ocean">
                    {{ name.replace('_', ' ') }}
                </span>

                <span class="text-no-wrap body-2">
                    {{ value }}
                </span>
            </template>
        </outlined-overview-card>
    </v-sheet>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
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
        getTopOverviewInfo: function() {
            /*
            Set fallback undefined value as string if properties doesnt exist
            This allows it to be render as text
          */
            const {
                attributes: {
                    monitor_diagnostics: { master, master_gtid_domain_id, state, primary } = {},
                } = {},
            } = this.currentMonitor

            const overviewInfo = {
                master,
                master_gtid_domain_id,
                state,
                primary,
            }
            Object.keys(overviewInfo).forEach(
                key => (overviewInfo[key] = this.$help.convertType(overviewInfo[key]))
            )
            return overviewInfo
        },
    },
}
</script>
