<template>
    <div
        :class="
            `resizer resizer--${split} resizer--${split}-${active ? 'resizing' : 'not-resizing'}`
        "
    />
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'resizer',
    props: {
        split: {
            validator(value) {
                return ['vert', 'horiz'].indexOf(value) !== -1
            },
            required: true,
        },
        active: { type: Boolean, required: true },
    },
}
</script>
<style lang="scss" scoped>
.resizer {
    position: absolute;
    opacity: 0.5;
    z-index: 1;
    &--horiz {
        height: 11px;
        margin: -5px 0;
        border-top: 5px solid transparent;
        border-bottom: 5px solid transparent;
        cursor: row-resize;
        width: 100%;
        &-resizing,
        &:hover {
            border-top: 5px solid $table-border;
            border-bottom: 5px solid $table-border;
        }
    }
    &--vert {
        width: 11px;
        height: 100%;
        margin-left: -5px;
        border-left: 5px solid transparent;
        border-right: 5px solid transparent;
        cursor: col-resize;
        &-resizing,
        &:hover {
            border-left: 5px solid $table-border;
            border-right: 5px solid $table-border;
        }
    }
}
</style>
