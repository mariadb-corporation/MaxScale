<template>
    <div ref="container" class="fill-height">
        <tree-graph v-if="ctrDim.height" :data="topology" :dim="ctrDim">
            <template v-slot:node-rect="{ data: { id } }">
                <div class="d-flex flex-column justify-center fill-height px-4 py-2 server-node">
                    <truncate-string :text="id" />
                </div>
            </template>
        </tree-graph>
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
import TreeGraph from './TreeGraph.vue'
export default {
    name: 'cluster',
    components: {
        'tree-graph': TreeGraph,
    },
    props: {
        cluster: { type: Object, required: true },
    },
    data() {
        return {
            ctrDim: {},
        }
    },
    created() {
        this.$nextTick(() => this.setCtrDim())
    },
    methods: {
        setCtrDim() {
            const { width, height } = this.$refs.container.getBoundingClientRect()
            this.ctrDim = { width, height }
        },
    },
}
</script>

<style lang="scss" scoped>
.server-node {
    font-size: 14px;
}
</style>
