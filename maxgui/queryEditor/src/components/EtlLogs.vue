<template>
    <div class="d-flex flex-column">
        <h6 class="etl-logs-title mxs-color-helper text-navigation mb-4">{{ $mxs_t('msgLog') }}</h6>
        <code
            ref="logCtr"
            class="fill-height log-container overflow-y-auto mariadb-code-style rounded mxs-color-helper all-border-separator pa-4"
        >
            <div v-for="log in activeEtlTask.logs" :key="log.timestamp" class="d-block text-wrap">
                <span class="mxs-color-helper text-grayed-out">
                    {{
                        $helpers.dateFormat({
                            value: log.timestamp,
                            formatType: 'YYYY-MM-DD  HH:mm:ss',
                        })
                    }}&nbsp;
                </span>
                <span>{{ log.name }}</span>
            </div>
        </code>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlTask from '@queryEditorSrc/store/orm/models/EtlTask'

export default {
    name: 'etl-logs',
    computed: {
        activeEtlTask() {
            return EtlTask.getters('getActiveEtlTaskWithRelation')
        },
    },
    watch: {
        'activeEtlTask.logs': {
            deep: true,
            handler(v) {
                if (v.length) this.scrollToBottom()
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
}
</style>
