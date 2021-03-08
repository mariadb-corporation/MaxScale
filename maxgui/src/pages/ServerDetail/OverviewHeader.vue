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
                {{ $t('overview') }}
            </template>
            <template v-slot:card-body>
                <span
                    class="detail-overview__card__name caption text-uppercase font-weight-bold color text-deep-ocean"
                >
                    {{ name.replace('_', ' ') }}
                </span>
                <template v-if="name === 'monitor'">
                    <router-link
                        v-if="value !== 'undefined'"
                        :key="index"
                        :to="`/dashboard/monitors/${value}`"
                        :class="[valueClass, 'no-underline']"
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
                            $vuetify.icons.edit
                        </v-icon>
                    </v-btn>
                </template>

                <span v-else-if="name === 'state'" :class="valueClass">
                    <template v-if="value.indexOf(',') > 0">
                        <span class="color font-weight-bold" :class="[serverStateClass]">
                            {{ value.slice(0, value.indexOf(',')) }}
                        </span>
                        /
                        <span class="color font-weight-bold" :class="[serverStateClass]">
                            {{ value.slice(value.indexOf(',') + 1) }}
                        </span>
                    </template>
                    <span v-else class="color font-weight-bold" :class="[serverStateClass]">
                        {{ value }}
                    </span>
                </span>
                <span v-else :class="valueClass">
                    <template>
                        {{
                            name === 'triggered_at' && value !== 'undefined'
                                ? $help.dateFormat({ value, formatType: 'DATE_RFC2822' })
                                : value
                        }}
                    </template>
                </span>
            </template>
        </outlined-overview-card>

        <select-dialog
            v-model="showSelectDialog"
            :title="dialogTitle"
            mode="change"
            :entityName="targetSelectItemType"
            clearable
            :onClose="handleClose"
            :onCancel="handleClose"
            :handleSave="confirmChange"
            :itemsList="itemsList"
            :defaultItems="defaultItems"
            @selected-items="targetItem = $event"
            @on-open="getAllEntities"
        />
    </v-sheet>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
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
            showSelectDialog: false,
            targetSelectItemType: 'monitors',
            itemsList: [],
            defaultItems: {},
            valueClass: 'detail-overview__card__value text-no-wrap body-2',
        }
    },

    computed: {
        serverStateClass: function() {
            switch (this.$help.serverStateIcon(this.getTopOverviewInfo.state)) {
                case 0:
                    return 'text-error'
                case 1:
                    return 'text-success'
                default:
                    return 'text-warning'
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
                key => (overviewInfo[key] = this.$help.convertType(overviewInfo[key]))
            )
            return overviewInfo
        },
    },
    methods: {
        handleClose() {
            this.showSelectDialog = false
        },
        onEdit(type) {
            this.dialogTitle = `${this.$t(`changeEntity`, {
                entityName: this.$tc(type, 1),
            })}`

            switch (type) {
                case 'monitors':
                    this.targetSelectItemType = type
                    break
            }

            this.showSelectDialog = true
        },
        // -------------------------------------------- Changes handle
        // get available entities and set default item when select-dialog is opened
        async getAllEntities() {
            switch (this.targetSelectItemType) {
                case 'monitors':
                    {
                        const data = await this.getRelationshipData(this.targetSelectItemType)
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
