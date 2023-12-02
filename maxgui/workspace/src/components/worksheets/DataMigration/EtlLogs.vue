<template>
    <div class="d-flex flex-column">
        <h6 class="etl-logs-title mxs-color-helper text-navigation mb-4">{{ $mxs_t('msgLog') }}</h6>
        <code
            ref="logCtr"
            class="fill-height log-container overflow-y-auto mariadb-code-style rounded mxs-color-helper all-border-separator pa-4"
        >
            <div v-for="log in logs" :key="log.timestamp">
                <span class="mxs-color-helper text-grayed-out">
                    {{
                        $helpers.dateFormat({
                            value: log.timestamp,
                            formatType: 'dd-MMM-yyyy  HH:mm:ss',
                        })
                    }}
                </span>
                <span class="log-name">{{ log.name }}</span>
            </div>
        </code>
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
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'etl-logs',
    props: {
        task: { type: Object, required: true },
    },
    computed: {
        etlLog() {
            return this.$typy(this.task, 'logs').safeObjectOrEmpty
        },
        activeStageIdx() {
            return this.$typy(this.task, 'active_stage_index').safeNumber
        },
        logs() {
            return this.$typy(this.etlLog, `[${this.activeStageIdx}]`).safeArray
        },
    },
    watch: {
        logs: {
            deep: true,
            handler(v) {
                if (v && v.length) this.scrollToBottom()
            },
        },
    },
    mounted() {
        this.scrollToBottom()
    },
    methods: {
        scrollToBottom() {
            let ele = this.$typy(this.$refs, 'logCtr').safeObject
            if (ele) ele.scrollTop = ele.scrollHeight
        },
    },
}
</script>

<style lang="scss" scoped>
.etl-logs-title {
    line-height: normal;
}
.log-container {
    font-size: 0.75rem;
    letter-spacing: -0.1px;
    .log-name {
        white-space: pre-wrap;
    }
}
</style>
