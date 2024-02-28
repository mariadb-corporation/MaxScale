<template>
    <code
        v-if="source"
        class="d-block mariadb-code-style text-wrap"
        :class="logPriorityColorClasses(attributes.priority)"
    >
        <span class="mxs-color-helper text-grayed-out">{{ attributes.timestamp }}&nbsp;&nbsp;</span>
        <span class="log-level d-inline-block">
            <status-icon size="13" type="log" :value="attributes.priority" />
            <span class="tk-azo-sans-web">&nbsp;</span>
            <span>{{ attributes.priority }}</span>
        </span>
        <span v-html="logLevelNbspGen(attributes.priority)" />
        <span class="log-level-divider mxs-color-helper text-code-color">:</span>
        <span>&nbsp;</span>
        <span>{{ attributes.message }}</span>
    </code>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'log-line',
    props: {
        source: {
            type: Object,
            default() {
                return {
                    attributes: {
                        timestamp: '',
                        priority: '',
                        message: '',
                    },
                    id: '',
                }
            },
        },
    },
    computed: {
        attributes() {
            return this.$typy(this.source, 'attributes').safeObjectOrEmpty
        },
    },
    methods: {
        logPriorityColorClasses: level =>
            `mxs-color-helper text-${level} ${level === 'alert' ? 'font-weight-bold' : ''}`,
        logLevelNbspGen(level) {
            switch (level) {
                case 'error':
                case 'alert':
                case 'debug':
                    return '&nbsp;&nbsp;'
                case 'notice':
                    return '&nbsp;'
                case 'info':
                    return '&nbsp;&nbsp;&nbsp;'
                case 'warning':
                    return ''
            }
        },
    },
}
</script>
