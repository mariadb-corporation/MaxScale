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
                        @click="clickToBottom"
                    >
                        {{ $t('newMessagesAvailable') }}!
                        <v-icon class="arrow-down" size="32">
                            $expand
                        </v-icon>
                    </v-btn>

                    <v-card
                        id="scrollable-wrapper"
                        ref="scrollableWrapper"
                        :tile="false"
                        class="overflow-y-auto color no-border bg-reflection"
                        :min-height="logViewHeight"
                        :max-height="logViewHeight"
                        outlined
                        :loading="isLoading"
                    >
                        <template slot="progress">
                            <div class="d-flex flex-column align-center justify-center mt-2">
                                <v-progress-circular color="primary" indeterminate size="24" />
                                <p class="mt-1 loading-logs-text-indicator color text-field-text">
                                    {{ $t('loadingLogs') }}...
                                </p>
                            </div>
                        </template>

                        <div ref="scrollableContent" v-scroll:#scrollable-wrapper="onScroll">
                            <log-lines
                                :isLoading="isLoading"
                                :isFiltering="isFiltering"
                                :allLogData="allLogData"
                                :filteredLog="filteredLog"
                            />
                            <div ref="bottomLogLine" />
                        </div>
                    </v-card>
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
import { mapActions, mapMutations, mapState } from 'vuex'
import PageHeader from './PageHeader'
import LogLines from './LogLines'
import LogHeader from './LogHeader'

