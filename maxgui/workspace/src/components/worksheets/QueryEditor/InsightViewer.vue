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
            <keep-alive>
                <insight-viewer-tab-item
                    v-if="activeConn.id && activeNode.id"
                    :dim="tabItemDim"
                    :conn="activeConn"
                    :node="activeNode"
                    :activeSpec="activeSpec"
                    :specs="specs"
                    :nodeType="nodeType"
                />
            </keep-alive>
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
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import InsightViewer from '@wsModels/InsightViewer'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import InsightViewerTabItem from '@wkeComps/QueryEditor/InsightViewerTabItem'

export default {
    name: 'insight-viewer',
    components: { InsightViewerTabItem },
    props: {
        dim: { type: Object, required: true },
    },
    computed: {
        ...mapState({
            NODE_TYPES: state => state.mxsWorkspace.config.NODE_TYPES,
            INSIGHT_SPECS: state => state.mxsWorkspace.config.INSIGHT_SPECS,
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
        activeConn() {
            return QueryConn.getters('activeQueryTabConn')
        },
        activeNode() {
            return InsightViewer.getters('activeNode')
        },
        nodeType() {
            return this.$typy(this.activeNode, 'type').safeString
        },
        specs() {
            const { SCHEMA, TBL, VIEW, TRIGGER, SP, FN } = this.NODE_TYPES
            switch (this.nodeType) {
                case SCHEMA:
                    return this.$helpers.lodash.pickBy(
                        this.INSIGHT_SPECS,
                        (value, key) => key !== 'CREATION_INFO'
                    )
                case TBL:
                    return this.$helpers.lodash.pick(this.INSIGHT_SPECS, [
                        'COLUMNS',
                        'INDEXES',
                        'TRIGGERS',
                        'DDL',
                    ])
                case VIEW:
                case TRIGGER:
                case SP:
                case FN:
                    return this.$helpers.lodash.pick(this.INSIGHT_SPECS, ['CREATION_INFO', 'DDL'])
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
