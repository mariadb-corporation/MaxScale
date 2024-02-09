<template>
    <v-card
        outlined
        class="node-card fill-height relative"
        :style="{ borderColor: headingColor.bg }"
    >
        <div
            class="node-heading d-flex align-center justify-center flex-row px-3 py-1"
            :style="{ backgroundColor: headingColor.bg }"
        >
            <!-- Don't render service id here if the filter nodes are visualizing, render it in the body
                 that has filters nodes point to the service node.
            -->
            <template v-if="!isShowingFilterNodes">
                <router-link
                    target="_blank"
                    rel="noopener noreferrer"
                    :to="`/dashboard/${nodeType}/${node.id}`"
                    class="text-truncate mr-2"
                    :style="{ color: headingColor.txt }"
                >
                    {{ node.id }}
                </router-link>
                <v-spacer />
            </template>
            <span
                class="node-type font-weight-medium text-no-wrap text-uppercase"
                :style="{ color: headingColor.txt }"
            >
                {{ $mxs_tc(nodeType, 1) }}
            </span>
        </div>
        <filter-nodes
            v-if="showFiltersInService && isServiceWithFiltersNode"
            v-model="isVisualizingFilters"
            :filters="filters"
            :handleVisFilters="handleVisFilters"
            :style="{ width: `${nodeWidth}px` }"
        />
        <div
            class="mxs-color-helper text-navigation d-flex justify-center flex-column px-3 py-1"
            :class="{ 'mx-5': isShowingFilterNodes }"
            :style="{ border: isShowingFilterNodes ? `1px solid ${headingColor.bg}` : 'unset' }"
        >
            <template v-if="isShowingFilterNodes">
                <router-link
                    target="_blank"
                    rel="noopener noreferrer"
                    :to="`/dashboard/${node.type}/${node.id}`"
                    class="text-truncate "
                >
                    {{ node.id }}
                </router-link>
            </template>
            <div
                v-for="(value, key) in nodeBody"
                :key="key"
                class="d-flex flex-row flex-grow-1 align-center"
                :style="{ lineHeight }"
            >
                <span class="mr-2 font-weight-bold text-capitalize text-no-wrap">
                    {{ key }}
                </span>

                <status-icon
                    v-if="key === 'state'"
                    size="13"
                    class="state-icon mr-1"
                    :type="nodeType"
                    :value="value"
                />
                <mxs-truncate-str :tooltipItem="{ txt: `${value}` }" />
                <mxs-tooltip-btn
                    v-if="key === 'filters'"
                    btnClass="ml-auto vis-filter-btn"
                    x-small
                    icon
                    depressed
                    color="primary"
                    @click="handleVisFilters"
                >
                    <template v-slot:btn-content>
                        <v-icon size="14">$vuetify.icons.mxs_reports </v-icon>
                    </template>
                    {{ $mxs_t('visFilters') }}
                </mxs-tooltip-btn>
            </div>
        </div>
    </v-card>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters } from 'vuex'
import FilterNodes from '@src/components/ConfNode/FilterNodes'
import { MXS_OBJ_TYPES } from '@share/constants'

export default {
    name: 'conf-node',
    components: { FilterNodes },
    props: {
        node: { type: Object, required: true },
        nodeWidth: { type: Number, default: 200 },
        onNodeResized: { type: Function, default: () => null },
        showFiltersInService: { type: Boolean, default: false },
    },
    data() {
        return {
            isVisualizingFilters: false,
        }
    },
    computed: {
        ...mapGetters({ getAllFiltersMap: 'filter/getAllFiltersMap' }),
        lineHeight() {
            return `18px`
        },
        headingColor() {
            const { MONITORS, SERVERS, SERVICES, FILTERS, LISTENERS } = MXS_OBJ_TYPES
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
            return this.node.nodeData
        },
        nodeType() {
            return this.node.type
        },
        // for node type SERVICES
        filters() {
            return this.$typy(this.node.nodeData, 'relationships.filters.data').safeArray
        },
        isServiceWithFiltersNode() {
            return this.nodeType === MXS_OBJ_TYPES.SERVICES && Boolean(this.filters.length)
        },
        isShowingFilterNodes() {
            return (
                this.showFiltersInService &&
                this.isServiceWithFiltersNode &&
                this.isVisualizingFilters
            )
        },
        nodeBody() {
            const { SERVICES, SERVERS, MONITORS, FILTERS, LISTENERS } = MXS_OBJ_TYPES
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
                        [parameters.socket ? 'socket' : 'address']: this.$helpers.getAddress(
                            parameters
                        ),
                    }
                    return data
                }
                case SERVICES: {
                    const { state, router, total_connections } = this.nodeData.attributes
                    let body = {
                        state,
                        router,
                        'Total Connections': total_connections,
                    }
                    if (!this.isVisualizingFilters && this.filters.length)
                        body.filters = this.filters.map(f => f.id).join(', ')
                    return body
                }
                case FILTERS:
                    return { module: this.nodeData.attributes.module }
                case LISTENERS: {
                    const { state, parameters } = this.nodeData.attributes
                    return {
                        state,
                        address: this.$helpers.getAddress(parameters),
                        protocol: parameters.protocol,
                        authenticator: parameters.authenticator,
                    }
                }
                default:
                    return {}
            }
        },
    },
    mounted() {
        if (this.filters.length < 4) this.isVisualizingFilters = true
    },
    methods: {
        handleVisFilters() {
            this.isVisualizingFilters = !this.isVisualizingFilters
            this.onNodeResized(this.node.id)
        },
    },
}
</script>

<style lang="scss" scoped>
.node-card {
    font-size: 12px;
}
</style>
