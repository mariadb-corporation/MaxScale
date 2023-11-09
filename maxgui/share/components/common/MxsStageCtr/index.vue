<template>
    <div class="mxs-stage-ctr d-flex flex-column fill-height" :class="className">
        <div v-if="$slots['header']" class="mxs-stage-ctr__header d-flex mx-3 pt-2">
            <slot name="header" />
        </div>
        <div v-if="$slots['body']" class="mxs-stage-ctr__body">
            <v-container fluid class="fill-height">
                <slot name="body" />
            </v-container>
        </div>
        <div v-if="$slots['footer']" class="mxs-stage-ctr__footer d-flex align-end mx-3">
            <slot name="footer" />
        </div>
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
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'mxs-stage-ctr',
    props: {
        className: { type: [String, Array, Object], default: 'pt-4 pr-4 pb-8 pl-8' },
    },
}
</script>
<style lang="scss">
.mxs-stage-ctr {
    position: absolute;
    top: 0;
    left: 0;
    width: 100%;
    &__body {
        flex: 1;
        overflow-y: auto;
    }
    .mxs-stage-ctr__title {
        line-height: 36px;
    }
}
</style>
