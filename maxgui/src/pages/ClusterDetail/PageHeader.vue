<template>
    <details-page-title>
        <template v-slot:page-title="{ pageId }">
            <router-link :to="`/dashboard/monitors/${pageId}`" class="rsrc-link">
                {{ pageId }}
            </router-link>
        </template>
        <template v-slot:setting-menu>
            <v-list class="color bg-color-background py-0">
                <template v-for="(op, i) in clusterOps">
                    <v-divider v-if="op.divider" :key="`divider-${i}`" />
                    <v-list-item
                        v-else
                        :key="op.text"
                        dense
                        link
                        :disabled="op.disabled"
                        class="px-2"
                        @click="$emit('on-choose-op', { op, target: current_cluster })"
                    >
                        <v-list-item-title
                            class="d-flex color text-text align-center node-op-item font-weight-regular"
                            :class="{ 'node-op-item--disabled': op.disabled }"
                        >
                            <div class="d-inline-block text-center mr-2" style="width:24px">
                                <v-icon
                                    v-if="op.icon"
                                    class="node-op-item__icon"
                                    :color="op.color"
                                    :size="op.iconSize"
                                >
                                    {{ op.icon }}
                                </v-icon>
                            </div>
                            <span class="node-op-item__text">{{ op.text }}</span>
                        </v-list-item-title>
                    </v-list-item>
                </template>
            </v-list>
        </template>
        <template v-slot:append>
            <portal to="page-header--right">
                <div class="d-flex align-center fill-height">
                    <refresh-rate :defRefreshRate="60" v-on="$listeners" />
                    <create-resource
                        class="ml-2 d-inline-block"
                        :defFormType="RESOURCE_FORM_TYPES.SERVER"
                        :defRelationshipObj="{
                            id: $route.params.id,
                            type: RELATIONSHIP_TYPES.MONITORS,
                        }"
                    />
                </div>
            </portal>
            <div class="pl-6">
                <icon-sprite-sheet
                    size="16"
                    class="monitor-state-icon mr-1"
                    :frame="$help.monitorStateIcon(current_cluster.state)"
                >
                    monitors
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
@on-count-done. Emit event after amount of time from <refresh-rate/>
*/
import { mapState, mapGetters } from 'vuex'

export default {
    name: 'page-header',
    computed: {
        ...mapState({
            current_cluster: state => state.visualization.current_cluster,
            MONITOR_OP_TYPES: state => state.app_config.MONITOR_OP_TYPES,
            RESOURCE_FORM_TYPES: state => state.app_config.RESOURCE_FORM_TYPES,
            RELATIONSHIP_TYPES: state => state.app_config.RELATIONSHIP_TYPES,
        }),
        ...mapGetters({ getMonitorOps: 'monitor/getMonitorOps' }),
        monitorOps() {
            return this.getMonitorOps({ currState: this.current_cluster.state, scope: this })
        },
        clusterOps() {
            const {
                monitorData: {
                    monitor_diagnostics: { primary = false } = {},
                    parameters = {},
                } = {},
            } = this.current_cluster
            let ops = [
                this.monitorOps[this.MONITOR_OP_TYPES.STOP],
                this.monitorOps[this.MONITOR_OP_TYPES.START],
                { divider: true },
                this.monitorOps[this.MONITOR_OP_TYPES.RESET_REP],
            ]
            // only add the release_locks option when this cluster is a primary one
            if (primary) ops.push(this.monitorOps[this.MONITOR_OP_TYPES.RELEASE_LOCKS])
            // only add the failover option when auto_failover is false
            if (!this.$typy(parameters, 'auto_failover').safeBoolean)
                ops.push(this.monitorOps[this.MONITOR_OP_TYPES.FAILOVER])
            return ops
        },
    },
}
</script>

<style lang="scss" scoped>
::v-deep.node-op-item {
    &--disabled {
        .node-op-item__icon {
            svg {
                color: rgba(0, 0, 0, 0.26) !important;
            }
        }
        .node-op-item__text {
            color: rgba(0, 0, 0, 0.26) !important;
        }
    }
}
</style>
