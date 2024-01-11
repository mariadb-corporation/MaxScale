<template>
    <page-wrapper v-resize="setPageContentDim" fluid class="fill-height">
        <page-header ref="pageHeader" />
        <v-sheet
            :style="{
                height: 'calc(100% - 38px)', // minus page header
            }"
        >
            <div ref="pageContent" class="fill-height">
                <log-header ref="logHeader" class="pb-2" />
                <div
                    v-if="logViewHeight"
                    class="log-lines-container pa-4 mxs-color-helper bg-separator"
                >
                    <log-container :logViewHeight="logViewHeight" />
                </div>
            </div>
        </v-sheet>
    </page-wrapper>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import PageHeader from './PageHeader'
import LogHeader from './LogHeader'
import LogContainer from './LogContainer.vue'

export default {
    name: 'logs',
    components: {
        PageHeader,
        LogHeader,
        LogContainer,
    },
    data() {
        return {
            logViewHeight: 0,
            isNotifShown: false,
        }
    },
    methods: {
        setPageContentDim() {
            this.$nextTick(() => {
                const pageContentHeight =
                    this.$typy(this.$refs, 'pageContent.clientHeight').safeNumber -
                    this.$typy(this.$refs, 'logHeader.$el.clientHeight').safeNumber
                // 32 is vertical padding in px
                if (pageContentHeight) this.logViewHeight = pageContentHeight - 32
            })
        },
    },
}
</script>
