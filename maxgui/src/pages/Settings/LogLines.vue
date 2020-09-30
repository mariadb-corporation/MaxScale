<template>
    <transition-group v-if="logData.length" name="log-lines-wrapper" tag="pre">
        <code
            v-for="{ timestamp, priority, message, id } in logData"
            v-show="!isFiltering || showChosenLevels(priority)"
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
 * Change Date: 2024-09-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'log-lines',
    props: {
        logData: { type: Array, required: true },
        chosenLogLevels: { type: Array, required: true },
        isLoading: { type: Boolean, required: true },
    },
    computed: {
        isFiltering: function() {
            if (this.chosenLogLevels.length === 0) return false
            else return true
        },
    },
    methods: {
        codeClasses: () => 'log-line color text-code-color text-no-wrap',
        logPriorityColorClasses: level =>
            `color text-${level} ${level === 'alert' ? 'font-weight-bold' : ''}`,
        showChosenLevels(priority) {
            if (this.isFiltering && this.chosenLogLevels.includes(priority)) return true
            else return false
        },
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
code {
    background-color: transparent;
    box-shadow: none;
    line-height: 1.5rem;
    padding: 0px 5px 0 10px;
    &::before,
    &::after {
        content: none;
    }
}
</style>
