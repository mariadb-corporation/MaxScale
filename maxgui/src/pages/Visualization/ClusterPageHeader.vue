<template>
    <details-page-title :showSearch="false" :showCreateRscBtn="false">
        <template v-slot:setting-menu>
            <details-icon-group-wrapper multiIcons>
                <template v-slot:body>
                    <v-tooltip
                        v-for="op in [
                            monitorOps[MONITOR_OP_TYPES.STOP],
                            monitorOps[MONITOR_OP_TYPES.START],
                        ]"
                        :key="op.text"
                        bottom
                        transition="slide-y-transition"
                        content-class="shadow-drop color text-navigation py-1 px-4"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn
                                :class="`${op.type}-btn`"
                                text
                                :color="op.color"
                                :disabled="op.disabled"
                                v-on="on"
                                @click="$emit('on-choose-op', { op, target: current_cluster })"
                            >
                                <v-icon :size="op.iconSize"> {{ op.icon }} </v-icon>
                            </v-btn>
                        </template>
                        <span>{{ op.text }} </span>
                    </v-tooltip>
                </template>
            </details-icon-group-wrapper>
        </template>
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
/*
@on-choose-op: { op:Object, target:Object }. Operation chosen and target object to dispatch update action
*/
import { mapState, mapGetters } from 'vuex'

export default {
    name: 'cluster-page-header',
    computed: {
        ...mapState({
            current_cluster: state => state.visualization.current_cluster,
            MONITOR_OP_TYPES: state => state.app_config.MONITOR_OP_TYPES,
        }),
        ...mapGetters({ getMonitorOps: 'monitor/getMonitorOps' }),
        monitorOps() {
            return this.getMonitorOps({ currState: this.current_cluster.state, scope: this })
        },
    },
}
</script>
