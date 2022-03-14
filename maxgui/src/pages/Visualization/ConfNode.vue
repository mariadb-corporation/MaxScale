<template>
    <v-card
        v-if="!$typy(node, 'data').isEmptyObject"
        outlined
        class="node-card fill-height"
        :width="nodeWidth - 2"
    >
        <div
            class="node-heading d-flex align-center flex-row px-3 py-1"
            :style="{ backgroundColor: headingColor.bg }"
        >
            <router-link
                target="_blank"
                :to="`/dashboard/${node.data.type}/${node.data.id}`"
                class="text-truncate rsrc-link"
                :style="{ color: headingColor.txt }"
            >
                {{ node.data.id }}
            </router-link>
            <v-spacer />
            <span
                class="node-type ml-2 font-weight-medium text-no-wrap text-uppercase"
                :style="{ color: headingColor.txt }"
            >
                {{ $help.resourceTxtTransform(node.data.type) }}
            </span>
        </div>
        <v-divider />
        <div class="color text-navigation d-flex justify-center flex-column px-3 py-1">
            <div
                v-for="(value, key) in nodeBody"
                :key="key"
                class="d-flex flex-row flex-grow-1 align-center"
                :style="{ lineHeight }"
            >
                <span class="mr-2 font-weight-bold text-capitalize">
                    {{ key }}
                </span>

                <icon-sprite-sheet
                    v-if="key === 'state'"
                    size="13"
                    class="status-icon mr-1"
                    :frame="stateIconFrame(value)"
                >
                    status
                </icon-sprite-sheet>

                <truncate-string :text="`${value}`" />
            </div>
        </div>
    </v-card>
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
        nodeWidth: { type: Number, required: true },
    },
    computed: {
        ...mapState({ RELATIONSHIP_TYPES: state => state.app_config.RELATIONSHIP_TYPES }),
        lineHeight() {
            return `18px`
        },
        headingColor() {
            const { SERVICES, SERVERS, MONITORS, FILTERS, LISTENERS } = this.RELATIONSHIP_TYPES
            switch (this.nodeType) {
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
        nodeType() {
            return this.node.data.type
        },
        nodeBody() {
            const { SERVICES, SERVERS, MONITORS, FILTERS, LISTENERS } = this.RELATIONSHIP_TYPES
            switch (this.nodeType) {
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
    methods: {
        stateIconFrame(value) {
            const { SERVICES, SERVERS, MONITORS, LISTENERS } = this.RELATIONSHIP_TYPES
            switch (this.nodeType) {
                case MONITORS:
                    return this.$help.monitorStateIcon(value)
                case SERVERS:
                    return this.$help.serverStateIcon(value)
                case SERVICES:
                    return this.$help.serviceStateIcon(value)
                case LISTENERS:
                    return this.$help.listenerStateIcon(value)
            }
        },
    },
}
</script>

<style lang="scss" scoped>
.node-card {
    font-size: 12px;
}
</style>
