<template>
    <page-wrapper v-resize="setPageContentDim">
        <page-header ref="pageHeader" />
        <v-sheet
            :style="{
                height: 'calc(100% - 38px)', // minus page header
            }"
        >
            <div ref="pageContent" class="fill-height">
                <log-header
                    ref="logHeader"
                    class="pb-4 pt-2"
                    @get-chosen-log-levels="chosenLogLevels = $event"
                />
                <div v-if="logViewHeight" class="log-lines-container pa-4 color bg-reflection">
                    <v-btn
                        v-if="isNotifShown"
                        class="pa-2 new-log-btn font-weight-medium px-7 text-capitalize"
                        small
                        height="36"
                        color="primary"
                        rounded
                        depressed
                        @click="scrollToBottom"
                    >
                        {{ $t('newMessagesAvailable') }}!
                        <v-icon class="arrow-down" size="32">
                            $expand
                        </v-icon>
                    </v-btn>

                    <log-container
                        ref="logContainer"
                        :logViewHeight="logViewHeight"
                        :chosenLogLevels="chosenLogLevels"
                        @is-notif-shown="isNotifShown = $event"
                    />
                </div>
            </div>
        </v-sheet>
    </page-wrapper>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions } from 'vuex'
import PageHeader from './PageHeader'
import LogHeader from './LogHeader'
import LogContainer from './LogContainer.vue'

export default {
    name: 'settings',
    components: {
        PageHeader,
        LogHeader,
        LogContainer,
    },
    data() {
        return {
            logViewHeight: 0,
            isNotifShown: false,
            chosenLogLevels: [],
        }
    },
    async created() {
        await this.fetchMaxScaleOverviewInfo()
    },
    methods: {
        ...mapActions({
            fetchMaxScaleOverviewInfo: 'maxscale/fetchMaxScaleOverviewInfo',
        }),
        setPageContentDim() {
            this.$nextTick(() => {
                const pageContentHeight =
                    this.$typy(this.$refs, 'pageContent.clientHeight').safeNumber -
                    this.$typy(this.$refs, 'logHeader.$el.clientHeight').safeNumber
                // 32 is vertical padding in px
                if (pageContentHeight) this.logViewHeight = pageContentHeight - 32
            })
        },

        scrollToBottom() {
            this.$refs.logContainer.toBottom()
            // hide notification
            this.isNotifShown = false
        },
    },
}
</script>

<style lang="scss" scoped>
.new-log-btn {
    position: absolute;
    right: 50%;
    transform: translateX(50%);
    bottom: 6%;
    z-index: 1;
}
</style>
