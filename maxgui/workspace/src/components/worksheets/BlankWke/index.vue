<template>
    <v-container class="blank-wke-ctr">
        <v-row ref="taskCardCtr" class="task-card-ctr" justify="center">
            <v-col cols="12" class="d-flex justify-center pa-0">
                <h6 class="mxs-color-helper text-navigation font-weight-regular">
                    {{ $mxs_t('chooseTask') }}
                </h6>
            </v-col>
            <v-col cols="12" class="d-flex flex-row flex-wrap justify-center py-2">
                <v-card
                    v-for="(card, i) in cards"
                    :key="i"
                    outlined
                    class="px-2 rounded-lg task-card relative ma-2"
                    height="90"
                    width="225"
                    @click="card.click"
                >
                    <v-card-title
                        class="pa-0 fill-height justify-center align-center card-title font-weight-regular mxs-color-helper text-primary"
                    >
                        <v-icon :size="card.iconSize" color="primary" class="mr-4">
                            {{ card.icon }}
                        </v-icon>
                        {{ card.text }}
                    </v-card-title>
                </v-card>
            </v-col>
        </v-row>
        <v-row justify="center">
            <v-col cols="12" md="10">
                <h6 class="mxs-color-helper text-navigation font-weight-regular">
                    {{ $mxs_t('migrationTasks') }}
                </h6>
                <etl-tasks :height="migrationTaskTblHeight" />
            </v-col>
        </v-row>
    </v-container>
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
import EtlTask from '@wsModels/EtlTask'
import Worksheet from '@wsModels/Worksheet'
import EtlTasks from '@wkeComps/BlankWke/EtlTasks.vue'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import { insertQueryTab } from '@wsSrc/store/orm/initEntities'

export default {
    name: 'blank-wke',
    components: { EtlTasks },
    props: {
        ctrDim: { type: Object, required: true },
    },
    data() {
        return {
            taskCardCtrHeight: 200,
        }
    },
    computed: {
        activeWkeId() {
            return Worksheet.getters('getActiveWkeId')
        },
        cards() {
            return [
                {
                    text: this.$mxs_t('runQueries'),
                    icon: '$vuetify.icons.mxs_workspace',
                    iconSize: 26,
                    click: async () => await this.runQueries(),
                },
                {
                    text: this.$mxs_t('dataMigration'),
                    icon: '$vuetify.icons.mxs_dataMigration',
                    iconSize: 32,
                    click: async () => await this.createEtlTask(),
                },
                /*  {
                    text: this.$mxs_t('createAnErd'),
                    icon: '$vuetify.icons.mxs_erd',
                    iconSize: 32,
                    click: () => null,
                }, */
            ]
        },
        migrationTaskTblHeight() {
            return this.ctrDim.height - this.taskCardCtrHeight - 12 - 24 - 80 // minus grid padding
        },
    },
    mounted() {
        this.setTaskCardCtrHeight()
    },
    methods: {
        setTaskCardCtrHeight() {
            const { height } = this.$refs.taskCardCtr.getBoundingClientRect()
            this.taskCardCtrHeight = height
        },
        async createEtlTask() {
            await EtlTask.dispatch('insertEtlTask')
        },
        runQueries() {
            QueryEditorTmp.insert({ data: { id: this.activeWkeId } })
            SchemaSidebar.insert({ data: { id: this.activeWkeId } })
            insertQueryTab(this.activeWkeId)
        },
    },
}
</script>

<style lang="scss" scoped>
.blank-wke-ctr {
    .task-card-ctr {
        padding-top: 90px;
        .task-card {
            border-color: #bed1da;

            .card-title {
                font-size: 1.125rem;
            }
        }
    }
}
</style>
