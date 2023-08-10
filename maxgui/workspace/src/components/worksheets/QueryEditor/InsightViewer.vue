<template>
    <v-card class="insight-viewer fill-height" tile>
        <v-tabs v-model="activeSpec" :height="24" class="v-tabs--mariadb">
            <v-tab v-for="spec of specs" :key="spec" color="primary" :href="`#${spec}`">
                {{ spec }}
            </v-tab>
        </v-tabs>
        <!-- TODO: Add tab spec item -->
    </v-card>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import InsightViewer from '@wsModels/InsightViewer'

import QueryEditor from '@wsModels/QueryEditor'

export default {
    name: 'insight-viewer',
    props: {
        dim: { type: Object, required: true },
    },
    computed: {
        ...mapState({
            NODE_TYPES: state => state.mxsWorkspace.config.NODE_TYPES,
            SCHEMA_INSIGHT_SPECS: state => state.mxsWorkspace.config.SCHEMA_INSIGHT_SPECS,
            TBL_INSIGHT_SPECS: state => state.mxsWorkspace.config.TBL_INSIGHT_SPECS,
        }),
        activeSpec: {
            get() {
                return InsightViewer.getters('activeSpec')
            },
            set(v) {
                InsightViewer.update({
                    where: QueryEditor.getters('activeQueryTabId'),
                    data: {
                        active_spec: v,
                    },
                })
            },
        },
        activeNode() {
            return InsightViewer.getters('activeNode')
        },
        nodeType() {
            return this.$typy(this.activeNode, 'type').safeString
        },
        specs() {
            if (this.nodeType === this.NODE_TYPES.SCHEMA) return this.SCHEMA_INSIGHT_SPECS
            return this.TBL_INSIGHT_SPECS
        },
    },
}
</script>
<style lang="scss" scoped>
.insight-viewer {
    box-shadow: none !important;
}
</style>