export default {
    name: 'settings',
    components: {
        PageHeader,
        LogLines,
        LogHeader,
    },
    data() {
        return {
            logViewHeight: 0,
            isLoading: false,
            allLogData: [],
            filteredLog: [],
            prevLogData: [],
            connection: null,
            isNotifShown: false,
            isAtBottom: false,
            chosenLogLevels: [],
        }
    },
    computed: {
        ...mapState({
            latest_logs: state => state.maxscale.latest_logs,
            prev_log_link: state => state.maxscale.prev_log_link,
            prev_log_data: state => state.maxscale.prev_log_data,
            prev_filtered_log_link: state => state.maxscale.prev_filtered_log_link,
            prev_filtered_log_data: state => state.maxscale.prev_filtered_log_data,
        }),
        isFiltering: function() {
            return this.chosenLogLevels.length > 0
        },
    },
    watch: {
        isAtBottom: function(val) {
            // Turn off notif if scroll position is at bottom already
            if (val) this.isNotifShown = false
        },
        isFiltering: async function(val) {
            if (val) {
                this.isLoading = true
                // filtered all logs in the view
                const filteredAllLogData = this.allLogData.filter(log =>
                    this.chosenLogLevels.includes(log.priority)
                )
                // filtered all logs in the view plus prefetched prevLogData
                this.filteredLog = Object.freeze([
                    ...this.prevLogData.filter(log => this.chosenLogLevels.includes(log.priority)),
                    ...filteredAllLogData,
                ])

                await this.$nextTick(async () => {
                    /* TODO: instead of looping fetching older logs until scroll is
                     * scrollable, there should be a way to filter older logs in maxscale
                     * end and returns 50 lines so that div is scrollable.
                     */
                    /* Needs to delay 350ms to get accurate value from this.isScrollable()
                     * because log-lines component is wrappered with transition-group
                     * and the animation is set for 300ms.
                     * */
                    await this.$help.delay(350)
                    if (!this.isScrollable()) {
                        await this.handleUnionPrevFilteredLogs()
                        while (!this.isScrollable() && this.prev_filtered_log_link) {
                            await this.handleUnionPrevFilteredLogs()
                        }
                    }
                    this.isLoading = false
                })
            } else {
                this['SET_PREV_FILTERED_LOG_LINK'](null)
                this.filteredLog = []
            }
            await this.$nextTick(async () => {
                this.toBottom('auto')
            })
        },
        prev_log_data: function(val) {
            // assign prev_log_data when it changes
            this.prevLogData = val
        },
    },

    async mounted() {
        this.cleanUp()
        await this.fetchMaxScaleOverviewInfo()
        await this.getLatestLogs()
        await this.loopGetOlderLogs()
        // go to bottom on mounted
        this.toBottom('auto')
        await this.openConnection()
    },
    beforeDestroy() {
        this.cleanUp()
    },
    methods: {
        ...mapActions({
            fetchMaxScaleOverviewInfo: 'maxscale/fetchMaxScaleOverviewInfo',
        }),
        ...mapMutations('maxscale', ['SET_PREV_FILTERED_LOG_LINK']),
        ...mapActions('maxscale', ['fetchLatestLogs', 'fetchPrevLog', 'fetchPrevFilteredLog']),
        setPageContentDim() {
            this.$nextTick(() => {
                const pageContentHeight =
                    this.$typy(this.$refs, 'pageContent.clientHeight').safeNumber -
                    this.$typy(this.$refs, 'logHeader.$el.clientHeight').safeNumber
                // 32 is vertical padding in px
                if (pageContentHeight) this.logViewHeight = pageContentHeight - 32
            })
        },
        cleanUp() {
            if (this.connection) this.disconnect()
        },
        /**
         * This function get latest 1000 log lines.
         * It assigns latest_logs to allLogData
         */
        async getLatestLogs() {
            this.isLoading = true
            await this.fetchLatestLogs()
            this.isLoading = false
            this.allLogData = Object.freeze(this.latest_logs)
        },

        /**
         * This function gets older logs than current until the log content div
         * is scrollable. This allows user to scroll up to get old logs if
         * current logs received are to small which make it unable to scroll.
         * This may happen when log_source is maxlog and log_debug=1 as
         * multiple log lines in maxscale is now ignored.
         */
        async loopGetOlderLogs() {
            while (!this.isScrollable() && this.prev_log_link) {
                this.isLoading = true
                await this.handleUnionPrevLogs()
            }
            this.isLoading = false
        },

        /**
         * This function handles unioning prevLogData to current allLogData.
         * and preserves current scrolling position
         */
        async handleUnionPrevLogs() {
            const { scrollableContent } = this.$refs
            // store current scroll height before unioning prevLogData to allLogData
            const scrollHeight = scrollableContent.scrollHeight
            if (this.prev_log_link) await this.fetchPrevLog()
            if (this.prevLogData.length) {
                this.allLogData = Object.freeze(
                    this.$help.lodash.unionBy(this.prevLogData, this.allLogData, 'id')
                )
                this.prevLogData = [] // clear logs as it has been prepended to allLogData
                this.$nextTick(() => {
                    this.preserveScrollHeight(scrollHeight)
                })
            }
        },

        /**
         * @param {Object} e - scroll event
         * This function handles scroll event.
         * It triggers handler methods if scroll position match
         * provided conditions
         */
        async onScroll(e) {
            // check is scrollable, only handles scroll events when it's scrollable
            if (e.target.scrollHeight > e.target.clientHeight) {
                this.isAtBottom = this.checkIsAtBottom(e)
                /* calls handleUnionPrevLogs method when there is prev_log_link and
                 * current scroll position is 0.
                 */
                if (e.target.scrollTop === 0) {
                    await this.handleScrollToTop()
                }
            }
        },

        async handleScrollToTop() {
            this.isLoading = true
            if (this.isFiltering) await this.handleUnionPrevFilteredLogs()
            else await this.handleUnionPrevLogs()
            this.isLoading = false
        },

        /**
         * This function handles unioning prev filtered log to current filteredLog.
         * It delays for 300ms before unioning log data
         * It also preserves current scrolling position
         */
        async handleUnionPrevFilteredLogs() {
            if (this.prev_filtered_log_link || this.prev_log_link) {
                // TODO: add filter params once maxscale supports it
                await this.fetchPrevFilteredLog()
                const { scrollableContent } = this.$refs
                // store current scroll height before unioning prevLogData to allLogData
                const scrollHeight = scrollableContent.scrollHeight
                await this.$help.delay(300) // delay adding for better UX
                // TODO: Once rest api supports filters, don't need to filter it here
                const prevFilteredLogData = this.prev_filtered_log_data.filter(log =>
                    this.chosenLogLevels.includes(log.priority)
                )
                this.filteredLog = Object.freeze(
                    this.$help.lodash.unionBy(prevFilteredLogData, this.filteredLog, 'id')
                )
                await this.$nextTick(() => {
                    this.preserveScrollHeight(scrollHeight)
                })
            }
        },

        /**
         * This function opens websocket connection to get real-time logs
         */
        async openConnection() {
            const { protocol, host } = window.location

            const socketProtocol = protocol === 'http:' ? 'ws' : 'wss'
            const socketURI = `${socketProtocol}://${host}/maxscale/logs/stream`

            this.connection = new WebSocket(socketURI)

            // push new log to allLogData
            this.connection.onmessage = async e => {
                const newEntry = JSON.parse(e.data)

                this.allLogData = Object.freeze([...this.allLogData, newEntry])

                if (this.isFiltering && this.isMatchedFilter(newEntry)) {
                    this.filteredLog = Object.freeze([...this.filteredLog, newEntry])
                }
                if (!this.isFiltering || this.isMatchedFilter(newEntry))
                    this.$nextTick(() => {
                        this.showNotifHandler()
                    })
            }
        },

        /* if scrolled position is at bottom already,
         * scroll to bottom to see latest data. Otherwise,
         * show notification button (let user controls scroll
         * to bottom)
         */
        showNotifHandler() {
            if (this.isAtBottom) this.toBottom()
            else this.isNotifShown = true
        },

        disconnect() {
            this.connection.close()
            this.allLogData = []
        },

        /**
         * @param {Object} log - log object
         */
        isMatchedFilter(log) {
            return this.chosenLogLevels.includes(log.priority)
        },

        clickToBottom() {
            this.toBottom()
            // hide notification
            this.isNotifShown = false
        },

        /**
         * @param {String} behavior - behavior when scroll to target. e.g. smooth, auto, ..
         * This function scrolls to bottom after latest logs has been fetched.
         */
        toBottom(behavior = 'smooth') {
            let bottomEle = this.$refs.bottomLogLine
            if (bottomEle)
                bottomEle.scrollIntoView({
                    behavior: behavior,
                    block: 'nearest',
                    inline: 'start',
                })
        },

        /**
         * @param {Object} e - scroll event
         * @returns returns boolean
         */
        checkIsAtBottom: e => e.target.clientHeight === e.target.scrollHeight - e.target.scrollTop,

        /**
         * Check if the log content div is scrollable
         */
        isScrollable() {
            const { scrollableContent, scrollableWrapper } = this.$refs
            if (!scrollableContent || !scrollableWrapper) return false
            else return scrollableContent.scrollHeight > scrollableWrapper.$el.clientHeight
        },

        /**
         * This function takes previous scroll height and calculates
         * it while accounting for offset if loading indicator section
         * is visible and minus the height of three log lines.
         * It assigns calculated value to scrollableWrapper scrollTop
         * to remain previous scroll position.
         * @param {Number} prevScrollHeight
         */
        preserveScrollHeight(prevScrollHeight) {
            const { scrollableContent, scrollableWrapper } = this.$refs
            const offset = this.isLoading ? 71 : 0 // offset height of loading section
            const aLineHeight = 24
            // preseve scroll pos after unioning logs and show 3 new log lines
            scrollableWrapper.$el.scrollTop = Math.floor(
                scrollableContent.offsetHeight - (prevScrollHeight + aLineHeight * 3 - offset)
            )
        },
    },
}
</script>

<style lang="scss" scoped>
.loading-logs-text-indicator {
    font-size: 0.825rem;
}
.new-log-btn {
    position: absolute;
    right: 50%;
    transform: translateX(50%);
    bottom: 6%;
    z-index: 1;
}
</style>
