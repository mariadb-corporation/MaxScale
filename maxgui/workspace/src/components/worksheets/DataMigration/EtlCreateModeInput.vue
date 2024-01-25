<template>
    <div>
        <label class="field__label">
            {{ $mxs_t('createMode') }}
        </label>
        <v-tooltip top transition="slide-y-transition">
            <template v-slot:activator="{ on }">
                <v-icon class="ml-1 pointer" size="14" color="primary" v-on="on">
                    $vuetify.icons.mxs_questionCircle
                </v-icon>
            </template>

            <span>{{ $mxs_t('modesForCreatingTbl') }}</span>
            <table>
                <tr v-for="(v, key) in ETL_CREATE_MODES" :key="`${key}`">
                    <td>{{ v }}:</td>
                    <td class="font-weight-bold">{{ $mxs_t(`info.etlCreateMode.${v}`) }}</td>
                </tr>
            </table>
        </v-tooltip>
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
            hide-details
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
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlTask from '@wsModels/EtlTask'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import { ETL_CREATE_MODES } from '@wsSrc/constants'

export default {
    name: 'etl-create-mode-input',
    props: {
        taskId: { type: String, required: true },
    },
    computed: {
        createMode: {
            get() {
                return EtlTask.getters('findCreateMode')(this.taskId)
            },
            set(v) {
                EtlTaskTmp.update({ where: this.taskId, data: { create_mode: v } })
            },
        },
    },
    created() {
        this.ETL_CREATE_MODES = ETL_CREATE_MODES
    },
}
</script>
