<template>
    <div class="d-flex justify-end">
        <v-btn
            v-if="step > ETL_STAGE_INDEX.CONN"
            small
            height="36"
            color="primary"
            class="font-weight-medium px-7 text-capitalize"
            rounded
            outlined
            depressed
            @click="$emit('prev', step)"
        >
            {{ $mxs_t('prev') }}
        </v-btn>
        <v-btn
            v-if="step < ETL_STAGE_INDEX.DATA_MIGR"
            small
            height="36"
            color="primary"
            class="ml-2 font-weight-medium px-7 text-capitalize"
            rounded
            depressed
            :disabled="isNextDisabled"
            @click="$emit('next', step)"
        >
            {{ $mxs_t('next') }}
        </v-btn>
        <!-- TODO: Add cancel button? -->
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
import { mapState } from 'vuex'

export default {
    name: 'data-migration-stage-btns',
    props: {
        step: { type: Number, required: true },
        isNextDisabled: { type: Boolean, required: true },
    },
    computed: {
        ...mapState({
            ETL_STAGE_INDEX: state => state.mxsWorkspace.config.ETL_STAGE_INDEX,
        }),
    },
}
</script>
