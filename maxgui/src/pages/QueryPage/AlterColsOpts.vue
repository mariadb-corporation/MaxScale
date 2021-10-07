<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 d-flex align-center flex-1">
            <v-spacer />
            <v-tooltip
                v-if="selectedItems.length"
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        x-small
                        class="mr-2 pa-1 text-capitalize"
                        outlined
                        depressed
                        color="error"
                        v-on="on"
                        @click="deleteSelectedRows(selectedItems)"
                    >
                        {{ $t('drop') }}
                    </v-btn>
                </template>
                <span>{{ $t('dropSelectedCols') }}</span>
            </v-tooltip>
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        x-small
                        class="pa-1 text-capitalize"
                        outlined
                        depressed
                        color="primary"
                        v-on="on"
                        @click="addNewCol"
                    >
                        {{ $t('add') }}
                    </v-btn>
                </template>
                <span>{{ $t('addNewCol') }}</span>
            </v-tooltip>
        </div>
        <!-- TODO: make virtual-scroll-table cell editable -->
        <virtual-scroll-table
            :benched="0"
            :headers="headers"
            :rows="$typy(colsOptsData, 'data').safeArray"
            :itemHeight="30"
            :height="height - headerHeight"
            :boundingWidth="boundingWidth"
            showSelect
            v-on="$listeners"
            @item-selected="selectedItems = $event"
        />
    </div>
</template>

<script>
export default {
    name: 'alter-cols-opts',
    props: {
        value: { type: Object, required: true },
        height: { type: Number, required: true },
        boundingWidth: { type: Number, required: true },
    },
    data() {
        return {
            selectedItems: [],
            headerHeight: 0,
        }
    },
    computed: {
        colsOptsData: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        headers() {
            return this.$typy(this.colsOptsData, 'fields').safeArray.map(field => ({ text: field }))
        },
    },
    watch: {
        colsOptsData(v) {
            if (!this.$typy(v).isEmptyObject) this.setHeaderHeight()
        },
    },
    methods: {
        setHeaderHeight() {
            if (this.$refs.header) this.headerHeight = this.$refs.header.clientHeight
        },
        // TODO: add handlers
        deleteSelectedRows(selectedItems) {
            console.log('DELETE selectedItems', selectedItems)
        },
        addNewCol() {},
    },
}
</script>

<style lang="scss" scoped></style>
