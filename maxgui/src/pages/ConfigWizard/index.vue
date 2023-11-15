<template>
    <page-wrapper class="fill-height">
        <page-header />
        <v-container fluid class="mt-3 fill-height">
            <v-row class="fill-height">
                <v-col cols="9" class="fill-height d-flex flex-column">
                    <v-tabs
                        v-model="activeIdxStage"
                        vertical
                        class="v-tabs--mariadb v-tabs--mariadb--vert fill-height"
                        hide-slider
                        eager
                    >
                        <v-tab
                            v-for="(stage, type, i) in stageDataMap"
                            :key="i"
                            class="justify-space-between align-center"
                        >
                            <div
                                class="tab-name pa-2 mxs-color-helper text-navigation font-weight-regular"
                            >
                                {{ stage.label }}
                            </div>
                        </v-tab>

                        <v-tabs-items v-model="activeIdxStage" class="fill-height">
                            <v-tab-item
                                v-for="(item, type, i) in stageDataMap"
                                :key="i"
                                class="fill-height"
                            >
                                <overview-stage
                                    v-if="activeIdxStage === 0"
                                    @next="activeIdxStage++"
                                />
                                <obj-stage
                                    v-else-if="activeIdxStage === i"
                                    :objType="type"
                                    :stageDataMap.sync="stageDataMap"
                                    @next="activeIdxStage++"
                                />
                            </v-tab-item>
                        </v-tabs-items>
                    </v-tabs>
                </v-col>
                <v-col v-if="newObjs.length" cols="3">
                    <div class="d-flex flex-column fill-height pb-10">
                        <p
                            class="text-body-2 mxs-color-helper text-navigation font-weight-bold text-uppercase"
                        >
                            {{ $mxs_t('recentlyCreatedObjs') }}
                        </p>
                        <div class="fill-height overflow-y-auto relative">
                            <div class="create-objs-ctr absolute pr-2">
                                <!-- TODO: Add component to show created object -->
                            </div>
                        </div>
                    </div>
                </v-col>
            </v-row>
        </v-container>
    </page-wrapper>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState } from 'vuex'
import PageHeader from '@rootSrc/pages/ConfigWizard/PageHeader'
import OverviewStage from '@rootSrc/pages/ConfigWizard/OverviewStage'
import ObjStage from '@rootSrc/pages/ConfigWizard/ObjStage'

export default {
    components: { PageHeader, OverviewStage, ObjStage },
    data() {
        return {
            activeIdxStage: 0,
            stageDataMap: {},
        }
    },
    computed: {
        ...mapState({
            MXS_OBJ_TYPES: state => state.app_config.MXS_OBJ_TYPES,
            all_modules_map: state => state.maxscale.all_modules_map,
        }),
        overviewStage() {
            return { label: this.$mxs_t('overview'), component: 'overview-stage' }
        },
        newObjs() {
            return Object.values(this.stageDataMap)
                .reduce((acc, stage) => {
                    acc.push(...this.$typy(stage, 'newObjs').safeArray)
                    return acc
                }, [])
                .reverse()
        },
    },
    async created() {
        await this.init()
    },
    methods: {
        ...mapActions({ fetchAllModules: 'maxscale/fetchAllModules' }),
        initStageMapData() {
            this.stageDataMap = Object.values(this.MXS_OBJ_TYPES).reduce(
                (map, type) => {
                    map[type] = {
                        label: this.$mxs_tc(type, 1),
                        component: 'form-ctr',
                        newObjs: [], // objects that have been recently created using the wizard
                        existingObjs: [], // existing object data received from API
                    }
                    return map
                },
                { [this.overviewStage.label]: this.overviewStage }
            )
        },
        async init() {
            this.initStageMapData()
            if (this.$typy(this.all_modules_map).isEmptyObject) await this.fetchAllModules()
        },
    },
}
</script>
<style lang="scss" scoped>
.create-objs-ctr {
    width: 100%;
}
</style>
