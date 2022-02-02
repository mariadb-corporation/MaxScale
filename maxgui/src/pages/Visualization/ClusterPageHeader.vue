<template>
    <portal to="page-header">
        <div class="d-flex align-center">
            <v-btn class="ml-n4" icon @click="goBack">
                <v-icon class="mr-1" style="transform:rotate(90deg)" size="28" color="deep-ocean">
                    $vuetify.icons.arrowDown
                </v-icon>
            </v-btn>
            <div class="d-inline-flex align-center">
                <truncate-string :text="$route.params.id" :maxWidth="600">
                    <span
                        style="line-height: normal;"
                        class="ml-1 mb-0 color text-navigation text-h4 page-title"
                    >
                        {{ $route.params.id }}
                    </span>
                </truncate-string>
            </div>
        </div>
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
    </portal>
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
import goBack from 'mixins/goBack'

export default {
    name: 'cluster-page-header',
    mixins: [goBack],
    computed: {
        ...mapState({
            current_cluster: state => state.visualization.current_cluster,
        }),
    },
}
</script>
