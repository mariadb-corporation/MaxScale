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
            {{ $mxs_t('newMessagesAvailable') }}!
            <v-icon class="rotate-down" size="32">
                mdi-chevron-down
            </v-icon>
        </v-btn>
        <virtual-list
            v-if="logs.length"
            ref="virtualList"
            :style="{ height: `${logViewHeight}px` }"
            class="log"
            :data-key="'id'"
            :data-sources="logs"
            :data-component="LogLine"
            :keeps="logs_page_size"
            @scroll="onScroll"
            @totop="onTotop"
        >
            <template slot="header">
                <div v-show="isScrollable">
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
                {{ $mxs_t('noLogsFound') }}
            </code>
        </div>
        <div v-else class="d-flex flex-column align-center justify-center mt-2">
            <v-progress-circular color="primary" indeterminate size="24" />
            <p class="mt-1 loading-logs-text-indicator mxs-color-helper text-grayed-out">
                {{ $mxs_t('loadingLogs') }}...
            </p>
        </div>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState, mapGetters } from 'vuex'
import VirtualList from 'vue-virtual-scroll-list'
import LogLine from './LogLine'
import { fromUnixTime, isToday } from 'date-fns'

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
            isScrollable: false,
            isFetching: false,
            finished: false,
            logs: [],
            prevLogData: [],
            isNotifShown: false,
            isAtBottom: false,
        }
    },
    computed: {
        ...mapState({
            logs_page_size: state => state.maxscale.logs_page_size,
            latest_logs: state => state.maxscale.latest_logs,
            prev_log_link: state => state.maxscale.prev_log_link,
            prev_logs: state => state.maxscale.prev_logs,
            log_filter: state => state.maxscale.log_filter,
        }),
        ...mapGetters({ logDateRangeTimestamp: 'maxscale/logDateRangeTimestamp' }),
        priorities() {
            return this.log_filter.priorities
        },
    },
    watch: {
        prev_logs(val) {
            this.prevLogData = val
        },
        log_filter: {
            deep: true,
            async handler(v) {
                if (!this.$typy(v).isEmptyObject) await this.handleFetchLogs()
            },
        },
    },
    async created() {
        await this.handleFetchLogs()
        await this.openConnection()
    },
    beforeDestroy() {
        this.disconnect()
    },
    methods: {
        ...mapActions('maxscale', ['fetchLatestLogs', 'fetchPrevLogs']),
        async handleFetchLogs() {
            this.finished = false
            await this.getLatestLogs()
            // If container is not scrollable, fetch previous logs until container is scrollable
            this.detectScrollability()
            if (!this.isScrollable) await this.fetchPrevLogsToFillContainer()
            this.$nextTick(() => this.setVirtualListToBottom())
        },
        async getLatestLogs() {
            this.isFetching = true
            await this.fetchLatestLogs()
            this.isFetching = false
            this.logs = Object.freeze(this.latest_logs)
        },
        /**
         * This function gets previous logs until the log container div
         * is scrollable. This allows user to scroll up to get older logs
         */
        async fetchPrevLogsToFillContainer() {
            while (!this.isScrollable && this.prev_log_link) {
                this.isFetching = true
                await this.fetchAndPrependPrevLogs()
                this.$nextTick(() => this.detectScrollability())
            }
            this.isFetching = false
        },
        /**
         * @param {boolean} [param.loop] - if true, it loops the request until getting some logs in case
         * prevLogData is an empty array.
         */
        async fetchAndPrependPrevLogs({ loop = false } = {}) {
            if (!this.prev_log_link) {
                this.finished = true
                return
            } else await this.fetchPrevLogs()
            const ids = this.getIds(this.prevLogData)
            // using union because prev logs may contain some log entries that are already in logs
            this.logs = this.$helpers.lodash.unionBy(this.prevLogData, this.logs, 'id')
            if (ids.length) {
                this.$nextTick(() => this.preserveScrollHeight(ids))
                this.prevLogData = [] // clear logs as it has been prepended to logs
            }
            // loop until getting some logs
            else if (loop) await this.fetchAndPrependPrevLogs({ loop })
        },
        /**
         * This function opens websocket connection to get real-time logs
         */
        async openConnection() {
            const { protocol, host } = window.location

            const socketProtocol = protocol === 'http:' ? 'ws' : 'wss'
            const socketURI = `${socketProtocol}://${host}/maxscale/logs/stream`

            this.connection = new WebSocket(socketURI)

            // push new log to logs
            this.connection.onmessage = async e => {
                const newEntry = JSON.parse(e.data)
                /**
                 * logs/stream endpoint doesn't return log entry with the same data structure as /logs/entries,
                 * so it should be transformed to the right structure
                 */
                const logEntry = {
                    id: newEntry.id,
                    attributes: this.$helpers.lodash.pickBy(newEntry, (value, key) => key !== 'id'),
                }
                if (this.isMatchedFilter(logEntry))
                    this.logs = Object.freeze([...this.logs, logEntry])
                if (this.isMatchedFilter(logEntry)) this.$nextTick(() => this.showNotifHandler())
            }
        },
        disconnect() {
            if (this.connection) this.connection.close()
            this.logs = []
        },
        onScroll(e) {
            if (e.target.scrollHeight > e.target.clientHeight) {
                if (this.checkIsAtBottom(e)) {
                    this.isAtBottom = true
                    this.isNotifShown = false // Turn off notif if it is at bottom already
                } else this.isAtBottom = false
            }
        },
        async onTotop() {
            if (this.isFetching || this.finished) return
            this.isFetching = true
            await this.fetchAndPrependPrevLogs({ loop: true })
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
        getIds(logs) {
            return logs.map(item => item.id)
        },
        detectScrollability() {
            const virtualList = this.$refs.virtualList
            this.isScrollable = virtualList
                ? virtualList.getScrollSize() > virtualList.getClientSize()
                : false
        },
        setVirtualListToOffset(offset) {
            if (this.$refs.virtualList) this.$refs.virtualList.scrollToOffset(offset)
        },
        setVirtualListToBottom() {
            if (this.$refs.virtualList) this.$refs.virtualList.scrollToBottom()
        },
        /**
         * @param {Object} e - scroll event
         * @returns {boolean}
         */
        checkIsAtBottom: e => {
            // Add a tolerance value to handle potential decimal discrepancies
            const tolerance = 1
            return e.target.clientHeight + e.target.scrollTop + tolerance >= e.target.scrollHeight
        },
        /**
         * If the `timestamp` falls within the current date, the condition evaluates to true,
         * even the `timestamp` is greater than logDateRangeTimestamp `to` value.
         * @param {number} timestamp unix timestamp in seconds
         * @returns {boolean}
         */
        isBetweenTimeRange(timestamp) {
            const [from, to] = this.logDateRangeTimestamp
            return timestamp >= from && (timestamp <= to || isToday(fromUnixTime(to)))
        },
        /**
         * @param {Object} log - log object
         * @returns {boolean}
         */
        isMatchedFilter({ attributes: { priority, unix_timestamp } }) {
            return (
                this.isBetweenTimeRange(unix_timestamp) &&
                (this.priorities.includes(priority) || !this.priorities.length)
            )
        },
        /**
         * @param {Array} ids - ids of new items to be prepended
         */
        preserveScrollHeight(ids) {
            const virtualList = this.$refs.virtualList
            const offset = ids.reduce((previousValue, currentID) => {
                const previousSize =
                    typeof previousValue === 'string' && previousValue !== 0
                        ? virtualList.getSize(previousValue)
                        : previousValue
                return previousSize + virtualList.getSize(currentID)
            }, 0)
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
