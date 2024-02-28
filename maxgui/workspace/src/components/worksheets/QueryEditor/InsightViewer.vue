<template>
    <v-card class="insight-viewer fill-height" tile>
        <v-tabs v-model="activeSpec" :height="tabNavHeight" class="v-tabs--mariadb">
            <v-tab v-for="spec of specs" :key="spec" color="primary" :href="`#${spec}`">
                {{ spec }}
            </v-tab>
        </v-tabs>
        <div
            :style="{ height: `${tabItemDim.height}px` }"
            class="pt-2 px-5 mxs-field-text-size mxs-color-helper text-small-text"
        >
            <insight-viewer-tab-item
                v-if="queryTabConn.id && node.id"
                :dim="tabItemDim"
                :conn="queryTabConn"
                :node="node"
                :activeSpec="activeSpec"
                :specs="specs"
                :nodeType="nodeType"
            />
        </div>
    </v-card>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import InsightViewer from '@wsModels/InsightViewer'
import QueryConn from '@wsModels/QueryConn'
import InsightViewerTabItem from '@wkeComps/QueryEditor/InsightViewerTabItem'
import { NODE_TYPES, INSIGHT_SPECS } from '@wsSrc/constants'

export default {
    name: 'insight-viewer',
    components: { InsightViewerTabItem },
    props: {
        dim: { type: Object, required: true },
        queryTab: { type: Object, required: true },
    },
    computed: {
        insightViewer() {
            return InsightViewer.find(this.queryTab.id) || {}
        },
        activeSpec: {
            get() {
                return this.$typy(this.insightViewer, 'active_spec').safeString
            },
            set(v) {
                InsightViewer.update({ where: this.queryTab.id, data: { active_spec: v } })
            },
        },
        queryTabConn() {
            return QueryConn.getters('findQueryTabConn')(this.queryTab.id)
        },
        node() {
            return this.$typy(this.insightViewer, 'active_node').safeObjectOrEmpty
        },
        nodeType() {
            return this.$typy(this.node, 'type').safeString
        },
        specs() {
            const { SCHEMA, TBL, VIEW, TRIGGER, SP, FN } = NODE_TYPES
            switch (this.nodeType) {
                case SCHEMA:
                    return this.$helpers.lodash.pickBy(
                        INSIGHT_SPECS,
                        (value, key) => key !== 'CREATION_INFO'
                    )
                case TBL:
                    return this.$helpers.lodash.pick(INSIGHT_SPECS, [
                        'COLUMNS',
                        'INDEXES',
                        'TRIGGERS',
                        'DDL',
                    ])
                case VIEW:
                case TRIGGER:
                case SP:
                case FN:
                    return this.$helpers.lodash.pick(INSIGHT_SPECS, ['CREATION_INFO', 'DDL'])
                default:
                    return {}
            }
        },
        tabNavHeight() {
            return 24
        },
        tabItemDim() {
            // minus pt-2 px-5 padding
            return { width: this.dim.width - 40, height: this.dim.height - this.tabNavHeight - 16 }
        },
    },
}
</script>
<style lang="scss" scoped>
.insight-viewer {
    box-shadow: none !important;
}
</style>
