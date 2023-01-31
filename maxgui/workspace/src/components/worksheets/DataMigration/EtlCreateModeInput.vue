<template>
    <div>
        <label class="field__label">
            {{ $mxs_t('createMode') }}
        </label>
        <!-- TODO: Add link to create_mode document -->
        <v-btn
            icon
            x-small
            color="primary"
            class="ml-1"
            target="_blank"
            rel="noopener noreferrer"
            href=""
        >
            <v-icon size="14" color="primary">
                $vuetify.icons.mxs_questionCircle
            </v-icon>
        </v-btn>
        <v-select
            v-model="createMode"
            :items="Object.values(ETL_CREATE_MODES)"
            item-text="text"
            item-value="id"
            name="createMode"
            outlined
            class="vuetify-input--override v-select--mariadb error--text__bottom mb-2"
            :menu-props="{
                contentClass: 'v-select--menu-mariadb',
                bottom: true,
                offsetY: true,
            }"
            dense
            :height="32"
            hide-details="auto"
        />
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
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState } from 'vuex'

export default {
    name: 'etl-create-mode-input',
    computed: {
        ...mapState({
            ETL_CREATE_MODES: state => state.mxsWorkspace.config.ETL_CREATE_MODES,
            create_mode: state => state.etlMem.create_mode,
        }),
        createMode: {
            get() {
                return this.create_mode
            },
            set(v) {
                this.SET_CREATE_MODE(v)
            },
        },
    },

    methods: {
        ...mapMutations({ SET_CREATE_MODE: 'etlMem/SET_CREATE_MODE' }),
    },
}
</script>
