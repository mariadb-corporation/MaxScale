<template>
    <div class="filters-wrapper">
        <div class="float-right">
            <v-select
                v-model="chosenLogLevels"
                multiple
                :items="allLogLevels"
                outlined
                dense
                class="std mariadb-select-input"
                :menu-props="{ contentClass: 'mariadb-select-v-menu', bottom: true, offsetY: true }"
                placeholder="Filter by"
                clearable
                @change="updateValue"
            >
                <template v-slot:selection="{ item, index }">
                    <span v-if="index === 0" class="v-select__selection v-select__selection--comma">
                        {{ item }}
                    </span>
                    <span
                        v-if="index === 1"
                        class="v-select__selection v-select__selection--comma color caption text-field-text "
                    >
                        (+{{ chosenLogLevels.length - 1 }} {{ $t('others') }})
                    </span>
                </template>
            </v-select>
        </div>
    </div>
</template>
<script>
export default {
    name: 'log-filter',

    data() {
        return {
            chosenLogLevels: [],
            allLogLevels: ['alert', 'error', 'warning', 'notice', 'info', 'debug'],
        }
    },

    methods: {
        updateValue: function(value) {
            this.$emit('get-chosen-log-levels', value)
        },
    },
}
</script>
<style lang="scss" scoped>
.filters-wrapper {
    height: 50px;
}
</style>
