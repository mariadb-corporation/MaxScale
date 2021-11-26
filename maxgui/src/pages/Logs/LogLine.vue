<template>
    <div v-if="log">
        <span class="pr-4 color text-field-text">{{ log.timestamp }}</span>
        <span
            class="log-level d-inline-flex justify-start"
            :class="logPriorityColorClasses(log.priority)"
        >
            <span>
                <icon-sprite-sheet size="13" :frame="log.priority">
                    logPriorities
                </icon-sprite-sheet>
            </span>
            <span class="ml-1">{{ log.priority }}</span>
        </span>

        <span class="log-level-divider">:</span>

        <span class="text-wrap" :class="logPriorityColorClasses(log.priority)">
            {{ log.message }}
        </span>
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
export default {
    name: 'log-lines',
    props: {
        log: { type: Object, required: true },
    },
    methods: {
        logPriorityColorClasses: level =>
            `color text-${level} ${level === 'alert' ? 'font-weight-bold' : ''}`,
    },
}
</script>
<style lang="scss" scoped>
.log-level {
    width: 75px;
}
.log-lines-wrapper-enter-active,
.log-lines-wrapper-leave-active {
    height: 100%;
    transition: 0.3s all ease;
}

.log-lines-wrapper-enter,
.log-lines-wrapper-leave-active {
    opacity: 0;
    transform: translateY(-20px);
}

pre {
    white-space: pre-wrap;
}
</style>
