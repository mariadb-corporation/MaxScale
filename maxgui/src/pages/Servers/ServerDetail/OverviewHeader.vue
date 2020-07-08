<template>
    <v-sheet class="d-flex mb-2">
        <outlined-overview-card
            v-for="(value, name, index) in getTopOverviewInfo"
            :key="name"
            cardWrapper="mt-5"
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
            @onOpen="getAllEntities"
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
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    name: 'overview-header',

    props: {
        currentServer: { type: Object, required: true },
        updateServerRelationship: { type: Function, required: true },
        dispatchRelationshipUpdate: { type: Function, required: true },
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
            let self = this
            let currentServer = self.$help.lodash.cloneDeep(self.currentServer)
            let overviewInfo = {}
            if (!self.$help.lodash.isEmpty(currentServer)) {
                // Set fallback undefined value if properties doesnt exist
                const {
                    attributes: {
                        state,
                        last_event = undefined,
                        triggered_at = undefined,

                        parameters: {
                            address = undefined,
                            socket = undefined,
                            port = undefined,
                        } = {},
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
                    key => (overviewInfo[key] = self.$help.handleValue(overviewInfo[key]))
                )
            }
            return overviewInfo
        },
    },
    methods: {
        onEdit(type) {
            let self = this
            self.dialogTitle = `${self.$t(`changeEntity`, {
                entityName: self.$tc(type, 1),
            })}`

            switch (type) {
                case 'monitors':
                    self.targetSelectItemType = type
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
                        const self = this
                        let res = await self.axios.get(`/monitors?fields[monitors]=state`)
                        let all = res.data.data.map(monitor => ({
                            id: monitor.id,
                            type: monitor.type,
                        }))

                        if (self.getTopOverviewInfo.monitor !== 'undefined') {
                            self.defaultItems = {
                                id: self.getTopOverviewInfo.monitor,
                                type: 'monitors',
                            }
                        } else {
                            self.defaultItems = undefined
                        }

                        this.itemsList = all
                    }
                    break
            }
        },
        async confirmChange() {
            let self = this

            switch (self.targetSelectItemType) {
                case 'monitors':
                    {
                        await self.dispatchRelationshipUpdate(
                            self.targetSelectItemType,
                            self.targetItem
                        )
                    }
                    break
            }
        },
    },
}
</script>
