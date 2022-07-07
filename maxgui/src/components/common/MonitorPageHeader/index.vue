<template>
    <details-page-title>
        <!-- Pass on all named slots -->
        <template v-for="(_, slot) in $scopedSlots" v-slot:[slot]="props">
            <slot :name="slot" v-bind="props" />
        </template>
        <template v-slot:setting-menu>
            <v-list class="color bg-color-background py-0">
                <template v-for="(op, i) in monitorOps">
                    <v-divider v-if="op.divider" :key="`divider-${i}`" />
                    <v-list-item
                        v-else
                        :key="op.text"
                        dense
                        link
                        :disabled="op.disabled"
                        class="px-2"
                        :class="`${op.type}-op`"
                        @click="$emit('on-choose-op', op)"
                    >
                        <v-list-item-title
                            class="d-flex color text-text align-center op-item font-weight-regular"
                            :class="{ 'op-item--disabled': op.disabled }"
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
                    <refresh-rate v-model="refreshRate" v-on="$listeners" />
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
                    :frame="$help.monitorStateIcon(state)"
                >
                    monitors
                </icon-sprite-sheet>
                <span class="resource-state color text-navigation text-body-2">
                    {{ state }}
                </span>
                <span class="color text-field-text text-body-2">
                    |
                    <span class="resource-module">{{ monitorModule }}</span>
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
@on-choose-op: op:Object. Operation chosen to dispatch update action
@on-count-done. Emit event after amount of time from <refresh-rate/>
*/
import { mapState, mapGetters } from 'vuex'
import refreshRate from 'mixins/refreshRate'
export default {
    name: 'monitor-page-header',
    mixins: [refreshRate],
    props: {
        targetMonitor: { type: Object, required: true },
    },
    computed: {
        ...mapState({
            MONITOR_OP_TYPES: state => state.app_config.MONITOR_OP_TYPES,
            RESOURCE_FORM_TYPES: state => state.app_config.RESOURCE_FORM_TYPES,
            RELATIONSHIP_TYPES: state => state.app_config.RELATIONSHIP_TYPES,
        }),
        ...mapGetters({ getMonitorOps: 'monitor/getMonitorOps' }),
        state() {
            return this.$typy(this.targetMonitor, 'attributes.state').safeString
        },
        monitorModule() {
            return this.$typy(this.targetMonitor, 'attributes.module').safeString
        },
        allOps() {
            return this.getMonitorOps({ currState: this.state, scope: this })
        },
        monitorOps() {
            const {
                attributes: { monitor_diagnostics: { primary = false } = {}, parameters = {} } = {},
            } = this.targetMonitor
            const {
                STOP,
                START,
                DESTROY,
                RESET_REP,
                RELEASE_LOCKS,
                FAILOVER,
            } = this.MONITOR_OP_TYPES
            let ops = [this.allOps[STOP], this.allOps[START], this.allOps[DESTROY]]
            if (this.monitorModule === 'mariadbmon') {
                ops = [...ops, { divider: true }, this.allOps[RESET_REP]]
                // only add the release_locks option when this cluster is a primary one
                if (primary) ops.push(this.allOps[RELEASE_LOCKS])
                // only add the failover option when auto_failover is false
                if (!this.$typy(parameters, 'auto_failover').safeBoolean)
                    ops.push(this.allOps[FAILOVER])
            }
            //TODO: Detect whether the monitor is monitoring a ColumnStore cluster and and its operation here
            return ops
        },
    },
}
</script>

<style lang="scss" scoped>
::v-deep.op-item {
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
