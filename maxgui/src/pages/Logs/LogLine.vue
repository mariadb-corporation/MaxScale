<template>
    <code
        v-if="source"
        class="d-block mariadb-code-style text-wrap"
        :class="logPriorityColorClasses(source.priority)"
    >
        <span class="mxs-color-helper text-grayed-out">{{ source.timestamp }}&nbsp;&nbsp;</span>
        <span class="log-level d-inline-block">
            <icon-sprite-sheet size="13" :frame="source.priority">
                logPriorities
            </icon-sprite-sheet>
            <span class="tk-azo-sans-web">&nbsp;</span>
            <span>{{ source.priority }}</span>
        </span>
        <span v-html="logLevelNbspGen(source.priority)" />
        <span class="log-level-divider mxs-color-helper text-code-color">:</span>
        <span>&nbsp;</span>
        <span>{{ source.message }}</span>
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
 * Change Date: 2027-11-30
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
                    id: '',
                    timestamp: '',
                    priority: '',
                    message: '',
                }
            },
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
