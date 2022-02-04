<template>
    <details-page-title>
        <template v-slot:append>
            <div class="pl-6">
                <icon-sprite-sheet
                    size="13"
                    class="status-icon mr-1"
                    :frame="$help.monitorStateIcon(current_cluster.state)"
                >
                    status
                </icon-sprite-sheet>
                <span class="resource-state color text-navigation text-body-2">
                    {{ current_cluster.state }}
                </span>
                <span class="color text-field-text text-body-2">
                    |
                    <span class="resource-module">{{ current_cluster.module }}</span>
                </span>
            </div>
        </template>
    </details-page-title>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'

export default {
    name: 'cluster-page-header',
    computed: {
        ...mapState({
            current_cluster: state => state.visualization.current_cluster,
        }),
    },
}
</script>
