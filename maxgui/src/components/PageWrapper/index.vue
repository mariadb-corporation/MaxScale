<template>
    <div class="page-wrapper">
        <div
            class="page-wrapper__container d-flex flex-column"
            :class="{ 'page-wrapper__container__fluid': fluid }"
        >
            <div class="d-flex flex-wrap">
                <portal-target name="page-header" />
                <slot name="page-header" />
                <v-spacer :style="spacerStyle" />
                <slot name="page-header--right" />
                <portal-target name="page-header--right" />
            </div>
            <slot></slot>
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
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    name: 'page-wrapper',
    props: {
        fluid: { type: Boolean, default: false },
        spacerStyle: { type: Object, default: () => {} },
    },
}
</script>

<style lang="scss" scoped>
//https://gs.statcounter.com/screen-resolution-stats top 2 for desktop screen resolution
.page-wrapper {
    padding: 24px 36px;
    .page-wrapper__container {
        max-width: 1366px; // preserve ratio for larger screen
        margin: 0 auto;
        height: inherit;
        &__fluid {
            max-width: 100%;
        }
    }
}
</style>
