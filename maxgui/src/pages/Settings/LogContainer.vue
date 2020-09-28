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
                    <log-lines :logData="logData" />
                    <div id="bottom-log-line" />
                </div>
            </v-card>
        </div>
    </div>
</template>

<script>
import LogLines from './LogLines'
export default {
    name: 'log-container',
    components: {
        LogLines,
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
            log_source: [],
            prevPageLink: null,
            connection: null,
            isNotifShown: false,
            isAtBottom: false,
        }
    },

    watch: {
        // controls by parent component
        shouldFetchLogs: async function(val) {
            if (val) {
                await this.fetchLatestLogs()
                this.toBottom('auto')
            }
        },
        isAtBottom: function(val) {
            // Turn off notif if scroll position is at bottom already
            if (val) this.isNotifShown = false
        },
    },

    async mounted() {
        this.setContainerHeight()
        window.addEventListener('resize', this.setContainerHeight)
        await this.fetchLatestLogs()
        await this.loopFetchOlderLogs()
        // go to bottom on mounted
        this.toBottom('auto')
        await this.openConnection()
    },
    beforeDestroy() {
        if (this.connection) this.disconnect()
        window.removeEventListener('resize', this.setContainerHeight)
    },
    methods: {
        setContainerHeight() {
            this.containerHeight = document.documentElement.clientHeight * 0.6
        },
        /**
         * This function fetches older logs than current until the log content div
         * is scrollable. This allows user to scroll up to get old logs if
         * current logs received are to small which make it unable to scroll.
         * This may happen when log_source is maxlog and log_debug=1 as
         * multiple log lines in maxscale is now ignored.
         */
        async loopFetchOlderLogs() {
            while (!this.isScrollable() && this.prevPageLink) {
                await this.handleFetchPrevPage()
            }
        },

        /**
         * This function fetches latest 50 log lines.
         * It assigns log_source, logData and prevLink
         */
        async fetchLatestLogs() {
            try {
                this.isLoading = true
                const res = await this.$axios.get('/maxscale/logs/data')
                await this.$help.delay(400).then(() => (this.isLoading = false))
                const {
                    data: { attributes: { log = [], log_source = null } = {} } = {},
                    links: { prev = null },
                } = res.data

                this.log_source = log_source
                // union it instead of assign otherwise there will be duplicated id
                this.logData = this.$help.lodash.unionBy(this.logData, log, 'id')
                this.prevPageLink = prev
            } catch (e) {
                this.$logger('LogContainer-fetchLatestLogs').error(e)
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

            // push new log to logData
            this.connection.onmessage = async e => {
                await this.logData.push(JSON.parse(e.data))
                /* if scrolled position is at bottom already,
                 * scroll to bottom to see latest data. Otherwise,
                 * show notification button (let user controls scroll
                 * to bottom)
                 */
                if (this.isAtBottom) this.toBottom()
                else this.isNotifShown = true
            }
        },

        disconnect() {
            this.connection.close()
            this.logData = []
        },

        /**
         * This function handles unioning previous cursor logData to current
         * logData.
         * It turns off loading state after 400ms as a minimum loading time
         * then preserve current scrolling position before unioning logs
         */
        async handleFetchPrevPage() {
            const prevLogs = await this.getPrevCursorLogs()
            await this.$help.delay(400).then(() => (this.isLoading = false))
            const { scrollableContent, scrollableWrapper } = this.$refs
            // store current height before union prev logs to logData
            const currentHeight = scrollableContent.scrollHeight

            if (!this.isLoading)
                this.logData = this.$help.lodash.unionBy(prevLogs, this.logData, 'id')

            await this.$nextTick(() => {
                const aLineHeight = 24
                // preseve scroll pos when union to logs and show 3 new log lines
                scrollableWrapper.$el.scrollTop = Math.floor(
                    scrollableContent.offsetHeight - (currentHeight + aLineHeight * 3)
                )
            })
        },

        /**
         * This function returns previous logData array from previous cursor page link.
         * It also assigns prev link
         * @returns previous logData array
         */
        async getPrevCursorLogs() {
            try {
                this.isLoading = true
                const indexOfEndpoint = this.prevPageLink.indexOf('/maxscale/logs/')
                const endpoint = this.prevPageLink.slice(indexOfEndpoint)
                const res = await this.$axios.get(`${endpoint}`)
                const {
                    data: { attributes: { log: newLogs = [] } = {} } = {},
                    links: { prev = null },
                } = res.data
                this.prevPageLink = prev
                return newLogs
            } catch (e) {
                this.$logger('LogContainer-getPrevCursorLogs').error(e)
            }
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
                block: 'center',
                inline: 'start',
            })
        },

        /**
         * @param {Object} e - scroll event
         * This function handles scroll event.
         * It triggers handler methods if scroll position match
         * provided conditions
         */
        async onScroll(e) {
            this.isAtBottom = this.checkIsAtBottom(e)
            /* calls handleFetchPrevPage method when there is prevPageLink and
             * current scroll position is 0.u
             */
            if (e.target.scrollTop === 0 && this.prevPageLink) await this.handleFetchPrevPage()
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
