<template>
    <div :class="['d-flex flex-column']">
        <p>
            <span class="color text-field-text">
                <span class="d-inline-block mr-2">
                    {{ log_file }}
                </span>
                {{
                    $help.dateFormat({
                        value: maxscaleOverviewInfo.started_at,
                        formatType: 'DATE_RFC2822',
                    })
                }}
            </span>
        </p>
        <div class="logs-wrapper pa-4 color bg-reflection">
            <v-card
                id="scrollable-logs"
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
                            Loading logs...
                        </p>
                    </div>
                </template>

                <div id="scrolled-content" v-scroll:#scrollable-logs="onScroll">
                    <log-view :logData="logData" />
                </div>
            </v-card>
        </div>
    </div>
</template>

<script>
import LogView from './LogView'
export default {
    name: 'log-container',
    components: {
        LogView,
    },
    props: {
        maxscaleOverviewInfo: { type: Object, required: true },
        shouldFetchLogs: { type: Boolean, required: true },
    },
    data() {
        return {
            containerHeight: document.documentElement.clientHeight * 0.6 + 'px',
            scrollTarget: null,
            scrolledContent: null,
            scrollTop: 100,
            isLoading: false,
            logData: [],
            log_file: [],
            prevPageLink: null,
            connection: null,
        }
    },

    watch: {
        shouldFetchLogs: async function(val) {
            if (val) {
                await this.fetchLatestLogs()
                // TODOS:
                await this.toBottom()
            }
        },
    },

    async mounted() {
        await this.fetchLatestLogs()
        await this.toBottom()
        await this.openConnection()
    },
    beforeDestroy() {
        if (this.connection) this.disconnect()
    },
    methods: {
        /**
         * This function scrolls to bottom after latest logs has been fetched.
         * scroll to bottom. TODO: if current scroll position is at bottom already,
         * call toBottom(). If user are scrolling up or shouldFetchLogs value change,
         * it should show a popup saying something like
         * "new logs available", clicking will call toBottom()
         */
        async toBottom() {
            this.scrolledContent = document.getElementById('scrolled-content')
            this.scrollTarget = document.getElementById('scrollable-logs')
            this.scrollTarget.scrollTop = Math.floor(this.scrolledContent.offsetHeight)
        },

        /**
         * This function detect scroll up event. It calls
         * handleFetchPrevPage method when there is prevPageLink and
         * current scroll position is 0.
         */
        async onScroll(e) {
            if (e.target.scrollTop === 0 && this.prevPageLink) await this.handleFetchPrevPage()
        },

        /**
         * This function fetches latest 50 log lines.
         * It assigns log_file, logData and prevLink
         */
        async fetchLatestLogs() {
            try {
                this.isLoading = true
                const res = await this.$axios.get('/maxscale/logs')
                await this.$help.delay(400).then(() => (this.isLoading = false))
                const {
                    data: { attributes: { log = [], log_file = null } = {} } = {},
                    links: { prev = null },
                } = res.data

                this.log_file = log_file
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
                await this.toBottom()
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
            // store current height before union prev logs to logData
            const currentHeight = this.scrolledContent.scrollHeight

            if (!this.isLoading)
                this.logData = this.$help.lodash.unionBy(prevLogs, this.logData, 'id')

            this.$nextTick(() => {
                const aLineHeight = 24
                // preseve scroll pos when union to logs and show 3 new log lines
                this.scrollTarget.scrollTop = Math.floor(
                    this.scrolledContent.offsetHeight - (currentHeight + aLineHeight * 3)
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
                const res = await this.$axios.get(endpoint)
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
    },
}
</script>
<style lang="scss" scoped>
.loading-logs-text-indicator {
    font-size: 0.825rem;
}
</style>
