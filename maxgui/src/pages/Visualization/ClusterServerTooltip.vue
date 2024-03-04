<template>
    <v-tooltip
        top
        content-class="shadow-drop py-1 px-4 cluster-server-tooltip"
        color="deep-ocean"
        :open-delay="200"
    >
        <template v-slot:activator="{ on }">
            <slot name="activator" :on="on" />
        </template>
        <div v-for="server in servers" :key="server.id" class="d-flex align-center">
            <icon-sprite-sheet
                size="16"
                class="server-state-icon mr-1"
                :frame="
                    $helpers.serverStateIcon(
                        $typy(server, 'serverData.attributes.state').safeString
                    )
                "
            >
                servers
            </icon-sprite-sheet>
            <span>{{ server.id }}</span>
            <span class="ml-1 mxs-color-helper text-text-subtle">
                {{ $mxs_t('uptime') }}
                {{
                    [$typy(server, 'serverData.attributes.uptime').safeNumber, 'seconds']
                        | duration('format', 'Y [years] M [months] D [days] h [hours] m [minutes]')
                }}
            </span>
        </div>
    </v-tooltip>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
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
