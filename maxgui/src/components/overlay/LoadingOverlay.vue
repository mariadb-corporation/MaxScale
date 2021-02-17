<template>
    <v-overlay z-index="9999" :opacity="1" :absolute="true">
        <div class="overlay">
            <div class="overlay-content">
                <loading-message :text="$t('welcomeTo')" />
                <div class="loading-icon">
                    <img src="@/assets/icon-globe.svg" alt="MariaDB" />
                </div>
                <div class="color text-accent text-center">
                    {{ $t('initializing') }}
                </div>
            </div>
        </div>
    </v-overlay>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import LoadingMessage from './LoadingMessage'
export default {
    name: 'load-overlay',
    components: { LoadingMessage },
}
</script>
