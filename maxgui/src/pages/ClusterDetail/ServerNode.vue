<template>
    <tree-graph-node
        :node="node"
        :lineHeight="lineHeight"
        :bodyWrapperClass="bodyWrapperClass"
        :expandOnMount="expandOnMount"
        :extraInfoSlides="extraInfoSlides"
        v-on="$listeners"
    >
        <template v-slot:node-heading>
            <div
                class="node-heading d-flex align-center flex-row px-3 py-1"
                :class="{ 'node-heading__droppable': isDroppableNode }"
            >
                <v-icon
                    size="16"
                    class="mr-1 server-state-icon"
                    :color="isDroppableNode ? 'white' : isMaster ? 'navigation' : 'accent'"
                >
                    {{
                        isMaster
                            ? '$vuetify.icons.mxs_primaryServer'
                            : '$vuetify.icons.mxs_secondaryServer'
                    }}
                </v-icon>
                <router-link
                    target="_blank"
                    rel="noopener noreferrer"
                    :to="`/dashboard/servers/${node.id}`"
                    class="text-truncate rsrc-link"
                >
                    {{ node.id }}
                </router-link>
                <v-spacer />
                <span
                    class="readonly-val ml-1 mxs-color-helper text-grayed-out font-weight-medium text-no-wrap"
                >
                    {{ nodeAttrs.read_only ? $mxs_t('readonly') : $mxs_t('writable') }}
                </span>
                <div class="ml-1 button-container text-no-wrap">
                    <mxs-tooltip-btn
                        small
                        icon
                        :color="iconColor"
                        @click="runQueries({ type: 'servers', conn_name: node.id })"
                    >
                        <template v-slot:btn-content>
                            <v-icon size="16">
                                $vuetify.icons.mxs_workspace
                            </v-icon>
                        </template>
                        {{ $mxs_t('runQueries') }}
                    </mxs-tooltip-btn>
                    <v-menu
                        v-if="isAdmin"
                        transition="slide-y-transition"
                        offset-y
                        nudge-left="100%"
                        content-class="v-menu--mariadb v-menu--mariadb-full-border"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn small class="gear-btn" icon :color="iconColor" v-on="on">
                                <v-icon size="16">
                                    $vuetify.icons.mxs_settings
                                </v-icon>
                            </v-btn>
                        </template>
                        <v-list>
                            <v-list-item
                                v-for="(op, i) in nodeOps"
                                :key="i"
                                dense
                                link
                                :disabled="op.disabled"
                                class="px-2"
                                @click="$emit('on-choose-op', { op, target: node })"
                            >
                                <v-list-item-title class="mxs-color-helper text-text">
                                    <div class="d-inline-block text-center mr-2" style="width:22px">
                                        <v-icon
                                            v-if="op.icon"
                                            :color="op.color"
                                            :size="op.iconSize"
                                        >
                                            {{ op.icon }}
                                        </v-icon>
                                    </div>
                                    {{ op.text }}
                                </v-list-item-title>
                            </v-list-item>
                        </v-list>
                    </v-menu>
                </div>
            </div>
        </template>
        <template v-slot:node-body>
            <div class="d-flex flex-row flex-grow-1 text-capitalize" :style="{ lineHeight }">
                <span class="sbm mr-2 font-weight-bold">
                    {{ $mxs_t('state') }}
                </span>
                <status-icon
                    size="16"
                    class="mr-1 server-state-icon"
                    :type="MXS_OBJ_TYPES.SERVERS"
                    :value="nodeAttrs.state"
                />
                <mxs-truncate-str
                    autoID
                    :tooltipItem="{ txt: `${nodeAttrs.state}` }"
                    :maxWidth="150"
                />
                <v-spacer />
                <span v-if="!node.data.isMaster" class="ml-1">
                    <span class="font-weight-bold text-capitalize">{{ $mxs_t('lag') }} </span>
                    <span> {{ sbm }}s </span>
                </span>
            </div>
            <div class="d-flex flex-grow-1" :style="{ lineHeight }">
                <span class="text-capitalize font-weight-bold mr-2">
                    {{ $mxs_tc('connections', 2) }}
                </span>
                <span>{{ nodeAttrs.statistics.connections }} </span>
            </div>
            <div class="d-flex flex-grow-1" :style="{ lineHeight }">
                <span class="text-capitalize font-weight-bold mr-2">
                    {{ nodeAttrs.parameters.socket ? $mxs_t('socket') : $mxs_t('address') }}
                </span>
                <mxs-truncate-str
                    autoID
                    :tooltipItem="{ txt: `${$helpers.getAddress(nodeAttrs.parameters)}` }"
                />
            </div>
        </template>
    </tree-graph-node>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import { mapGetters, mapActions } from 'vuex'
