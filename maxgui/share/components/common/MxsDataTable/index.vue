<template>
    <v-data-table
        class="v-data-table--mariadb"
        v-bind="{ ...$attrs }"
        :header-props="{ sortIcon: '$vuetify.icons.mxs_arrowDown' }"
        v-on="$listeners"
    >
        <template v-for="slot in Object.keys($scopedSlots)" v-slot:[slot]="slotData">
            <slot :name="slot" v-bind="slotData" />
        </template>
    </v-data-table>
</template>

<script>
export default {
    name: 'mxs-data-table',
    inheritAttrs: false,
}
</script>

<style lang="scss">
.v-data-table--mariadb {
    thead {
        tr {
            box-shadow: -7px 5px 7px -7px rgba(0, 0, 0, 0.1);
            th {
                background-color: $table-border !important;
                border-bottom: none !important;
                padding: 0 24px !important;
                color: $small-text !important;
                text-transform: uppercase;
                font-size: 11px;
                white-space: nowrap;
                &:first-child {
                    border-radius: 5px 0 0 0;
                }
                &:last-child {
                    border-radius: 0 5px 0 0;
                }
                .v-data-table-header__icon {
                    margin-bottom: 3px;
                    margin-left: 16px;
                    font-size: 14px !important;
                    height: 14px !important;
                    width: 14px !important;
                }
                &.active {
                    color: black !important;
                    .v-data-table-header__icon {
                        color: black !important;
                    }
                }
            }
        }
    }
    tbody {
        tr {
            &:hover {
                background: #fafcfc !important;
            }
            &:active {
                background: #f2fcff !important;
            }
            &.v-data-table__selected {
                background: #9fd7e6 !important;
            }

            td {
                padding: 0 24px !important;
                color: $navigation !important;
                border-bottom: thin solid $table-border !important;
                &:last-child:not(.hide) {
                    border-right: thin solid $table-border;
                }
                &:first-child:not(.hide) {
                    border-left: thin solid $table-border;
                }
            }
        }
    }
    .v-data-footer {
        border-top: none !important;
    }
}
</style>
