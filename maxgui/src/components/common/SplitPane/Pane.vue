<template>
    <div :class="`splitter-pane ${splitTypeClass} ${panePosClass}`">
        <slot />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'pane',
    props: {
        isLeft: { type: Boolean, default: false },
    },
    computed: {
        splitTypeClass() {
            return `splitter-pane--${this.$parent.split}`
        },
        panePosClass() {
            return `${this.isLeft ? `${this.splitTypeClass}-left` : `${this.splitTypeClass}-right`}`
        },
    },
}
</script>

<style lang="scss" scoped>
.splitter-pane {
    position: absolute;
    /* TODO: transition time affects the process of getting dim through ref
     * Add delay when calculating dim
     */
    /*   transition: 0.2s cubic-bezier(0.4, 0, 0.2, 1); */
    overflow: hidden;
    &--vert {
        height: 100%;
        &-left {
            left: 0;
        }
        &-right {
            right: 0;
        }
    }

    &--horiz {
        width: 100%;
        &-left {
            top: 0;
        }
        &-right {
            bottom: 0;
        }
    }
}
</style>
