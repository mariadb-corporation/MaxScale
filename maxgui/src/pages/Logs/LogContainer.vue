<template>
    <div>
        <v-btn
            v-if="isNotifShown"
            class="pa-2 new-log-btn font-weight-medium px-7 text-capitalize"
            small
            height="36"
            color="primary"
            rounded
            depressed
            @click="setVirtualListToBottom"
        >
            {{ $t('newMessagesAvailable') }}!
            <v-icon class="arrow-down" size="32">
                $expand
            </v-icon>
        </v-btn>
        <virtual-list
            v-if="logToShow.length"
            ref="vsl"
            :style="{ height: `${logViewHeight}px` }"
            class="log"
            :class="{ overflow }"
            :data-key="'id'"
            :data-sources="logToShow"
            :data-component="LogLine"
            :keeps="logs_page_size"
            :estimate-size="72"
            @resized="onItemRendered"
            @scroll="onScroll"
            @totop="onTotop"
        >
            <template slot="header">
                <div v-show="overflow">
                    <div class="d-flex flex-column align-center justify-center mt-2">
                        <v-progress-circular
                            v-show="!finished"
                            color="primary"
                            indeterminate
                            size="24"
                        />
                    </div>
                </div>
            </template>
        </virtual-list>
        <div v-else-if="!isFetching" :style="{ height: `${logViewHeight}px` }" class="empty">
            <code class="d-block mariadb-code-style">
                {{ $t('noLogsFound') }}
            </code>
        </div>
        <div v-else class="d-flex flex-column align-center justify-center mt-2">
            <v-progress-circular color="primary" indeterminate size="24" />
            <p class="mt-1 loading-logs-text-indicator color text-field-text">
                {{ $t('loadingLogs') }}...
            </p>
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
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapMutations, mapState } from 'vuex'
import VirtualList from 'vue-virtual-scroll-list'
import LogLine from './LogLine'
export default {
    name: 'log-container',
    components: {
        VirtualList,
    },
    props: {
        logViewHeight: { type: Number, required: true },
    },
    data() {
        return {
            connection: null,
            LogLine: LogLine,
            overflow: false,
            isFetching: false,
            isFirstPageReady: false,
            finished: false,
            allLogData: [],
            prevLogData: [],
            filteredLogData: [],
            isNotifShown: false,
            isAtBottom: false,
        }
    },
    computed: {
        ...mapState({
            MAXSCALE_LOG_LEVELS: state => state.app_config.MAXSCALE_LOG_LEVELS,
            logs_page_size: state => state.maxscale.logs_page_size,
            latest_logs: state => state.maxscale.latest_logs,
            prev_log_link: state => state.maxscale.prev_log_link,
            prev_log_data: state => state.maxscale.prev_log_data,
            chosen_log_levels: state => state.maxscale.chosen_log_levels,
            prev_filtered_log_link: state => state.maxscale.prev_filtered_log_link,
            prev_filtered_log_data: state => state.maxscale.prev_filtered_log_data,
        }),
        logToShow() {
            if (this.isFiltering) return this.filteredLogData
            else return this.allLogData
        },
        isFiltering: function() {
            /* Default chosen_log_levels length is equal to the length of MAXSCALE_LOG_LEVELS
             * so, if it's not equal, then user's filtering logs
             */
            return this.chosen_log_levels.length !== this.MAXSCALE_LOG_LEVELS.length
        },
    },
    watch: {
        prev_log_data: function(val) {
            // assign prev_log_data when it changes
            this.prevLogData = val
        },
        chosen_log_levels: {
            deep: true,
            async handler(v) {
                if (v.length && this.isFiltering) {
                    // filter allLogData based on chosen_log_levels and assign it to filteredLogData
                    this.filteredLogData = this.allLogData.filter(log =>
                        this.chosen_log_levels.includes(log.priority)
                    )
                } else {
                    this['SET_PREV_FILTERED_LOG_LINK'](null)
                    this.filteredLogData = []
                }
                this.$nextTick(() => this.setVirtualListToBottom())
            },
        },
    },
    async created() {
        this.finished = false
        // first page request
        await this.getLatestLogs()
        // Check overflow to auto fetch older logs to enough logs so that the container is overflow
        this.checkOverFlow()
        if (!this.overflow) await this.loopGetOlderLogs()
        await this.openConnection()
    },
    beforeDestroy() {
        this.cleanUp()
    },
    methods: {
        ...mapActions('maxscale', ['fetchLatestLogs', 'fetchPrevLog', 'fetchPrevFilteredLog']),
        ...mapMutations('maxscale', ['SET_PREV_FILTERED_LOG_LINK']),
        /**
         * This function get latest log line
         * It assigns latest_logs to allLogData
         */
        async getLatestLogs() {
            this.isFetching = true
            await this.fetchLatestLogs()
            this.isFetching = false
            this.allLogData = Object.freeze(this.latest_logs)
        },
        async onItemRendered() {
            if (!this.$refs.vsl) return
            this.checkOverFlow()
            // first page items are all mounted, scroll to bottom, execute below block once
            if (
                !this.isFirstPageReady &&
                this.$refs.vsl.getSizes() >= this.logs_page_size &&
                this.overflow
            ) {
                this.isFirstPageReady = true
                this.setVirtualListToBottom()
            }
        },
        onScroll(e) {
            if (e.target.scrollHeight > e.target.clientHeight) {
                if (e.target.clientHeight === e.target.scrollHeight - e.target.scrollTop) {
                    this.isAtBottom = true
                    this.isNotifShown = false // Turn off notif if it is at bottom already
                } else this.isAtBottom = false
            }
        },
        async onTotop() {
            if (this.isFetching || this.finished) return
            this.isFetching = true
            if (this.isFiltering) {
                await this.handleUnionPrevFilteredLogs()
                // Check overflow to auto fetch older logs to enough logs so that the container is overflow
                this.checkOverFlow()
                if (!this.overflow) await this.loopGetOlderLogs()
            } else await this.handleUnionPrevLogs()
            this.isFetching = false
        },

        /* if scrolled position is at bottom position before new logs are appended,
         * scroll to bottom to see latest data. Otherwise, how notification button
         * (let user controls scroll to bottom)
         */
        showNotifHandler() {
            if (this.isAtBottom) this.setVirtualListToBottom()
            else this.isNotifShown = true
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

                if (this.isFiltering && this.isMatchedFilter(newEntry))
                    this.filteredLogData = Object.freeze([...this.filteredLogData, newEntry])

                this.$nextTick(() => {
                    if (!this.isFiltering | (this.isFiltering && this.isMatchedFilter(newEntry)))
                        this.showNotifHandler()
                })
            }
        },
        disconnect() {
            this.connection.close()
            this.allLogData = []
        },
        cleanUp() {
            if (this.connection) this.disconnect()
        },

        //TODO: DRY below union handlers
        /**
         * This function handles unioning prevLogData to current allLogData.
         * and preserves current scrolling position
         */
        async handleUnionPrevLogs() {
            if (!this.prev_log_link) {
                this.finished = true
                return
            } else await this.fetchPrevLog()
            const ids = this.getIds(this.prevLogData)
            this.allLogData = this.$help.lodash.unionBy(this.prevLogData, this.allLogData, 'id')
            if (ids.length) this.$nextTick(() => this.preserveScrollHeight(ids))
            this.prevLogData = [] // clear logs as it has been prepended to allLogData
        },
        /**
         * This function handles unioning prev filtered log to current filteredLog
         * and preserves current scrolling position
         */
        async handleUnionPrevFilteredLogs() {
            if (this.prev_filtered_log_link || this.prev_log_link) {
                await this.fetchPrevFilteredLog()
                this.filteredLogData = Object.freeze(
                    this.$help.lodash.unionBy(
                        this.prev_filtered_log_data,
                        this.filteredLogData,
                        'id'
                    )
                )
                const ids = this.getIds(this.prev_filtered_log_data)
                if (ids.length) this.$nextTick(() => this.preserveScrollHeight(ids))
                // recursive until prev_filtered_log_link is null
                else await this.handleUnionPrevFilteredLogs()
            }
        },

        /**
         * This function gets older logs than current until the log content div
         * is scrollable. This allows user to scroll up to get old logs if
         * current logs received are to small which make it unable to scroll.
         * This may happen when log_source is maxlog and log_debug=1 as
         * multiple log lines in maxscale is now ignored.
         */
        async loopGetOlderLogs() {
            const prevLink = this.prev_filtered_log_link || this.prev_log_link
            while (!this.overflow && prevLink) {
                this.isFetching = true
                this.isFiltering
                    ? await this.handleUnionPrevFilteredLogs()
                    : await this.handleUnionPrevLogs()

                this.$nextTick(() => this.checkOverFlow())
            }
            this.isFetching = false
        },

        getIds(logs) {
            return logs.map(message => message.id)
        },
        checkOverFlow() {
            const vsl = this.$refs.vsl
            if (vsl) {
                this.overflow = vsl.getScrollSize() > vsl.getClientSize()
            }
        },
        // mock received message
        setVirtualListToOffset(offset) {
            if (this.$refs.vsl) this.$refs.vsl.scrollToOffset(offset)
        },
        setVirtualListToBottom() {
            if (this.$refs.vsl) this.$refs.vsl.scrollToBottom()
        },
        /**
         * @param {Object} e - scroll event
         * @returns returns boolean
         */
        checkIsAtBottom: e => e.target.clientHeight === e.target.scrollHeight - e.target.scrollTop,
        /**
         * @param {Object} log - log object
         */
        isMatchedFilter(log) {
            return this.chosen_log_levels.includes(log.priority)
        },
        /**
         * @param {Array} ids - ids of new items to be prepended
         */
        preserveScrollHeight(ids) {
            const vsl = this.$refs.vsl
            const offset = ids.reduce((previousValue, currentID) => {
                const previousSize =
                    typeof previousValue === 'string' ? vsl.getSize(previousValue) : previousValue
                return previousSize + this.$refs.vsl.getSize(currentID)
            })
            this.setVirtualListToOffset(offset)
        },
    },
}
</script>

<style lang="scss">
.log {
    position: relative;
    width: 100%;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
}
.new-log-btn {
    position: absolute;
    right: 50%;
    transform: translateX(50%);
    bottom: 6%;
    z-index: 1;
}
</style>
