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
                    class="ma-2 px-2 rounded-lg task-card relative"
                    :class="{ 'mxs-color-helper all-border-separator': card.disabled }"
                    height="90"
                    width="225"
                    :disabled="card.disabled"
                    @click="card.click"
                >
                    <div
                        class="d-flex fill-height align-center justify-center mxs-color-helper card-title"
                        :class="card.disabled ? 'text-grayed-out' : 'text-primary'"
                    >
                        <v-icon
                            :size="card.iconSize"
                            :color="card.disabled ? 'grayed-out' : 'primary'"
                            class="mr-4"
                        >
                            {{ card.icon }}
                        </v-icon>
                        <div class="d-flex flex-column">
                            {{ card.title }}
                            <span class="card-subtitle font-weight-medium">
                                {{ card.subtitle }}
                            </span>
                        </div>
                    </div>
                </v-card>
            </v-col>
            <v-col cols="12" class="d-flex justify-center pa-0">
                <slot name="blank-worksheet-task-cards-bottom" />
            </v-col>
        </v-row>
        <v-row v-if="hasEtlTasks" justify="center">
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
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlTask from '@wsModels/EtlTask'
import EtlTasks from '@wkeComps/BlankWke/EtlTasks.vue'
export default {
    name: 'blank-wke',
    components: { EtlTasks },
    props: {
        ctrDim: { type: Object, required: true },
        cards: { type: Array, required: true },
    },
    data() {
        return {
            taskCardCtrHeight: 200,
        }
    },
    computed: {
        migrationTaskTblHeight() {
            return this.ctrDim.height - this.taskCardCtrHeight - 12 - 24 - 80 // minus grid padding
        },
        hasEtlTasks() {
            return Boolean(EtlTask.all().length)
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
                opacity: 1;
                .card-subtitle {
                    font-size: 0.75rem;
                }
            }
        }
    }
}
</style>
