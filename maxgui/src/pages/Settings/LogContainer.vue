<template>
    <div class="log-view-container">
        <p>
            <span class="color text-field-text">
                <span class="d-block mr-2"> log_source: {{ log_source }} </span>
                {{
                    $help.dateFormat({
                        value: maxscaleOverviewInfo.started_at,
                        formatType: 'DATE_RFC2822',
                    })
                }}
            </span>
        </p>
        <log-filter @get-chosen-log-levels="chosenLogLevels = $event" />
        <div class="log-lines-container pa-4 color bg-reflection">
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
                :min-height="containerHeight"
                :max-height="containerHeight"
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
                        :chosenLogLevels="chosenLogLevels"
                        :logData="logData"
                    />
                    <div id="bottom-log-line" />
                </div>
            </v-card>
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
 * Change Date: 2024-09-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import LogLines from './LogLines'
import LogFilter from './LogFilter'
import { mapActions, mapState } from 'vuex'

export default {
    name: 'log-container',
    components: {
        LogLines,
        LogFilter,
    },
    props: {
        maxscaleOverviewInfo: { type: Object, required: true },
        shouldFetchLogs: { type: Boolean, required: true },
    },
    data() {
        return {
            containerHeight: 0,
            isLoading: false,
            logData: [],
            prevLogData: [],
            connection: null,
            isNotifShown: false,
            isAtBottom: false,
            chosenLogLevels: [],
        }
    },
    computed: {
        ...mapState('maxscale', {
            latest_logs: state => state.latest_logs,
            prev_log_link: state => state.prev_log_link,
            log_source: state => state.log_source,
            prev_log_data: state => state.prev_log_data,
        }),
    },
    watch: {
        // controls by parent component
        shouldFetchLogs: async function(val) {
            if (val) {
                await this.getLatestLogs()
                await this.$nextTick(async () => {
                    this.toBottom('auto')
                })
            }
        },
        isAtBottom: function(val) {
            // Turn off notif if scroll position is at bottom already
            if (val) this.isNotifShown = false
        },

        chosenLogLevels: async function() {
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
                await this.loopGetOlderLogs()

                this.toBottom('auto')
            })
        },
        prev_log_data: function(val) {
            // assign prev_log_data when it changes
            this.prevLogData = val
        },
    },

    async mounted() {
        this.setContainerHeight()
        window.addEventListener('resize', this.setContainerHeight)
        await this.getLatestLogs()
        await this.loopGetOlderLogs()
        // go to bottom on mounted
        this.toBottom('auto')
        await this.openConnection()
        // call fetchPrevLog to get prev logs one step ahead
        await this.fetchPrevLog()
    },
    beforeDestroy() {
        if (this.connection) this.disconnect()
        window.removeEventListener('resize', this.setContainerHeight)
    },

    methods: {
        ...mapActions('maxscale', ['fetchLatestLogs', 'fetchPrevLog']),

        /**
         * This function get latest 50 log lines.
         * It assigns latest_logs to logData
         */
        async getLatestLogs() {
            this.isLoading = true
            await this.fetchLatestLogs()
            await this.$help.delay(400).then(() => (this.isLoading = false))
            this.logData = Object.freeze(this.latest_logs)
        },

        /**
         * @param {Object} e - scroll event
         * This function handles scroll event.
         * It triggers handler methods if scroll position match
         * provided conditions
         */
        async onScroll(e) {
            this.isAtBottom = this.checkIsAtBottom(e)
            /* calls handleUnionPrevLogs method when there is prev_log_link and
             * current scroll position is 0.
             */
            if (e.target.scrollTop === 0) {
                this.isLoading = true
                await this.handleUnionPrevLogs()
                this.isLoading = false
            }
        },

        /**
         * This function handles unioning prevLogData to current logData.
         * It delays for 300ms before unioning log data
         * It also preserves current scrolling position
         */
        async handleUnionPrevLogs() {
            const { scrollableContent } = this.$refs
            // store current scroll height before unioning prevLogData to logData
            const scrollHeight = scrollableContent.scrollHeight
            if (this.prevLogData.length) {
                await this.$help.delay(300) // delay adding for better UX
                this.logData = Object.freeze(
                    this.$help.lodash.unionBy(this.prevLogData, this.logData, 'id')
                )
                this.prevLogData = [] // clear logs have been prepended to logData
                await this.$nextTick(() => {
                    this.preserveScrollHeight(scrollHeight)
                })
            }
            // prefetch fetchPrevLog one step a head so a delay time is always 300ms
            if (this.prev_log_link) await this.fetchPrevLog()
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
         * This function opens websocket connection to get real-time logs
         */
        async openConnection() {
            const { protocol, host } = window.location

            const socketProtocol = protocol === 'http:' ? 'ws' : 'wss'
            const socketURI = `${socketProtocol}://${host}/maxscale/logs/stream`

            this.connection = new WebSocket(socketURI)

            // push new log to logData
            this.connection.onmessage = async e => {
                const newEntry = JSON.parse(e.data)
                const isFiltering = this.chosenLogLevels.length
                const matchedFilter = this.chosenLogLevels.includes(newEntry.priority)
                if ((isFiltering && matchedFilter) || !isFiltering)
                    this.logData = Object.freeze([...this.logData, newEntry])
                /* if scrolled position is at bottom already,
                 * scroll to bottom to see latest data. Otherwise,
                 * show notification button (let user controls scroll
                 * to bottom)
                 */
                await this.$nextTick(() => {
                    if (this.isAtBottom) this.toBottom()
                    else this.isNotifShown = true
                })
            }
        },

        disconnect() {
            this.connection.close()
            this.logData = []
        },

        setContainerHeight() {
            this.containerHeight = document.documentElement.clientHeight * 0.6
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
            let bottomEle = document.getElementById('bottom-log-line')
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
            return scrollableContent.scrollHeight > scrollableWrapper.$el.clientHeight
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
