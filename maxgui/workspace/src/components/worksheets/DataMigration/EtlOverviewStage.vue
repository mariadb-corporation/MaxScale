<template>
    <mxs-stage-ctr>
        <template v-slot:header>
            <v-icon size="48" color="info" class="mr-5 mt-n1">
                $vuetify.icons.mxs_dataMigration
            </v-icon>
            <h3 class="mxs-stage-ctr__title mxs-color-helper text-navigation font-weight-light">
                {{ $mxs_t('dataMigration') }}
            </h3>
        </template>
        <template v-slot:body>
            <v-container fluid class="fill-height">
                <v-row class="fill-height">
                    <v-col cols="12" md="6" class="fill-height mxs-color-helper text-navigation">
                        <p>
                            {{ $mxs_t('info.etlOverviewInfo') }}
                        </p>
                        <a
                            target="_blank"
                            href="https://mariadb.com/kb/en/mariadb-maxscale-2302-sql-resource/#prepare-etl-operation"
                            rel="noopener noreferrer"
                            class="rsrc-link"
                        >
                            {{ $mxs_t('info.etlDocLinkText') }}
                        </a>
                        <a
                            target="_blank"
                            href="https://mariadb.com/kb/en/mariadb-maxscale-2302-limitations-and-known-issues-within-mariadb-maxscale/#etl-limitations"
                            rel="noopener noreferrer"
                            class="d-block rsrc-link"
                        >
                            {{ $mxs_t('limitations') }}
                        </a>
                    </v-col>
                </v-row>
            </v-container>
        </template>
        <template v-slot:footer>
            <v-btn
                small
                height="36"
                color="primary"
                class="mt-auto font-weight-medium px-7 text-capitalize"
                rounded
                depressed
                :disabled="disabled"
                @click="next"
            >
                {{ $mxs_t('setUpConns') }}
            </v-btn>
        </template>
    </mxs-stage-ctr>
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
import EtlTask from '@wsModels/EtlTask'
import { ETL_STATUS } from '@wsSrc/constants'

export default {
    name: 'etl-overview-stage',
    props: {
        task: { type: Object, required: true },
        hasConns: { type: Boolean, required: true },
    },
    computed: {
        disabled() {
            const { RUNNING, COMPLETE } = ETL_STATUS
            const { status } = this.task
            return this.hasConns || status === COMPLETE || status === RUNNING
        },
    },
    methods: {
        next() {
            EtlTask.update({
                where: this.task.id,
                data(obj) {
                    obj.active_stage_index = obj.active_stage_index + 1
                },
            })
        },
    },
}
</script>
