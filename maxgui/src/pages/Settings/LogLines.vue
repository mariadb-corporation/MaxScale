<template>
    <!-- TODO: Replace with v-virtual-scroll once it supports dynamic height -->
    <transition-group v-if="logToShow.length" name="log-lines-wrapper" tag="pre">
        <code
            v-for="{ timestamp, priority, message, id } in logToShow"
            :key="`${id}`"
            :class="[codeClasses()]"
        >
            <span>
                <span class="pr-4 color text-field-text">{{ timestamp }}</span>
                <span
                    class="log-level d-inline-flex justify-start"
                    :class="logPriorityColorClasses(priority)"
                >
                    <span>
                        <icon-sprite-sheet size="13" :frame="priority">
                            logPriorities
                        </icon-sprite-sheet>
                    </span>
                    <span class="ml-1">{{ priority }}</span>
                </span>

                <span class="log-level-divider">:</span>

                <span class="text-wrap" :class="logPriorityColorClasses(priority)">
                    {{ message }}
                </span>
            </span>
        </code>
    </transition-group>

    <code v-else-if="!isLoading" :class="codeClasses()">
        {{ $t('noLogsFound') }}
    </code>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'log-lines',
    props: {
        allLogData: { type: Array, required: true },
        filteredLog: { type: Array, required: true },
        isLoading: { type: Boolean, required: true },
        isFiltering: { type: Boolean, required: true },
    },
    computed: {
        logToShow: function() {
            if (this.isFiltering) return this.filteredLog
            else return this.allLogData
        },
    },

    methods: {
        codeClasses: () => 'log-line mariadb-code-style',
        logPriorityColorClasses: level =>
            `color text-${level} ${level === 'alert' ? 'font-weight-bold' : ''}`,
    },
}
</script>
<style lang="scss" scoped>
.log-line {
    display: block;
}
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
