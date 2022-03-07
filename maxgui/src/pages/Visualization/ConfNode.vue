<template>
    <tree-graph-node
        v-if="!$typy(node, 'data').isEmptyObject"
        :node="node"
        :nodeWidth="nodeSize.width"
        :lineHeight="lineHeight"
        v-on="$listeners"
    >
        <template v-slot:node-heading>
            <div
                class="node-heading d-flex align-center flex-row px-3 py-1"
                :style="{ backgroundColor: headingColor.bg }"
            >
                <router-link
                    target="_blank"
                    :to="`/dashboard/${node.data.type}/${node.data.name}`"
                    class="text-truncate rsrc-link"
                    :style="{ color: headingColor.txt }"
                >
                    {{ node.data.name }}
                </router-link>
                <v-spacer />
                <span
                    class="node-type ml-2 font-weight-medium text-no-wrap text-uppercase"
                    :style="{ color: headingColor.txt }"
                >
                    {{ $help.resourceTxtTransform(node.data.type) }}
                </span>
            </div>
        </template>
        <template v-slot:node-body>
            <div
                v-for="(value, key) in nodeBody"
                :key="key"
                class="d-flex flex-row flex-grow-1"
                :style="{ lineHeight }"
            >
                <span class="mr-2 font-weight-bold text-capitalize">
                    {{ key }}
                </span>
                <truncate-string :text="`${value}`" />
            </div>
        </template>
    </tree-graph-node>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'

export default {
    name: 'conf-node',
    props: {
        node: { type: Object, required: true },
        nodeSize: { type: Object, required: true },
    },
    computed: {
        ...mapState({ RELATIONSHIP_TYPES: state => state.app_config.RELATIONSHIP_TYPES }),
        lineHeight() {
            return `18px`
        },
        headingColor() {
            const { SERVICES, SERVERS, MONITORS, FILTERS, LISTENERS } = this.RELATIONSHIP_TYPES
            switch (this.node.data.type) {
                case MONITORS:
                    return { bg: '#0E9BC0', txt: '#fff' }
                case SERVERS:
                    return { bg: '#e7eef1', txt: '#2d9cdb' }
                case SERVICES:
                    return { bg: '#7dd012', txt: '#fff' }
                case FILTERS:
                    return { bg: '#f59d34', txt: '#fff' }
                case LISTENERS:
                    return { bg: '#424f62', txt: '#fff' }
                default:
                    return { bg: '#e7eef1', txt: '#fff' }
            }
        },
        nodeData() {
            return this.node.data.nodeData
        },
        nodeBody() {
            const { SERVICES, SERVERS, MONITORS, FILTERS, LISTENERS } = this.RELATIONSHIP_TYPES
            switch (this.node.data.type) {
                case MONITORS: {
                    const { state, module } = this.nodeData.attributes
                    return { state, module }
                }
                case SERVERS: {
                    const {
                        state,
                        parameters,
                        statistics: { connections },
                    } = this.nodeData.attributes
                    let data = {
                        state,
                        connections,
                        [parameters.socket ? 'socket' : 'address']: this.$help.getAddress(
                            parameters
                        ),
                    }
                    return data
                }
                case SERVICES: {
                    const { state, router, total_connections } = this.nodeData.attributes
                    return {
                        state,
                        router,
                        'Total Connections': total_connections,
                    }
                }
                case FILTERS:
                    return { module: this.nodeData.attributes.module }
                case LISTENERS: {
                    const { state, parameters } = this.nodeData.attributes
                    return {
                        state,
                        address: this.$help.getAddress(parameters),
                        protocol: parameters.protocol,
                        authenticator: parameters.authenticator,
                    }
                }
                default:
                    return {}
            }
        },
    },
}
</script>