import { SERVER_OP_TYPES } from '@src/constants'
import { MXS_OBJ_TYPES } from '@share/constants'

/*
@node-height: v: Number. Cluster node height. Emit from <tree-graph-node/>
@get-expanded-node: v: String. Id of expanded node. Emit from <tree-graph-node/>
@on-choose-op: { op:Object, target:Object }. Operation chosen and target object to dispatch update action
*/
export default {
    name: 'server-node',
    props: {
        node: { type: Object, required: true },
        droppableTargets: { type: Array, required: true },
        bodyWrapperClass: { type: String, default: '' },
        expandOnMount: { type: Boolean, default: false },
    },
    computed: {
        ...mapGetters({
            getCurrStateMode: 'server/getCurrStateMode',
            getServerOps: 'server/getServerOps',
            isAdmin: 'user/isAdmin',
        }),
        isDroppableNode() {
            return this.droppableTargets.includes(this.node.id)
        },
        iconColor() {
            return this.isDroppableNode ? 'white' : 'primary'
        },
        lineHeight() {
            return `18px`
        },
        nodeAttrs() {
            return this.node.data.serverData.attributes
        },
        currStateMode() {
            let stateMode = this.nodeAttrs.state.toLowerCase()
            if (stateMode.indexOf(',') > 0) {
                stateMode = stateMode.slice(0, stateMode.indexOf(','))
            }
            return stateMode
        },
        // only slave node has this property
        slave_connections() {
            return this.$typy(this.node.data, 'server_info.slave_connections').safeArray
        },
        sbm() {
            return this.$helpers.getMin({
                arr: this.slave_connections,
                pickBy: 'seconds_behind_master',
            })
        },
        firstSlideCommonInfo() {
            return {
                last_event: this.nodeAttrs.last_event,
                gtid_binlog_pos: this.nodeAttrs.gtid_binlog_pos,
                gtid_current_pos: this.nodeAttrs.gtid_current_pos,
            }
        },
        secondSlideCommonInfo() {
            return {
                uptime: this.$helpers.uptimeHumanize(this.nodeAttrs.uptime),
                version_string: this.nodeAttrs.version_string,
            }
        },
        masterExtraInfo() {
            return [
                {
                    ...this.firstSlideCommonInfo,
                },
                this.secondSlideCommonInfo,
            ]
        },
        slaveExtraInfo() {
            return [
                {
                    ...this.firstSlideCommonInfo,
                    slave_io_running: this.$helpers.getMostFreq({
                        arr: this.slave_connections,
                        pickBy: 'slave_io_running',
                    }),
                    slave_sql_running: this.$helpers.getMostFreq({
                        arr: this.slave_connections,
                        pickBy: 'slave_sql_running',
                    }),
                },
                this.secondSlideCommonInfo,
            ]
        },
        isMaster() {
            return this.node.data.isMaster
        },
        extraInfo() {
            if (this.isMaster) return this.masterExtraInfo
            else return this.slaveExtraInfo
        },
        extraInfoSlides() {
            return this.extraInfo
        },
        serverOps() {
            const { MAINTAIN, CLEAR, DRAIN } = SERVER_OP_TYPES
            const currStateMode = this.getCurrStateMode(this.nodeAttrs.state)
            const ops = this.getServerOps({ currStateMode, scope: this })
            return [ops[MAINTAIN], ops[CLEAR], ops[DRAIN]]
        },
        nodeOps() {
            return [...this.serverOps]
        },
    },
    created() {
        this.MXS_OBJ_TYPES = MXS_OBJ_TYPES
    },
    methods: {
        ...mapActions({ chooseQueryEditorWke: 'visualization/chooseQueryEditorWke' }),
        runQueries(param) {
            this.chooseQueryEditorWke(param)
            this.$router.push(`/workspace`)
        },
    },
}
</script>

<style lang="scss" scoped>
.node-heading {
    &__droppable {
        background: $success;
        color: white;
        .rsrc-link {
            color: white;
        }
        .readonly-val {
            color: white !important;
        }
    }
}
</style>
