<template>
    <div :class="`split-pane ${splitTypeClass} ${panePosClass}`">
        <slot />
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
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'split-pane',
    props: {
        split: { type: String, required: true },
        isLeft: { type: Boolean, default: false },
    },
    computed: {
        splitTypeClass() {
            return `split-pane--${this.split}`
        },
        panePosClass() {
            return `${this.isLeft ? `${this.splitTypeClass}-left` : `${this.splitTypeClass}-right`}`
        },
    },
}
</script>

<style lang="scss" scoped>
.split-pane {
    position: absolute;
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
