<template>
    <v-overlay z-index="9999">
        <div class="overlay-wrapper d-flex flex-column justify-center align-center fill-height">
            <div class="welcome-txt text-center mxs-color-helper text-text-subtle">
                <div v-if="showWelcomeTxt">{{ $mxs_t('welcomeTo') }}</div>
                <div class="font-weight-medium">{{ $mxs_t('mariaDbMaxScale') }}</div>
            </div>
            <div class="loading-icon">
                <img src="@share/assets/icon-globe.svg" alt="MariaDB" />
            </div>
            <div class="mxs-color-helper text-accent text-center">
                {{ msg }}
            </div>
        </div>
    </v-overlay>
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
    name: 'loading-overlay',
    props: {
        msg: { type: String, default: '' },
        showWelcomeTxt: { type: Boolean, default: true },
    },
}
</script>
<style lang="scss" scoped>
.overlay-wrapper {
    background: radial-gradient(1100px at 100% 89%, $accent 0%, $deep-ocean 100%);

    .welcome-txt {
        font-size: 30px;
    }
}
</style>
