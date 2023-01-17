<template>
    <div class="stage-wrapper d-flex flex-column fill-height">
        <div v-if="$slots['header']" class="stage-wrapper__header d-flex align-center mx-3">
            <slot name="header" />
        </div>
        <div v-if="$slots['body']" class="stage-wrapper__body">
            <v-container fluid class="fill-height">
                <v-row class="fill-height">
                    <slot name="body" />
                </v-row>
            </v-container>
        </div>
        <div v-if="$slots['footer']" class="stage-wrapper__footer d-flex align-end mx-3">
            <slot name="footer" />
        </div>
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
export default {
    name: 'etl-stage-ctr',
}
</script>
<style lang="scss">
.stage-wrapper {
    position: absolute;
    top: 0;
    left: 0;
    width: 100%;
    &__header {
        height: 50px;
    }
    &__body {
        flex: 1;
        overflow-y: auto;
    }
    &__footer {
        height: 80px;
    }
    .etl-stage-title {
        line-height: 36px;
        width: 100%;
        height: 50px;
        display: flex;
        align-items: center;
    }
}
</style>
