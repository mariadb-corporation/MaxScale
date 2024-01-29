<template>
    <v-sheet class="d-flex mb-2">
        <outlined-overview-card
            v-for="(value, name, index) in getTopOverviewInfo"
            :key="name"
            wrapperClass="mt-5"
            cardClass="px-10"
            :hoverableCard="name === 'monitor'"
            @card-hover="showEditBtn = $event"
        >
            <template v-if="index === 0" v-slot:title>
                {{ $mxs_t('overview') }}
            </template>
            <template v-slot:card-body>
                <span
                    class="detail-overview__card__name text-caption text-uppercase font-weight-bold mxs-color-helper text-deep-ocean"
                >
                    {{ name.replace('_', ' ') }}
                </span>
                <template v-if="name === 'monitor'">
                    <router-link
                        v-if="value !== 'undefined'"
                        :key="index"
                        :to="`/dashboard/monitors/${value}`"
                        :class="[valueClass, 'rsrc-link']"
                    >
                        <span>{{ value }} </span>
                    </router-link>
                    <span v-else :class="valueClass">
                        {{ value }}
                    </span>
                    <v-btn
                        v-if="showEditBtn"
                        class="monitor-edit-btn"
                        icon
                        @click="() => onEdit('monitors')"
                    >
                        <v-icon size="18" color="primary">
                            $vuetify.icons.mxs_edit
                        </v-icon>
                    </v-btn>
                </template>

                <span v-else-if="name === 'state'" :class="valueClass">
                    <template v-if="value.indexOf(',') > 0">
                        <span class="mxs-color-helper font-weight-bold" :class="[serverStateClass]">
                            {{ value.slice(0, value.indexOf(',')) }}
                        </span>
                        /
                        <span class="mxs-color-helper font-weight-bold" :class="[serverStateClass]">
                            {{ value.slice(value.indexOf(',') + 1) }}
                        </span>
                    </template>
                    <span
                        v-else
                        class="mxs-color-helper font-weight-bold"
                        :class="[serverStateClass]"
                    >
                        {{ value }}
                    </span>
                </span>
                <span v-else :class="valueClass">
                    <template>
                        {{
                            name === 'triggered_at' && value !== 'undefined'
                                ? $helpers.dateFormat({ value })
                                : value
                        }}
                    </template>
                </span>
            </template>
        </outlined-overview-card>

        <mxs-sel-dlg
            v-model="isSelectDlgOpened"
            :title="dialogTitle"
            saveText="change"
            :entityName="targetSelectItemType"
            clearable
            :itemsList="itemsList"
            :defaultItems="defaultItems"
            :onSave="confirmChange"
            @selected-items="targetItem = $event"
            @on-open="getAllEntities"
        />
    </v-sheet>
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

/*
This component:
Emits:
- $emit('on-relationship-update', {
            type: this.targetSelectItemType,
            data: this.targetItem,
        })

*/
import statusIconHelpers from '@share/utils/statusIconHelpers'
import { MXS_OBJ_TYPES } from '@share/constants'

export default {
    name: 'overview-header',
    props: {
        getRelationshipData: { type: Function, required: true },
        currentServer: { type: Object, required: true },
    },
    data() {
        return {
            showEditBtn: false,
            dialogTitle: '',
            targetItem: null,
            //select dialog
            isSelectDlgOpened: false,
            targetSelectItemType: 'monitors',
            itemsList: [],
            defaultItems: {},
            valueClass: 'detail-overview__card__value text-no-wrap text-body-2',
        }
    },

    computed: {
        serverStateClass() {
            switch (statusIconHelpers[MXS_OBJ_TYPES.SERVERS](this.getTopOverviewInfo.state)) {
                case 0:
                    return 'text-error'
                case 1:
                    return 'text-success'
                default:
                    return 'text-grayed-out'
            }
        },
        getTopOverviewInfo: function() {
            const {
                attributes: {
                    state,
                    last_event,
                    triggered_at,
                    parameters: { address, socket, port } = {},
                } = {},
                relationships: { monitors } = {},
            } = this.currentServer

            let overviewInfo = {
                address,
                socket,
                port,
                state,
                last_event,
                triggered_at,
                monitor: monitors ? monitors.data[0].id : 'undefined',
            }

            if (socket) {
                delete overviewInfo.address
                delete overviewInfo.port
            } else delete overviewInfo.socket

            Object.keys(overviewInfo).forEach(
                key => (overviewInfo[key] = this.$helpers.convertType(overviewInfo[key]))
            )
            return overviewInfo
        },
    },
    methods: {
        onEdit(type) {
            this.dialogTitle = `${this.$mxs_t(`changeEntity`, {
                entityName: this.$mxs_tc(type, 1),
            })}`

            switch (type) {
                case 'monitors':
                    this.targetSelectItemType = type
                    break
            }
            this.isSelectDlgOpened = true
        },
        // -------------------------------------------- Changes handle
        // get available entities and set default item when mxs-sel-dlg is opened
        async getAllEntities() {
            switch (this.targetSelectItemType) {
                case 'monitors':
                    {
                        const data = await this.getRelationshipData({
                            type: this.targetSelectItemType,
                        })
                        this.itemsList = data.map(monitor => ({
                            id: monitor.id,
                            type: monitor.type,
                        }))

                        const { monitor: id } = this.getTopOverviewInfo
                        if (id !== 'undefined') {
                            this.defaultItems = {
                                id,
                                type: 'monitors',
                            }
                        } else this.defaultItems = {}
                    }
                    break
            }
        },
        async confirmChange() {
            switch (this.targetSelectItemType) {
                case 'monitors':
                    {
                        await this.$emit('on-relationship-update', {
                            type: this.targetSelectItemType,
                            data: this.targetItem,
                        })
                    }
                    break
            }
        },
    },
}
</script>
<style lang="scss" scoped>
.monitor-edit-btn {
    position: absolute;
    right: 10px;
    bottom: 10px;
}
</style>
