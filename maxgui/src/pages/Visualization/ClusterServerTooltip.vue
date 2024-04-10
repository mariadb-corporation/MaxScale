<template>
    <v-tooltip top color="deep-ocean" :open-delay="200">
        <template v-slot:activator="{ on }">
            <slot name="activator" :on="on" />
        </template>
        <div v-for="server in servers" :key="server.id" class="d-flex align-center">
            <status-icon
                size="16"
                class="server-state-icon mr-1"
                :type="$typy(server, 'serverData.type').safeString"
                :value="$typy(server, 'serverData.attributes.state').safeString"
            />
            {{ server.id }}
            <span class="ml-1 mxs-color-helper text-text-subtle">
                {{ $mxs_t('uptime') }}
                {{
                    $helpers.uptimeHumanize(
                        $typy(server, 'serverData.attributes.uptime').safeNumber
                    )
                }}
            </span>
        </div>
    </v-tooltip>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'cluster-server-tooltip',
    props: {
        servers: { type: Array, required: true },
    },
}
</script>

<style lang="scss" scoped>
.cluster-server-tooltip {
    border-radius: 4px;
}
</style>
