<template>
    <v-card outlined class="server-node" width="273" height="88">
        <div
            class="d-flex align-center flex-row node-title-wrapper px-2 py-1"
            :class="[droppableTargets.includes(node.id) ? 'server-node__droppable' : '']"
        >
            <icon-sprite-sheet
                size="13"
                class="mr-1 server-state-icon"
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
            <div class="button-container">
                <!--TODO: open a dialog to config the node -->
                <v-btn small class="ml-2 gear-btn" icon>
                    <v-icon
                        size="16"
                        :color="droppableTargets.includes(node.id) ? 'background' : 'primary'"
                    >
                        $vuetify.icons.settings
                    </v-icon>
                </v-btn>
            </div>
        </div>
        <v-divider />
        <div class="d-flex justify-center flex-column node-text-wrapper px-2 py-1">
            <div class="d-flex flex-row flex-grow-1">
                <span>{{ node.data.state }}</span>
                <v-spacer />
                <v-tooltip
                    top
                    transition="slide-y-transition"
                    content-class="shadow-drop color text-navigation py-1 px-4"
                >
                    <template v-slot:activator="{ on }">
                        <span
                            v-if="
                                !$typy(node.data).isEmptyObject &&
                                    !$typy(node, 'data.isMaster').safeBoolean
                            "
                            class="ml-1 color text-field-text"
                            v-on="on"
                        >
                            (+{{ sbm }}s)
                        </span>
                    </template>
                    <span>
                        <!-- TODO: Show Replication status by re-using rep-tooltip -->
                        {{ $t('repLag') }}
                    </span>
                </v-tooltip>
            </div>
            <div class="d-flex flex-grow-1">
                <span class="text-capitalize">{{ $tc('connections', 2) }}:</span>
                <span class="ml-1">
                    {{ node.data.connections }}
                </span>
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
    computed: {
        sbm() {
            return this.$help.getOverallRepStat({
                repStats: this.node.data.server_info.slave_connections,
                pickBy: 'seconds_behind_master',
                isNumber: true,
            })
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
    }
}
</style>
