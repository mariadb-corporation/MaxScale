<template>
    <div ref="nodeWrapper" class="cluster-node-wrapper">
        <v-card outlined class="server-node" width="273">
            <div
                class="node-title-wrapper d-flex align-center flex-row px-2 py-1"
                :class="[droppableTargets.includes(node.id) ? 'server-node__droppable' : '']"
            >
                <icon-sprite-sheet
                    size="13"
                    class="server-state-icon mr-1"
                    :frame="$help.serverStateIcon($typy(node, 'data.state').safeString)"
                >
                    status
                </icon-sprite-sheet>
                <div class="text-truncate">
                    <router-link :to="`/dashboard/servers/${node.id}`" class="rsrc-link">
                        {{ $typy(node, 'data.title').safeString }}
                    </router-link>
                </div>
                <v-spacer />
                <span class="readonly-val ml-1 color text-field-text font-weight-medium">
                    {{ $typy(node, 'data.readonly').safeBoolean ? $t('readonly') : $t('writable') }}
                </span>
                <div class="ml-1 button-container">
                    <v-menu
                        transition="slide-y-transition"
                        offset-y
                        nudge-left="100%"
                        content-class="mariadb-select-v-menu mariadb-select-v-menu--full-border"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn small class="gear-btn" icon v-on="on">
                                <v-icon
                                    size="16"
                                    :color="
                                        droppableTargets.includes(node.id)
                                            ? 'background'
                                            : 'primary'
                                    "
                                >
                                    $vuetify.icons.settings
                                </v-icon>
                            </v-btn>
                        </template>
                        <!-- TODO: Render cluster node actions -->
                        <v-list class="color bg-color-background"> </v-list>
                    </v-menu>
                </div>
            </div>
            <v-divider />
            <div
                class="node-text-wrapper color text-navigation d-flex justify-center flex-column px-2 py-1"
            >
                <div
                    class="node-state d-flex flex-row flex-grow-1 text-capitalize"
                    :style="{ lineHeight }"
                >
                    <span class="sbm mr-2 font-weight-bold">
                        {{ $t('serverState') }}
                    </span>
                    <truncate-string :text="`${node.data.state}`" />
                    <v-spacer />
                    <span
                        v-if="
                            !$typy(node.data).isEmptyObject &&
                                !$typy(node, 'data.isMaster').safeBoolean
                        "
                        class="sbm ml-1"
                    >
                        <span class="font-weight-bold text-capitalize">
                            {{ $t('lag') }}
                        </span>
                        <span> {{ sbm }}s </span>
                    </span>
                </div>
                <div class="node-connections d-flex flex-grow-1" :style="{ lineHeight }">
                    <span class="text-capitalize font-weight-bold mr-2">
                        {{ $tc('connections', 2) }}
                    </span>
                    <span>{{ node.data.connections }} </span>
                </div>
                <v-expand-transition>
                    <div v-if="isExpanded" class="node-text--expanded-content">
                        <template v-if="!$typy(node, 'data.isMaster').safeBoolean">
                            <div
                                v-for="(value, key) in repStat"
                                :key="`${key}`"
                                class="d-flex"
                                :style="{ lineHeight }"
                            >
                                <span class="mr-2 font-weight-bold">
                                    {{ key }}
                                </span>
                                <truncate-string :text="`${value}`" />
                            </div>
                        </template>
                        <!-- TODO: Show more info here, could be a carousel to show different slide of info  -->
                    </div>
                </v-expand-transition>
            </div>
        </v-card>
        <div class="d-flex justify-center">
            <v-btn
                x-small
                height="16"
                class="arrow-toggle text-capitalize font-weight-medium px-2 color bg-background"
                depressed
                outlined
                color="#e3e6ea"
                @click="toggleExpand(node)"
            >
                <v-icon :class="[isExpanded ? 'arrow-up' : 'arrow-down']" size="20" color="primary">
                    $expand
                </v-icon>
            </v-btn>
        </div>
    </div>
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

export default {
    name: 'cluster-node',
    props: {
        node: { type: Object, required: true },
        droppableTargets: { type: Array, required: true },
    },
    data() {
        return {
            isExpanded: false,
        }
    },
    computed: {
        lineHeight() {
            return `18px`
        },
        sbm() {
            return this.$help.getMin({
                arr: this.node.data.server_info.slave_connections,
                pickBy: 'seconds_behind_master',
            })
        },
        repStat() {
            return {
                'Slave IO Running': this.$help.getMostFreq({
                    arr: this.node.data.server_info.slave_connections,
                    pickBy: 'slave_io_running',
                }),

                'Slave SQL Running': this.$help.getMostFreq({
                    arr: this.node.data.server_info.slave_connections,
                    pickBy: 'slave_sql_running',
                }),
            }
        },
    },
    methods: {
        toggleExpand(node) {
            //TODO: get current height and height after expanding and pass to get-expanded-node evt
            this.isExpanded = !this.isExpanded
            this.$emit('get-expanded-node', node.id)
        },
    },
}
</script>

<style lang="scss" scoped>
.server-node {
    font-size: 12px;
    &__droppable {
        background: $success;
        color: $background;
        .rsrc-link {
            color: $background;
        }
        .readonly-val {
            color: $background !important;
        }
    }
}
</style>
