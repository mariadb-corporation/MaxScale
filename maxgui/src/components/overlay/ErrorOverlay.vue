<template>
    <v-overlay z-index="9999" :opacity="1" :absolute="true">
        <div class="overlay">
            <div class="overlay-content">
                <WelcomeMessage :greeting="$t('welcomeTo')" />
                <div class="loading-icon">
                    <img src="@/assets/icon-globe-error.svg" alt="MariaDB" class="loading-icon" />
                </div>
                <div class="color text-error text-center">
                    {{ $t('errors.serviceUnavailable') }} <br />
                    {{ $t('errors.refreshLater') }}
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
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import WelcomeMessage from './WelcomeMessage'
export default {
    name: 'error-overlay',
    components: { WelcomeMessage },
}
</script>
