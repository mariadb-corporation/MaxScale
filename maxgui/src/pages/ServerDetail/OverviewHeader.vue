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
                        class="detail-overview__card__value body-2 no-underline"
                    >
                        <span>{{ value }} </span>
                    </router-link>
                    <div v-if="showEditBtn" style="position:absolute;right:10px;bottom:10px">
                        <v-btn icon @click="() => onEdit('monitors')">
                            <v-icon size="18" color="primary">
                                $vuetify.icons.edit
                            </v-icon>
                        </v-btn>
                    </div>
                </template>
                <span
                    v-else-if="name === 'state'"
                    class="detail-overview__card__value text-no-wrap body-2"
                >
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
                <span v-else class="detail-overview__card__value text-no-wrap body-2">
                    <template v-if="value !== 'undefined'">
                        {{
                            name === 'triggered_at' && value !== 'undefined'
                                ? $help.formatValue(value, 'DATE_RFC2822')
                                : value
                        }}
                    </template>
                </span>
            </template>
        </outlined-overview-card>

        <select-dialog
            v-if="!$help.lodash.isEmpty(getTopOverviewInfo)"
            v-model="showSelectDialog"
            :title="dialogTitle"
            mode="change"
            :entityName="targetSelectItemType"
            :onClose="() => (showSelectDialog = false)"
            :onCancel="() => (showSelectDialog = false)"
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
 * Change Date: 2024-07-16
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
import { mapGetters } from 'vuex'

export default {
    name: 'overview-header',
    props: {
        getRelationshipData: { type: Function, required: true },
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
            defaultItems: undefined,
        }
    },

    computed: {
        ...mapGetters({
            currentServer: 'server/currentServer',
        }),
        serverStateClass: function() {
            switch (this.$help.serverStateIcon(this.currentServer.attributes.state)) {
                case 0:
                    return 'text-error'
                case 1:
                    return 'text-success'
                default:
                    return 'text-warning'
            }
        },
        getTopOverviewInfo: function() {
            const currentServer = this.$help.lodash.cloneDeep(this.currentServer)
            let overviewInfo = {}

            // Set fallback undefined value if properties doesnt exist
            const {
                attributes: {
                    state,
                    last_event = undefined,
                    triggered_at = undefined,
                    parameters: { address = undefined, socket = undefined, port = undefined } = {},
                } = {},
                relationships: { monitors } = {},
            } = currentServer

            overviewInfo = {
                address: address,
                socket: socket,
                port: port,
                state: state,
                last_event: last_event,
                triggered_at: triggered_at,
                monitor: monitors ? monitors.data[0].id : undefined,
            }

            if (socket) {
                delete overviewInfo.address
                delete overviewInfo.port
            } else delete overviewInfo.socket

            Object.keys(overviewInfo).forEach(
                key => (overviewInfo[key] = this.$help.handleValue(overviewInfo[key]))
            )

            return overviewInfo
        },
    },
    methods: {
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

                        const { monitor: currentMonitorId } = this.getTopOverviewInfo
                        if (currentMonitorId !== 'undefined') {
                            this.defaultItems = {
                                id: currentMonitorId,
                                type: 'monitors',
                            }
                        } else this.defaultItems = undefined
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
